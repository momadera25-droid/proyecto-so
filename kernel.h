#ifndef KERNEL_H
#define KERNEL_H

#include <pthread.h>

/* ============================================================================
 * PROYECTO SISTEMAS OPERATIVOS - FASE I
 * SIMULADOR DE KERNEL Y GESTOR DE PROCESOS (SHELL & SCHEDULING ENGINE)
 * ============================================================================
 * Este archivo cabecera define las estructuras de datos fundamentales del 
 * sistema operativo simulado, incluyendo el Bloque de Control de Proceso (PCB),
 * los estados del ciclo de vida, las políticas de planificación y las variables
 * globales sincronizadas mediante hilos POSIX (pthreads).
 * ============================================================================
 */

// Ciclo de vida de los procesos en el simulador
typedef enum {
    ESTADO_NEW,         // Proceso recién instanciado
    ESTADO_READY,       // Proceso en cola esperando turno en CPU
    ESTADO_RUNNING,     // Proceso actualmente ejecutándose en la CPU simulada
    ESTADO_WAITING,     // Proceso en espera o bloqueado
    ESTADO_TERMINATED   // Proceso que culminó exitosamente toda su ráfaga
} EstadoProceso;

// Algoritmos de planificación admitidos en el simulador
typedef enum {
    PLANIF_RR,          // Round Robin (con quantum)
    PLANIF_SRTF         // Shortest Remaining Time First (Apropiativo)
} AlgoritmoSched;

// Estructura del Process Control Block (PCB)
typedef struct PCB {
    int pid;                    // Identificador único autoincremental
    char nombre[50];            // Nombre simbólico del proceso (ej. Proceso_A)
    int burst_time;             // Ráfaga inicial de CPU asignada (duración total)
    int tiempo_restante;        // Ráfaga pendiente de CPU por consumir
    int prioridad;              // Nivel de prioridad (rango 1 a 5, 3 por defecto)
    EstadoProceso estado;       // Estado actual del ciclo de vida
    
    // Métricas del Sistema Operativo para evaluación matemática
    int arrival_time;           // Momento exacto de llegada al sistema (clock tick)
    int start_time;             // Primer tick en que la CPU le otorgó ejecución (-1 si aún no)
    int completion_time;        // Tick en que culminó la ráfaga (-1 si no ha finalizado)
    int waiting_time;           // Tiempo total acumulado en espera dentro de READY
    int turnaround_time;        // Tiempo total de retorno: Completion Time - Arrival Time
    int response_time;          // Tiempo de respuesta inicial: Start Time - Arrival Time
    
    struct PCB* siguiente;      // Puntero para implementar listas/colas enlazadas
} PCB;

// Nodo para el historial del Diagrama de Gantt
typedef struct SegmentoGantt {
    char nombre[50];            // Nombre del proceso o "[IDLE]"
    int tick_inicio;            // Instante de reloj inicial del segmento
    int tick_fin;               // Instante de reloj final del segmento
    struct SegmentoGantt* siguiente;
} SegmentoGantt;

/* ============================================================================
 * DECLARACIÓN DE VARIABLES GLOBALES COMPARTIDAS ENTRE HILOS
 * (Protegidas obligatoriamente mediante sistema_mutex)
 * ============================================================================
 */
extern pthread_mutex_t sistema_mutex; // Cerrojo de sincronización entre CLI y Scheduler

// Colas de procesos
extern PCB* ready_queue;        // Cola de procesos listos para ejecución
extern PCB* running_proc;       // Proceso que actualmente posee la CPU simulada (NULL si IDLE)
extern PCB* terminated_list;    // Lista histórica de procesos culminados
extern PCB* batch_queue;        // Cola de trabajos en lote pendientes por llegar (Arrival Time)

// Historial acumulado para el Diagrama de Gantt
extern SegmentoGantt* gantt_head;
extern SegmentoGantt* gantt_tail;

// Variables de estado y simulación
extern int ultimo_pid;          // Contador autoincremental para los PID
extern int clock_ticks;         // Reloj simulado discreto del sistema (+1 por tick)
extern int cpu_busy_ticks;      // Cantidad de ticks en los que la CPU ejecutó trabajo útil
extern int speed_ms;            // Velocidad del reloj: duración del retardo real (en ms)
extern AlgoritmoSched sched_algorithm; // Algoritmo activo (SCHED_RR o SCHED_SRTF)
extern int rr_quantum;          // Quantum actual configurado para Round Robin (defecto: 2)
extern int sim_running;         // Bandera: 1 = En ejecución (start), 0 = Pausado (pause)
extern int sim_exit;            // Bandera: 1 = Señal de terminación limpia para los hilos

/* ============================================================================
 * PROTOTIPOS DE FUNCIONES DEL SISTEMA
 * ============================================================================
 */
// Funciones del ciclo de vida del Kernel
void kernel_init(void);
void kernel_destroy(void);

// Funciones del Hilo Motor de Scheduling (CPU)
void* cpu_scheduler_thread_func(void* arg);
void registrar_gantt(const char* nombre, int inicio, int fin);

// Funciones de gestión y búsqueda de procesos
PCB* crear_pcb(const char* nombre, int burst, int prioridad, int arrival);
void encolar_ready(PCB* pcb);
void encolar_batch(PCB* pcb);
PCB* buscar_pcb_global(int pid);
int cargar_archivo_batch(const char* ruta_archivo);

// Hilo CLI
void* cli_thread_func(void* arg);

#endif // KERNEL_H
