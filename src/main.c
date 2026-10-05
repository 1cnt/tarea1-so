#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "dag.h"
#include "parser.h"

#define MAX_MSG 128
#define RESERVA_FDS 32

typedef enum { PENDIENTE, LISTA, CORRIENDO, TERMINADA, FALLIDA, ABORTADA } Estado;

typedef struct {
    pid_t pid;
    int task_idx;  
    int fd_up;    
} ActiveProcess;

typedef struct {
    DAG* dag;
    Estado* estado;          
    int* dep_off;            
    int* dep_idx;            
    int* cola;               
    int q_front;
    int q_rear;
    int* pila;               
    ActiveProcess* slots;
    int n_slots;             
    int* libres;             
    int n_libres;
    int activos;             
    int terminadas;
    int fallidas;
    int abortadas;
    bool sin_recursos;    
} Plan;

static Plan P;
static volatile sig_atomic_t g_sigint = 0;
static sigset_t g_mask_orig;    

static void log_linea(const char* fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

static void marcar(int i, Estado e) {
    Activity* a = &P.dag->array[i];

    P.estado[i] = e;
    a->running = (e == CORRIENDO);
    a->completed = (e == TERMINADA);
    a->failed = (e == FALLIDA);
    a->aborted = (e == ABORTADA);
    if (e == TERMINADA) P.terminadas++;
    if (e == FALLIDA) P.fallidas++;
    if (e == ABORTADA) P.abortadas++;
}

static void encolar(int i) {
    P.estado[i] = LISTA;
    P.cola[P.q_rear++] = i;
}

static int escribir_todo(int fd, const char* buf, size_t n) {
    size_t hecho = 0;

    while (hecho < n) {
        ssize_t w = write(fd, buf + hecho, n - hecho);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        hecho += (size_t)w;
    }
    return 0;
}

static int leer_mensaje(int fd, char* buf, size_t cap) {
    size_t total = 0;

    while (total + 1 < cap) {
        ssize_t r = read(fd, buf + total, cap - 1 - total);
        if (r < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (r == 0) break;
        total += (size_t)r;
    }
    buf[total] = '\0';
    while (total > 0 && (buf[total - 1] == '\n' || buf[total - 1] == '\r')) {
        buf[--total] = '\0';
    }
    return (int)total;
}

static void on_sigint(int s) {
    (void)s;
    g_sigint = 1;
}

static void on_sigchld(int s) {
    (void)s;
}

static void configurar_senales(void) {
    sigset_t bloqueo;
    struct sigaction sa;

    sigemptyset(&bloqueo);
    sigaddset(&bloqueo, SIGCHLD);
    sigaddset(&bloqueo, SIGINT);
    sigprocmask(SIG_BLOCK, &bloqueo, &g_mask_orig);

    memset(&sa, 0, sizeof sa);
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = on_sigint;
    sigaction(SIGINT, &sa, NULL);

    sa.sa_handler = on_sigchld;
    sa.sa_flags = SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);

    sa.sa_handler = SIG_IGN;
    sa.sa_flags = 0;
    sigaction(SIGPIPE, &sa, NULL);
}

static bool hay_sigint(void) {
    sigset_t pendientes;

    if (g_sigint) return true;
    sigpending(&pendientes);
    return sigismember(&pendientes, SIGINT) == 1;
}

static void abortar_rama(int f) {
    Activity* A = P.dag->array;
    int sp = 0;

    for (int k = 0; k < A[f].dep_count; k++) {
        int d = A[f].dependents[k];
        if (P.estado[d] == PENDIENTE) {
            marcar(d, ABORTADA);
            P.pila[sp++] = d;
        }
    }
    while (sp > 0) {
        int x = P.pila[--sp];
        log_linea("[ABORTADA] %s %s (por la falla de %s)\n", A[x].id, A[x].name, A[f].id);
        for (int k = 0; k < A[x].dep_count; k++) {
            int d = A[x].dependents[k];
            if (P.estado[d] == PENDIENTE) {
                marcar(d, ABORTADA);
                P.pila[sp++] = d;
            }
        }
    }
}

static void finalizar_ok(int i, const char* msg) {
    Activity* A = P.dag->array;

    strncpy(A[i].output_msg, msg, sizeof A[i].output_msg - 1);
    A[i].output_msg[sizeof A[i].output_msg - 1] = '\0';
    marcar(i, TERMINADA);
    log_linea("[TERMINO]  %s %s (mensaje: %s)\n", A[i].id, A[i].name, A[i].output_msg);

    for (int k = 0; k < A[i].dep_count; k++) {
        int d = A[i].dependents[k];
        A[d].in_degree--;
        if (A[d].in_degree == 0 && P.estado[d] == PENDIENTE) encolar(d);
    }
}

static void finalizar_fallo(int i, const char* motivo) {
    Activity* A = P.dag->array;

    marcar(i, FALLIDA);
    log_linea("[FALLO]    %s %s (%s)\n", A[i].id, A[i].name, motivo);
    abortar_rama(i);
}

static void liberar_slot(int s) {
    close(P.slots[s].fd_up);
    P.slots[s].task_idx = -1;
    P.slots[s].fd_up = -1;
    P.libres[P.n_libres++] = s;
    P.activos--;
    P.sin_recursos = false;
}

static int id_en_lista(const char* lista, const char* id) {
    size_t largo = strlen(id);
    const char* p = lista;

    if (!lista) return 0;
    while (*p) {
        const char* ini;
        const char* fin;

        while (*p == ' ' || *p == ',') p++;
        ini = p;
        while (*p && *p != ',') p++;
        fin = p;
        while (fin > ini && fin[-1] == ' ') fin--;
        if ((size_t)(fin - ini) == largo && strncmp(ini, id, largo) == 0) return 1;
    }
    return 0;
}

static _Noreturn void hijo(const Activity* a, int up_r, int up_w, int down_r, int down_w) {
    struct sigaction sa;
    char buf[4096];
    char msg[MAX_MSG];
    struct timespec t;
    int insumos = 0;
    int len;
    memset(&sa, 0, sizeof sa);
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = SIG_IGN;
    sigaction(SIGINT, &sa, NULL);
    sa.sa_handler = SIG_DFL;
    sigaction(SIGCHLD, &sa, NULL);
    sigprocmask(SIG_SETMASK, &g_mask_orig, NULL);
    close(up_r);
    close(down_w);
    for (int s = 0; s < P.n_slots; s++) {
        if (P.slots[s].task_idx >= 0) close(P.slots[s].fd_up);
    }
    for (;;) {
        ssize_t r = read(down_r, buf, sizeof buf);
        if (r < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (r == 0) break;
        for (ssize_t k = 0; k < r; k++) {
            if (buf[k] == '\n') insumos++;
        }
    }
    close(down_r);
    t.tv_sec = a->duration_ms / 1000;
    t.tv_nsec = (long)(a->duration_ms % 1000) * 1000000L;
    while (nanosleep(&t, &t) < 0 && errno == EINTR) {
    }

    if (id_en_lista(getenv("PLANIFICADOR_MATAR"), a->id)) {
        kill(getpid(), SIGKILL);
        _exit(4);
    }
    if (id_en_lista(getenv("PLANIFICADOR_FALLAR"), a->id)) _exit(3);

    len = snprintf(msg, sizeof msg, "OK %s %s insumos=%d\n", a->id, a->name, insumos);
    if (len >= (int)sizeof msg) {
        len = (int)sizeof msg - 1;
        msg[len - 1] = '\n';
        msg[len] = '\0';
    }
    escribir_todo(up_w, msg, (size_t)len);
    close(up_w);
    _exit(0);
}

static int lanzar(int i) {
    Activity* A = P.dag->array;
    int up[2] = {-1, -1};
    int down[2] = {-1, -1};
    int err = 0;
    pid_t pid = -1;
    int s;

    if (pipe(up) < 0) {
        err = errno;
    } else if (pipe(down) < 0) {
        err = errno;
        close(up[0]);
        close(up[1]);
    }
    if (err == 0) {
        fflush(stdout);
        pid = fork();
        if (pid < 0) {
            err = errno;
            close(up[0]);
            close(up[1]);
            close(down[0]);
            close(down[1]);
        } else if (pid == 0) {
            hijo(&A[i], up[0], up[1], down[0], down[1]);
        }
    }
    if (err != 0) {
        char motivo[128];

        if (P.activos > 0) {
            P.sin_recursos = true;
            return 1;
        }
        snprintf(motivo, sizeof motivo, "no se pudo crear el proceso: %s", strerror(err));
        finalizar_fallo(i, motivo);
        return 0;
    }

    close(up[1]);
    close(down[0]);
    s = P.libres[--P.n_libres];
    P.slots[s].pid = pid;
    P.slots[s].task_idx = i;
    P.slots[s].fd_up = up[0];
    P.activos++;
    marcar(i, CORRIENDO);
    log_linea("[INICIO]   %s %s (pid %d)\n", A[i].id, A[i].name, (int)pid);

    for (int k = P.dep_off[i]; k < P.dep_off[i + 1]; k++) {
        const char* m = A[P.dep_idx[k]].output_msg;
        char linea[MAX_MSG + 2];
        int n = snprintf(linea, sizeof linea, "%s\n", m);

        if (n >= (int)sizeof linea) n = (int)sizeof linea - 1;
        if (escribir_todo(down[1], linea, (size_t)n) < 0) break; 
    }
    close(down[1]);
    return 0;
}

static void procesar_hijo(pid_t pid, int status) {
    int s = -1;
    int i;
    int n;
    char msg[MAX_MSG];
    char motivo[128];

    for (int k = 0; k < P.n_slots; k++) {
        if (P.slots[k].task_idx >= 0 && P.slots[k].pid == pid) {
            s = k;
            break;
        }
    }
    if (s < 0) return;
    i = P.slots[s].task_idx;

    n = leer_mensaje(P.slots[s].fd_up, msg, sizeof msg);
    liberar_slot(s);

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        if (n > 0) {
            finalizar_ok(i, msg);
        } else {
            finalizar_fallo(i, "salio sin dejar mensaje");
        }
    } else if (WIFEXITED(status)) {
        snprintf(motivo, sizeof motivo, "salio con codigo %d", WEXITSTATUS(status));
        finalizar_fallo(i, motivo);
    } else if (WIFSIGNALED(status)) {
        snprintf(motivo, sizeof motivo, "murio por senal %d (%s)", WTERMSIG(status),
                 strsignal(WTERMSIG(status)));
        finalizar_fallo(i, motivo);
    } else {
        finalizar_fallo(i, "termino de forma inesperada");
    }
}

static void cosechar(void) {
    for (;;) {
        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG);

        if (pid > 0) {
            procesar_hijo(pid, status);
        } else if (pid < 0 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
}

static void abortar_todo(void) {
    Activity* A = P.dag->array;

    log_linea("[SEREMI]   Inspeccion detectada (SIGINT): abortando todas las actividades\n");

    cosechar();

    for (int s = 0; s < P.n_slots; s++) {
        int i = P.slots[s].task_idx;
        if (i < 0) continue;
        kill(P.slots[s].pid, SIGKILL);
        log_linea("[ABORTADA] %s %s (pid %d, en ejecucion)\n", A[i].id, A[i].name,
                  (int)P.slots[s].pid);
        marcar(i, ABORTADA);
    }

    for (;;) {
        int status;
        pid_t pid = waitpid(-1, &status, 0);

        if (pid > 0) {
            for (int s = 0; s < P.n_slots; s++) {
                if (P.slots[s].task_idx >= 0 && P.slots[s].pid == pid) {
                    liberar_slot(s);
                    break;
                }
            }
        } else if (pid < 0 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }

    for (int i = 0; i < P.dag->count; i++) {
        if (P.estado[i] == PENDIENTE || P.estado[i] == LISTA) {
            marcar(i, ABORTADA);
            log_linea("[ABORTADA] %s %s (pendiente)\n", A[i].id, A[i].name);
        }
    }
}

static int ajustar_k(int k, int n) {
    struct rlimit rl;
    long limite = 1000;

    if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
        long cur;

        if (rl.rlim_cur < rl.rlim_max) {
            rlim_t nuevo = rl.rlim_max;
            if (nuevo == RLIM_INFINITY || nuevo > (rlim_t)(1 << 20)) nuevo = (rlim_t)(1 << 20);
            rl.rlim_cur = nuevo;
            setrlimit(RLIMIT_NOFILE, &rl);
            getrlimit(RLIMIT_NOFILE, &rl);
        }
        cur = (rl.rlim_cur == RLIM_INFINITY || rl.rlim_cur > (rlim_t)(1 << 20))
                  ? (1 << 20)
                  : (long)rl.rlim_cur;
        limite = cur - RESERVA_FDS;
        if (limite < 1) limite = 1;
    }
    if (k > limite) {
        fprintf(stderr, "[PLANIFICADOR] Aviso: K = %d supera el limite de descriptores; se usa K = %ld\n",
                k, limite);
        k = (int)limite;
    }
    if (n > 0 && k > n) k = n; 
    return k;
}

int main(int argc, char* argv[]) {
    char* fin = NULL;
    long k_pedido;
    int K;
    int n;
    int codigo = 0;
    RawPlan* raw_plan;
    char err[512] = "";

    if (argc < 3) {
        fprintf(stderr, "Uso: %s <archivo_plan.txt> <K_concurrencia>\n", argv[0]);
        return 1;
    }
    k_pedido = strtol(argv[2], &fin, 10);
    if (*argv[2] == '\0' || *fin != '\0' || k_pedido <= 0 || k_pedido > INT_MAX) {
        fprintf(stderr, "Error: K debe ser un entero mayor a 0.\n");
        return 1;
    }

    setvbuf(stdout, NULL, _IOLBF, 0);

    configurar_senales();

    raw_plan = parser_parse_file(argv[1]);
    if (!raw_plan) return 1;

    memset(&P, 0, sizeof P);
    P.dag = dag_build(raw_plan, err, sizeof err);
    parser_free_plan(raw_plan);
    if (!P.dag) {
        fprintf(stderr, "Error en el plan: %s\n", err);
        return 1;
    }
    n = P.dag->count;
    if (n == 0) {
        printf("[PLANIFICADOR] El plan no tiene actividades.\n");
        dag_free(P.dag);
        return 0;
    }

    K = ajustar_k((int)k_pedido, n);

    P.estado = calloc((size_t)n, sizeof *P.estado);
    P.dep_off = calloc((size_t)n + 1, sizeof *P.dep_off);
    P.cola = malloc((size_t)n * sizeof *P.cola);
    P.pila = malloc((size_t)n * sizeof *P.pila);
    P.slots = malloc((size_t)K * sizeof *P.slots);
    P.libres = malloc((size_t)K * sizeof *P.libres);
    if (!P.estado || !P.dep_off || !P.cola || !P.pila || !P.slots || !P.libres) {
        fprintf(stderr, "Error: sin memoria.\n");
        codigo = 1;
        goto limpiar;
    }
    P.n_slots = K;
    for (int s = 0; s < K; s++) {
        P.slots[s].pid = 0;
        P.slots[s].task_idx = -1;
        P.slots[s].fd_up = -1;
        P.libres[s] = K - 1 - s;
    }
    P.n_libres = K;

    {
        Activity* A = P.dag->array;
        int* cursor;
        int total;

        for (int u = 0; u < n; u++)
            for (int k = 0; k < A[u].dep_count; k++) P.dep_off[A[u].dependents[k] + 1]++;
        for (int i = 0; i < n; i++) P.dep_off[i + 1] += P.dep_off[i];
        total = P.dep_off[n];
        P.dep_idx = malloc((size_t)(total > 0 ? total : 1) * sizeof *P.dep_idx);
        cursor = malloc((size_t)n * sizeof *cursor);
        if (!P.dep_idx || !cursor) {
            free(cursor);
            fprintf(stderr, "Error: sin memoria.\n");
            codigo = 1;
            goto limpiar;
        }
        memcpy(cursor, P.dep_off, (size_t)n * sizeof *cursor);
        for (int u = 0; u < n; u++)
            for (int k = 0; k < A[u].dep_count; k++) {
                int v = A[u].dependents[k];
                P.dep_idx[cursor[v]++] = u;
            }
        free(cursor);

        for (int i = 0; i < n; i++)
            if (A[i].in_degree == 0) encolar(i);
    }

    log_linea("[PLANIFICADOR] Iniciando simulacion con K = %d (%d actividades)\n", K, n);

    for (;;) {
        if (hay_sigint()) {
            abortar_todo();
            codigo = 130;
            break;
        }

        while (P.q_front < P.q_rear && P.activos < K && !P.sin_recursos) {
            int i = P.cola[P.q_front++];

            if (lanzar(i) == 1) {
                P.cola[--P.q_front] = i; 
                break;
            }
            if (hay_sigint()) break;
        }
        if (hay_sigint()) continue;

        cosechar();

        if (P.terminadas + P.fallidas + P.abortadas == n) break;
        if (P.q_front < P.q_rear && P.activos < K && !P.sin_recursos) continue;

        if (P.activos == 0) {
            fprintf(stderr, "Error interno: el plan no puede avanzar.\n");
            for (int i = 0; i < n; i++) {
                if (P.estado[i] == PENDIENTE || P.estado[i] == LISTA) marcar(i, ABORTADA);
            }
            codigo = 1;
            break;
        }

        sigsuspend(&g_mask_orig);
    }

    log_linea("[RESUMEN]  actividades=%d terminadas=%d fallidas=%d abortadas=%d\n", n,
              P.terminadas, P.fallidas, P.abortadas);

limpiar:
    free(P.estado);
    free(P.dep_off);
    free(P.dep_idx);
    free(P.cola);
    free(P.pila);
    free(P.slots);
    free(P.libres);
    dag_free(P.dag);
    return codigo;
}
