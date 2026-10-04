#ifndef DAG_H
#define DAG_H

#include "parser.h"
#include <stdbool.h>

typedef struct {
    char id[MAX_ID_LEN];
    char name[MAX_NAME_LEN];
    int duration_ms;

    int in_degree;           // Contador: Cuántas dependencias le faltan para estar lista
    int* dependents;         // Arreglo de posiciones de las actividades que dependen de esta
    int dep_count;
    int dep_capacity;

    bool running;
    bool completed;
    bool failed;
    bool aborted;
    char output_msg[256];    // Para guardar el mensaje que mandará por el pipe
} Activity;

typedef struct {
    Activity* array;
    int count;
} DAG;

// Toma los datos crudos del parser y construye el grafo conectado
DAG* dag_build(RawPlan* plan);
void dag_free(DAG* dag);

#endif // DAG_H