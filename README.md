# Planificador Dieciochero - Tarea 1 Sistemas Operativos

Simulador y planificador de actividades basado en un Grafo Acíclico Dirigido (DAG), implementado estrictamente con multiprocesamiento en C.

## Modo de Uso

### Compilación
Para compilar el proyecto utilizando las banderas estrictas de evaluación (`-Wall -Wextra -std=c17 -lpthread`), ejecute:
`make`

### Ejecución
Para iniciar el simulador, utilice la siguiente sintaxis:
`./planificador <archivo_plan.txt> <K>`

Donde `<archivo_plan.txt>` es la ruta al archivo de texto con el plan de actividades, y `<K>` es el límite numérico de concurrencia máxima permitida. Ejemplo:
`./planificador plan_estres_aleatorio.txt 150`

### Limpieza
Para eliminar los archivos compilados (`.o` y el ejecutable):
`make clean`

## Funciones Implementadas

El proyecto está dividido en tres módulos principales para garantizar una arquitectura limpia:

1. **`parser.c` (Parseo Robusto):** 
   Se encarga de la lectura del archivo de texto. Limpia los espacios, procesa los IDs alfanuméricos y las listas de dependencias. Si una actividad no especifica su tiempo de ejecución, el parser le asigna un tiempo aleatorio entre 100ms y 5000ms.
2. **`dag.c` (Modelado del Grafo):** 
   Toma la información en bruto del parser y la modela matemáticamente en un Grafo Acíclico Dirigido (DAG). Pre-calcula arreglos estáticos de dependencias para un acceso rápido en tiempo de ejecución (`O(1)`). Implementa el Algoritmo de Kahn para el ordenamiento topológico, lo que permite detectar ciclos, IDs repetidos y dependencias "fantasmas" de forma segura antes de la ejecución.
3. **`main.c` (Planificador Multiproceso):** 
   Orquesta la ejecución. Crea los procesos hijos (`fork`), administra las tuberías (`pipes`), controla el límite de concurrencia `K` y maneja la intercepción de señales del sistema operativo (`SIGCHLD`, `SIGINT`, `SIGPIPE`).

### Justificaciones de Decisiones de Diseño

Para asegurar la máxima eficiencia y el cumplimiento estricto de la rúbrica, el diseño del planificador se fundamenta en las siguientes decisiones técnicas:

### 1. Multiprocesamiento Puro (Prohibición de Hilos)
Acorde a las restricciones del enunciado, se evitó por completo el uso de la biblioteca `pthread` para la creación de hilos o sincronización (mutex/senáforos). Cada actividad se aísla en su propio proceso utilizando la llamada al sistema `fork()`, garantizando protección de memoria entre actividades.

### 2. Control de Concurrencia sin *Busy-Waiting*
Para respetar el límite de concurrencia `K` sin malgastar ciclos de CPU iterando en bucles vacíos (espera activa), se implementó un mecanismo reactivo basado en señales. El proceso padre utiliza `sigsuspend()` para suspender su ejecución (consumo 0% de CPU). El sistema operativo solo lo despierta cuando recibe la señal `SIGCHLD`, indicando que un proceso hijo terminó y liberó un cupo en la tabla de procesos activos. 

### 3. Aislamiento de Errores sin Pila de Llamadas Excesiva
Para cumplir con el aislamiento de errores (abortar solo la rama descendiente de un nodo fallido), se diseño una propagación iterativa apoyada en una estructura de Pila explícita, en lugar de utilizar funciones recursivas. Esta decisión fue tomada específicamente para evitar desbordamientos de pila (*Stack Overflow*) durante las pruebas de estrés masivo de la Seremi, las cuales evalúan grafos de hasta 10,000 nodos.

### 4. Uso de Pipes en Cascada para Paso de Mensajes
La comunicación entre procesos se realiza exclusivamente mediante descriptores de archivo anónimos (`pipes`). Se implementó un esquema direccional (`up` y `down`) y altamente encapsulado: el proceso padre lee el mensaje de término del hijo, lo almacena temporalmente, y lo inyecta a través del pipe de bajada hacia los procesos hijos dependientes justo en el momento del `fork`. Esto asegura que los procesos no compartan memoria ni corran riesgos de *race conditions*.

### 5. Gestión Dinámica de Descriptores (`RLIMIT_NOFILE`)
Para garantizar la resiliencia del planificador en pruebas de estrés (carga extrema), el programa invoca `getrlimit` y `setrlimit` en su inicio. Esto expande temporalmente el límite máximo de descriptores de archivos que el sistema operativo le permite abrir al proceso, evitando caídas abruptas por falta de recursos (`EMFILE`) al abrir múltiples *pipes* simultáneos.
