# Tarea 1: Planificador Dieciochero
**Ramo:** Sistemas Operativos  

## Breve Descripción del Proyecto
El proyecto aquí presente es un simulador y planificador de actividades asociadas a las fiesta patrias modelado mediante un Grafo Acíclico Dirigido (DAG). Tiene por objetivo orquestar un conjunto de tareas interdependientes leídas desde un archivo de texto, respetando un límite estricto de concurrencia ($K$) y utilizando exclusivamente llamadas al sistema POSIX (`fork`, `pipe`, `waitpid`, `sigaction`) SIN USAR HILOS (`threads`).

## Compilación
El proyecto utiliza un `Makefile` configurado con banderas estrictas para asegurar el estándar C17. Para compilar, abra una terminal en la raíz del proyecto y ejecute:

```bash
make