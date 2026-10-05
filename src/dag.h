#ifndef DAG_H
#define DAG_H

#include "parser.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char id[MAX_ID_LEN];
    char name[MAX_NAME_LEN];
    int duration_ms;

    int in_degree;           
    int* dependents;       
    int dep_count;          
    int dep_capacity;

    bool running;
    bool completed;
    bool failed;
    bool aborted;
    char output_msg[256];    
} Activity;

typedef struct {
    Activity* array;
    int count;
} DAG;

DAG* dag_build(RawPlan* plan, char* err, size_t err_tam);
void dag_free(DAG* dag);

#endif
