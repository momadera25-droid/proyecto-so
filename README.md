# Simulador de Kernel y Gestor de Procesos en Consola
## Sistemas Operativos - Fase I (UCAB)

Simulador de núcleo ejecutor y Shell interactiva de procesos con soporte multihilo mediante **POSIX Threads (`pthread`)** y sincronización mediante cerrojos de exclusión mutua (**`pthread_mutex_t`**).

---

### Arquitectura de Concurrencia
El sistema está estructurado mediante dos hilos obligatorios concurrentes:
1. **Hilo CLI (Interfaz de Consola):** Atiende comandos del usuario, valida sintaxis y tipos sin bloquearse ni colapsar.
2. **Hilo Motor de CPU (Scheduler):** Simula el avance del reloj discreto (`clock_ticks`), ejecuta las políticas de planificación (**Round Robin** y **SRTF**), aplica desapropiación inmediata, calcula métricas matemáticas de rendimiento y genera el historial para el Diagrama de Gantt.

---

### Requisitos del Sistema y Compilación
* Compilador **GCC (C11)** con soporte para hilos POSIX (`-pthread`).
* Se incluye un archivo `Makefile` automatizado.

#### Compilar el proyecto:
```bash
mingw32-make
```
*(O alternativamente: `gcc -Wall -Wextra -std=c11 -O2 Principal.c Interfaz_CL.c kernel_core.c cpu_scheduler.c -o simulador_kernel.exe`)*

#### Ejecutar el simulador:
```bash
./simulador_kernel.exe
```

---

### Lista de Comandos Disponibles en la CLI
* `create <nombre> <burst> [prioridad]`: Crea un nuevo proceso (prioridad 1 a 5, por defecto 3).
* `load <archivo>`: Carga en lote un archivo de traza de procesos respetando sus tiempos de llegada (*Arrival Time*).
* `ps`: Despliega una tabla con el estado de todos los procesos (PID, Nombre, Estado, Burst Restante, Prioridad).
* `pcb <PID>`: Muestra el detalle extendido del Bloque de Control de Proceso (Arrival Time, Burst inicial y restante, WT, TAT, RT).
* `set_sched <RR|SRTF> [quantum]`: Establece la política activa del planificador (permite cambio en caliente).
* `set_speed <ms_por_tick>`: Ajusta la velocidad del reloj simulado (50 a 5000 ms, por defecto 1000 ms).
* `start`: Inicia o reanuda la ejecución del motor de scheduling.
* `pause`: Detiene momentáneamente el reloj para inspección estática del sistema.
* `gantt`: Imprime el Diagrama de Gantt acumulado en formato texto (`[0-2s: P1] -> [2-4s: P2] -> ...`).
* `stats`: Muestra las métricas globales del sistema (Procesos finalizados, Tiempo Promedio de Espera WT, Retorno TAT, Respuesta RT y % de Uso de CPU).
* `help`: Muestra el manual de usuario rápido.
* `exit`: Libera toda la memoria dinámica, detiene los hilos y sale limpiamente.

---

### Entregables Incluidos
* Código fuente modular en C11 (`Principal.c`, `Interfaz_CL.c`, `kernel_core.c`, `cpu_scheduler.c`, `kernel.h`).
* `workload_test.txt`: Archivo de traza para la prueba de carga por lotes.
* `Informe_Tecnico_Fase1_Sistemas_Operativos.pdf`: Informe técnico formal con diagrama de arquitectura, validación de escenarios y anexo obligatorio de uso ético de Inteligencia Artificial.
