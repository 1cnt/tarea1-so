#ifndef PARSER_H
#define PARSER_H

#include <stdbool.h>

#define MAX_ID_LEN 64
#define MAX_NAME_LEN 128

// Esta estructura guarda exactamente una línea de plan.txt
typedef struct {
    char id[MAX_ID_LEN];
    char name[MAX_NAME_LEN];
    int duration_ms;                 
    char** raw_dependencies;         // Arreglo de textos con los IDs de dependencias
    int raw_dep_count;
} RawActivity;

// Esta estructura guarda la lista completa de todas las actividades
typedef struct {
    RawActivity* items;
    int count;       // Cuántas llevamos
    int capacity;    // Cuánto espacio hay reservado
} RawPlan;

// Firmas de las funciones que usaremos desde otros archivos
RawPlan* parser_parse_file(const char* filepath);
void parser_free_plan(RawPlan* plan);

#endif // PARSER_H