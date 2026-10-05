#define _POSIX_C_SOURCE 200809L

#include "dag.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Cuantos IDs se muestran como maximo al describir un ciclo.
#define MAX_IDS_EN_CICLO 8

// Par (ID, posicion) para el indice ordenado que usa bsearch.
typedef struct {
    const char* id;
    int pos;
} EntradaIndice;

static int cmp_entradas(const void* a, const void* b) {
    const EntradaIndice* x = a;
    const EntradaIndice* y = b;
    return strcmp(x->id, y->id);
}

static int cmp_enteros(const void* a, const void* b) {
    int x = *(const int*)a;
    int y = *(const int*)b;
    return (x > y) - (x < y);
}

static void poner_error(char* err, size_t err_tam, const char* fmt, ...) {
    va_list ap;

    if (err == NULL || err_tam == 0) return;
    va_start(ap, fmt);
    vsnprintf(err, err_tam, fmt, ap);
    va_end(ap);
}

// Agrega txt al final de buf sin desbordarlo. *uso lleva el largo escrito.
static void anexar(char* buf, size_t tam, size_t* uso, const char* txt) {
    int w;

    if (buf == NULL || *uso + 1 >= tam) return;
    w = snprintf(buf + *uso, tam - *uso, "%s", txt);
    if (w > 0) *uso += (size_t)w;
}

// Escribe en err el ciclo camino[desde..hasta]. Cada actividad depende de
// la siguiente, y la ultima depende de la primera.
static void describir_ciclo(const DAG* dag, const int* camino, int desde,
                            int hasta, char* err, size_t err_tam) {
    size_t uso = 0;
    int largo = hasta - desde + 1;
    int mostrar = largo < MAX_IDS_EN_CICLO ? largo : MAX_IDS_EN_CICLO;
    char num[32];

    if (err == NULL || err_tam == 0) return;
    err[0] = '\0';
    anexar(err, err_tam, &uso,
           "Ciclo en las dependencias (cada actividad depende de la siguiente): ");
    for (int k = 0; k < mostrar; k++) {
        anexar(err, err_tam, &uso, dag->array[camino[desde + k]].id);
        anexar(err, err_tam, &uso, " -> ");
    }
    if (mostrar < largo) {
        snprintf(num, sizeof num, "%d", largo);
        anexar(err, err_tam, &uso, "... (ciclo de ");
        anexar(err, err_tam, &uso, num);
        anexar(err, err_tam, &uso, " actividades)");
    } else {
        anexar(err, err_tam, &uso, dag->array[camino[desde]].id);
    }
}

void dag_free(DAG* dag) {
    if (!dag) return;
    for (int i = 0; i < dag->count; i++) {
        free(dag->array[i].dependents);
    }
    free(dag->array);
    free(dag);
}

DAG* dag_build(RawPlan* plan, char* err, size_t err_tam) {
    DAG* dag = NULL;
    EntradaIndice* indice = NULL;
    int** deps = NULL;     // deps[i]: posiciones de las que depende i (sin repetir)
    int* n_deps = NULL;
    int* cola = NULL;      // cola de Kahn; luego se reusa como camino del ciclo
    int* pend = NULL;
    int* visita = NULL;
    int cabeza = 0;
    int cantidad = 0;
    int n;
    bool ok = false;

    if (!plan) {
        poner_error(err, err_tam, "No hay plan que construir");
        return NULL;
    }
    n = plan->count;

    dag = calloc(1, sizeof(DAG));
    if (!dag) {
        poner_error(err, err_tam, "Sin memoria al construir el grafo");
        return NULL;
    }
    dag->array = calloc((size_t)(n > 0 ? n : 1), sizeof(Activity));
    indice = malloc((size_t)(n > 0 ? n : 1) * sizeof *indice);
    deps = calloc((size_t)(n > 0 ? n : 1), sizeof *deps);
    n_deps = calloc((size_t)(n > 0 ? n : 1), sizeof *n_deps);
    cola = malloc((size_t)(n > 0 ? n : 1) * sizeof *cola);
    pend = malloc((size_t)(n > 0 ? n : 1) * sizeof *pend);
    if (!dag->array || !indice || !deps || !n_deps || !cola || !pend) {
        poner_error(err, err_tam, "Sin memoria al construir el grafo");
        goto limpiar;
    }
    dag->count = n;

    // 1. Traspasar los datos y armar el indice (ID -> posicion).
    for (int i = 0; i < n; i++) {
        Activity* act = &dag->array[i];
        const RawActivity* raw = &plan->items[i];

        if (raw->duration_ms < 0) {
            poner_error(err, err_tam,
                        "La actividad '%s' tiene una duracion negativa", raw->id);
            goto limpiar;
        }
        snprintf(act->id, MAX_ID_LEN, "%s", raw->id);
        snprintf(act->name, MAX_NAME_LEN, "%s", raw->name);
        act->duration_ms = raw->duration_ms;
        indice[i].id = raw->id;
        indice[i].pos = i;
    }

    // 2. Ordenar el indice y detectar IDs repetidos (quedan vecinos).
    qsort(indice, (size_t)n, sizeof *indice, cmp_entradas);
    for (int i = 1; i < n; i++) {
        if (strcmp(indice[i - 1].id, indice[i].id) == 0) {
            poner_error(err, err_tam, "ID de actividad repetido: '%s'",
                        indice[i].id);
            goto limpiar;
        }
    }

    // 3. Traducir las dependencias de texto a posiciones (sin repetidas).
    for (int i = 0; i < n; i++) {
        const RawActivity* raw = &plan->items[i];
        int nd = raw->raw_dep_count;
        int m = 0;

        if (nd > 0) {
            deps[i] = malloc((size_t)nd * sizeof(int));
            if (!deps[i]) {
                poner_error(err, err_tam, "Sin memoria al construir el grafo");
                goto limpiar;
            }
            for (int k = 0; k < nd; k++) {
                EntradaIndice clave;
                EntradaIndice* r;

                clave.id = raw->raw_dependencies[k];
                clave.pos = 0;
                r = bsearch(&clave, indice, (size_t)n, sizeof *indice,
                            cmp_entradas);
                if (!r) {
                    poner_error(err, err_tam,
                                "La actividad '%s' depende de '%s', que no existe",
                                raw->id, raw->raw_dependencies[k]);
                    goto limpiar;
                }
                deps[i][k] = r->pos;
            }
            qsort(deps[i], (size_t)nd, sizeof(int), cmp_enteros);
            for (int k = 0; k < nd; k++) {
                if (m == 0 || deps[i][k] != deps[i][m - 1]) deps[i][m++] = deps[i][k];
            }
        }
        n_deps[i] = m;
        dag->array[i].in_degree = m;
    }

    // 4. Armar las listas de dependientes en dos pasadas (memoria exacta).
    for (int i = 0; i < n; i++)
        for (int k = 0; k < n_deps[i]; k++)
            dag->array[deps[i][k]].dep_count++;

    for (int j = 0; j < n; j++) {
        Activity* a = &dag->array[j];
        if (a->dep_count > 0) {
            a->dependents = malloc((size_t)a->dep_count * sizeof(int));
            if (!a->dependents) {
                poner_error(err, err_tam, "Sin memoria al construir el grafo");
                goto limpiar;
            }
            a->dep_capacity = a->dep_count;
            a->dep_count = 0;   // ahora sirve de cursor
        }
    }
    for (int i = 0; i < n; i++) {
        for (int k = 0; k < n_deps[i]; k++) {
            Activity* p = &dag->array[deps[i][k]];
            p->dependents[p->dep_count++] = i;
        }
    }

    // 5. Detectar ciclos con el algoritmo de Kahn (orden topologico).
    for (int i = 0; i < n; i++) {
        pend[i] = n_deps[i];
        if (pend[i] == 0) cola[cantidad++] = i;
    }
    while (cabeza < cantidad) {
        int u = cola[cabeza++];
        for (int k = 0; k < dag->array[u].dep_count; k++) {
            int v = dag->array[u].dependents[k];
            if (--pend[v] == 0) cola[cantidad++] = v;
        }
    }

    if (cantidad < n) {
        // Los nodos con pend > 0 estan en un ciclo o cuelgan de uno. Cada
        // uno tiene una dependencia que tambien quedo sin procesar, asi que
        // seguirlas termina repitiendo un nodo, y ese tramo es un ciclo.
        int actual = 0;
        int largo = 0;

        visita = malloc((size_t)n * sizeof *visita);
        if (!visita) {
            poner_error(err, err_tam, "Hay un ciclo en las dependencias del plan");
            goto limpiar;
        }
        for (int i = 0; i < n; i++) visita[i] = -1;
        while (pend[actual] == 0) actual++;

        while (visita[actual] < 0) {
            int siguiente = -1;

            visita[actual] = largo;
            cola[largo++] = actual;
            for (int k = 0; k < n_deps[actual]; k++) {
                if (pend[deps[actual][k]] > 0) {
                    siguiente = deps[actual][k];
                    break;
                }
            }
            if (siguiente < 0) {
                poner_error(err, err_tam, "Hay un ciclo en las dependencias del plan");
                goto limpiar;
            }
            actual = siguiente;
        }
        describir_ciclo(dag, cola, visita[actual], largo - 1, err, err_tam);
        goto limpiar;
    }

    ok = true;

limpiar:
    free(indice);
    if (deps) {
        for (int i = 0; i < n; i++) free(deps[i]);
    }
    free(deps);
    free(n_deps);
    free(cola);
    free(pend);
    free(visita);
    if (!ok) {
        dag_free(dag);
        return NULL;
    }
    return dag;
}
