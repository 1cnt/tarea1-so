#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include "parser.h"
#include "dag.h"

// Estructura para recordar qué procesos están corriendo actualmente
typedef struct {
    pid_t pid;
    int task_idx;
} ActiveProcess;

int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Uso: %s <archivo_plan.txt> <K_concurrencia>\n", argv[0]);
        return 1;
    }

    int K_limit = atoi(argv[2]);
    if (K_limit <= 0) {
        fprintf(stderr, "Error: K debe ser mayor a 0.\n");
        return 1;
    }

    RawPlan* raw_plan = parser_parse_file(argv[1]);
    if (!raw_plan) return 1;

    DAG* dag = dag_build(raw_plan);

    printf("[PLANIFICADOR] Iniciando simulación con K = %d\n", K_limit);

    // 1. Preparar la "Cola de Listos" (FIFO simple usando un arreglo)
    int* ready_queue = (int*)malloc(sizeof(int) * dag->count);
    int q_front = 0;
    int q_rear = 0;

    // Buscar las tareas iniciales (las que no dependen de nadie) y meterlas a la cola
    for (int i = 0; i < dag->count; i++) {
        if (dag->array[i].in_degree == 0) {
            ready_queue[q_rear++] = i;
        }
    }

    // 2. Tabla para registrar procesos activos sin pasarnos de K
    ActiveProcess* active_table = (ActiveProcess*)calloc(K_limit, sizeof(ActiveProcess));
    int active_count = 0;
    int completed_count = 0;

    // 3. EL CORAZÓN DEL PLANIFICADOR
    while (completed_count < dag->count) {

        // FASE A: Lanzar hijos mientras haya tareas listas Y no superemos el límite K
        while (q_front < q_rear && active_count < K_limit) {
            int task_idx = ready_queue[q_front++];
            Activity* act = &dag->array[task_idx];

            pid_t pid = fork(); // ¡Nace un proceso!
            if (pid < 0) {
                perror("Error en fork");
                exit(1);
            }

            if (pid == 0) {
                // ================= PROCESO HIJO =================
                printf("  -> Iniciando [%s] %s (%d ms)\n", act->id, act->name, act->duration_ms);
                
                // Dormir para simular que está trabajando en la actividad
                usleep((useconds_t)act->duration_ms * 1000);
                
                exit(0); // Termina sin errores
            } else {
                // ================= PROCESO PADRE =================
                act->running = true;
                // Buscar un hueco libre en la tabla para registrar al hijo
                for (int s = 0; s < K_limit; s++) {
                    if (active_table[s].pid == 0) {
                        active_table[s].pid = pid;
                        active_table[s].task_idx = task_idx;
                        active_count++;
                        break;
                    }
                }
            }
        }

        // FASE B: Esperar (Cero Busy-Waiting). El padre se bloquea hasta que un hijo termine.
        if (active_count > 0) {
            int status;
            pid_t finished_pid = waitpid(-1, &status, 0); // Bloqueo real, no gasta CPU

            if (finished_pid > 0) {
                // Descubrir qué tarea fue la que terminó
                int finished_idx = -1;
                for (int s = 0; s < K_limit; s++) {
                    if (active_table[s].pid == finished_pid) {
                        finished_idx = active_table[s].task_idx;
                        active_table[s].pid = 0; // Liberar el hueco en la tabla
                        active_count--;
                        break;
                    }
                }

                if (finished_idx != -1) {
                    Activity* act = &dag->array[finished_idx];
                    act->running = false;
                    act->completed = true;
                    completed_count++;

                    printf("  <- Completada [%s] %s\n", act->id, act->name);

                    // Despertar a las tareas que dependían de esta
                    for (int d = 0; d < act->dep_count; d++) {
                        int dep_idx = act->dependents[d];
                        Activity* dep = &dag->array[dep_idx];
                        
                        dep->in_degree--; // Le falta una dependencia menos
                        
                        // Si ya no le faltan dependencias, se va a la cola de listos
                        if (dep->in_degree == 0) {
                            ready_queue[q_rear++] = dep_idx;
                        }
                    }
                }
            }
        }
    }

    printf("\n[PLANIFICADOR] Todas las %d actividades finalizaron con éxito.\n", completed_count);

    // 4. Limpieza impecable
    free(active_table);
    free(ready_queue);
    dag_free(dag);
    parser_free_plan(raw_plan);

    return 0;
}