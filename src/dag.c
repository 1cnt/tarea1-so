#include "dag.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Función auxiliar para buscar en qué posición de la lista está un ID
static int find_activity_index(RawPlan* plan, const char* id) {
    for (int i = 0; i < plan->count; i++) {
        if (strcmp(plan->items[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}

DAG* dag_build(RawPlan* plan) {
    if (!plan) return NULL;

    DAG* dag = (DAG*)malloc(sizeof(DAG));
    dag->count = plan->count;
    dag->array = (Activity*)malloc(sizeof(Activity) * dag->count);

    // 1. Traspasar los datos y preparar contadores
    for (int i = 0; i < dag->count; i++) {
        Activity* act = &dag->array[i];
        strncpy(act->id, plan->items[i].id, MAX_ID_LEN);
        strncpy(act->name, plan->items[i].name, MAX_NAME_LEN);
        act->duration_ms = plan->items[i].duration_ms;
        
        act->in_degree = plan->items[i].raw_dep_count;
        
        act->dep_capacity = 4;
        act->dep_count = 0;
        act->dependents = (int*)malloc(sizeof(int) * act->dep_capacity);
        
        act->running = false;
        act->completed = false;
        act->failed = false;
        act->aborted = false;
        memset(act->output_msg, 0, sizeof(act->output_msg));
    }

    // 2. Conectar las dependencias (quién desbloquea a quién)
    for (int i = 0; i < dag->count; i++) {
        for (int d = 0; d < plan->items[i].raw_dep_count; d++) {
            const char* dep_id = plan->items[i].raw_dependencies[d];
            int parent_idx = find_activity_index(plan, dep_id);
            
            if (parent_idx != -1) {
                Activity* parent = &dag->array[parent_idx];
                // Agregar la actividad 'i' a la lista de dependientes del 'parent'
                if (parent->dep_count >= parent->dep_capacity) {
                    parent->dep_capacity *= 2;
                    parent->dependents = (int*)realloc(parent->dependents, sizeof(int) * parent->dep_capacity);
                }
                parent->dependents[parent->dep_count++] = i;
            } else {
                // Si la dependencia no existe, descontamos el in_degree para que no se congele
                dag->array[i].in_degree--;
            }
        }
    }
    return dag;
}

void dag_free(DAG* dag) {
    if (!dag) return;
    for (int i = 0; i < dag->count; i++) {
        free(dag->array[i].dependents);
    }
    free(dag->array);
    free(dag);
}