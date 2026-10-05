#ifndef PARSER_H
#define PARSER_H

#include <stdbool.h>

#define MAX_ID_LEN 64
#define MAX_NAME_LEN 128

typedef struct {
    char id[MAX_ID_LEN];
    char name[MAX_NAME_LEN];
    int duration_ms;                 
    char** raw_dependencies;    
    int raw_dep_count;
} RawActivity;

typedef struct {
    RawActivity* items;
    int count;    
    int capacity;  
} RawPlan;


RawPlan* parser_parse_file(const char* filepath);
void parser_free_plan(RawPlan* plan);

#endif 
