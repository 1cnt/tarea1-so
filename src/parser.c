#define _POSIX_C_SOURCE 200809L
#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

static char* trim_whitespace(char* str) {
    if (!str) return NULL;
    while (isspace((unsigned char)*str)) str++;
    if (*str == '\0') return str;
    char* end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

static void raw_plan_init(RawPlan* plan) {
    plan->capacity = 32;
    plan->count = 0;
    plan->items = (RawActivity*)malloc(sizeof(RawActivity) * plan->capacity);
}

void parser_free_plan(RawPlan* plan) {
    if (!plan) return;
    for (int i = 0; i < plan->count; i++) {
        for (int d = 0; d < plan->items[i].raw_dep_count; d++) {
            free(plan->items[i].raw_dependencies[d]);
        }
        free(plan->items[i].raw_dependencies);
    }
    free(plan->items);
    free(plan);
}

RawPlan* parser_parse_file(const char* filepath) {
    FILE* file = fopen(filepath, "r");
    if (!file) {
        perror("Error al abrir el archivo");
        return NULL;
    }

    RawPlan* plan = (RawPlan*)malloc(sizeof(RawPlan));
    raw_plan_init(plan);

    srand((unsigned int)time(NULL));

    char line_buffer[2048]; 

    while (fgets(line_buffer, sizeof(line_buffer), file)) {
        char* trimmed_line = trim_whitespace(line_buffer);
        if (strlen(trimmed_line) == 0) continue; 

        if (plan->count >= plan->capacity) {
            plan->capacity *= 2;
            plan->items = (RawActivity*)realloc(plan->items, sizeof(RawActivity) * plan->capacity);
        }

        RawActivity* current = &plan->items[plan->count];
        memset(current, 0, sizeof(RawActivity));

        char* token_id = strtok(trimmed_line, ":");
        char* token_name = strtok(NULL, ":");
        char* token_duration = strtok(NULL, ":");
        char* token_deps = strtok(NULL, ":"); 

        if (!token_id || !token_name) continue; 

        strncpy(current->id, trim_whitespace(token_id), MAX_ID_LEN - 1);
        strncpy(current->name, trim_whitespace(token_name), MAX_NAME_LEN - 1);

        char* dur_clean = token_duration ? trim_whitespace(token_duration) : NULL;
        if (dur_clean && strlen(dur_clean) > 0) {
            current->duration_ms = atoi(dur_clean);
        } else {
            current->duration_ms = 100 + (rand() % (5000 - 100 + 1));
        }

        current->raw_dep_count = 0;
        current->raw_dependencies = NULL;

        if (token_deps) {
            char* deps_clean = trim_whitespace(token_deps);
            if (strlen(deps_clean) > 0) {
                char* dep_tok = strtok(deps_clean, ",");
                while (dep_tok) {
                    char* dep_str = trim_whitespace(dep_tok);
                    if (strlen(dep_str) > 0) {
                        current->raw_dependencies = (char**)realloc(
                            current->raw_dependencies,
                            sizeof(char*) * (current->raw_dep_count + 1)
                        );
                        current->raw_dependencies[current->raw_dep_count] = strdup(dep_str);
                        current->raw_dep_count++;
                    }
                    dep_tok = strtok(NULL, ",");
                }
            }
        }

        plan->count++;
    }

    fclose(file);
    return plan;
}
