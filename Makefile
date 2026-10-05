# ==============================================================================
# Makefile - Tarea 1: Sistemas Operativos (Planificador Dieciochero)
# ==============================================================================

CC = gcc
CFLAGS = -Wall -Wextra -std=c17 -lpthread -O2

TARGET = planificador

SRCS = main.c parser.c dag.c
OBJS = $(SRCS:.c=.o)
HEADERS = parser.h dag.h

GREEN = \033[0;32m
BLUE = \033[0;34m
NC = \033[0m 

.PHONY: all clean run valgrind help

all: $(TARGET)

$(TARGET): $(OBJS)
    @echo "$(BLUE)Linkeando el ejecutable $(TARGET)...$(NC)"
    $(CC) $(CFLAGS) -o $@ $^
    @echo "$(GREEN)¡Compilación exitosa!$(NC)"

%.o: %.c $(HEADERS)
    @echo "Compilando $<..."
    $(CC) $(CFLAGS) -c $< -o $@

clean:
    @echo "$(BLUE)Limpiando archivos de compilación...$(NC)"
    rm -f $(OBJS) $(TARGET)
    @echo "$(GREEN)¡Directorio limpio!$(NC)"

run: $(TARGET)
    ./$(TARGET) ejemplo.txt 2

valgrind: $(TARGET)
    valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./$(TARGET) ejemplo.txt 2

help:
    @echo "$(BLUE)Comandos disponibles en este Makefile:$(NC)"
    @echo "  make          - Compila el proyecto completo y genera el ejecutable '$(TARGET)'"
    @echo "  make clean    - Elimina los archivos .o y el ejecutable"
    @echo "  make run      - Ejecuta el programa de prueba"
    @echo "  make valgrind - Ejecuta el programa con Valgrind"
