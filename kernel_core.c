#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <ctype.h>
#include "kernel.h"

/* ============================================================================
 * MODULO: kernel_core.c
 * RESPONSABILIDAD:
 *   - Inicialización y liberación global de recursos del simulador.
 *   - Administración de memoria para los bloques PCB y nodos de Gantt.
 *   - Implementación de colas y listas del sistema (Ready, Running, Terminated, Batch).
 *   - Parser robusto para el archivo de trazas por lotes (load <archivo>).
 * ============================================================================
 */

// Definición de las variables globales compartidas
pthread_mutex_t sistema_mutex;

PCB* ready_queue = NULL;
PCB* running_proc = NULL;
PCB* terminated_list = NULL;
PCB* batch_queue = NULL;

SegmentoGantt* gantt_head = NULL;
SegmentoGantt* gantt_tail = NULL;

int ultimo_pid = 0;
int clock_ticks = 0;
int cpu_busy_ticks = 0;
int speed_ms = 1000;              // Por defecto: 1000 ms = 1 segundo real
AlgoritmoSched sched_algorithm = PLANIF_RR; // Por defecto: Round Robin
int rr_quantum = 2;               // Quantum por defecto = 2
int sim_running = 0;              // Por defecto en pausa hasta recibir 'start'
int sim_exit = 0;

/* ============================================================================
 * FUNCIÓN: kernel_init
 * Inicializa el mutex y reinicia los contadores y punteros del sistema.
 * ============================================================================
 */
void kernel_init(void) {
    pthread_mutex_init(&sistema_mutex, NULL);
    ready_queue = NULL;
    running_proc = NULL;
    terminated_list = NULL;
    batch_queue = NULL;
    gantt_head = NULL;
    gantt_tail = NULL;
    ultimo_pid = 0;
    clock_ticks = 0;
    cpu_busy_ticks = 0;
    speed_ms = 1000;
    sched_algorithm = PLANIF_RR;
    rr_quantum = 2;
    sim_running = 0;
    sim_exit = 0;
}

// Función auxiliar para liberar una lista enlazada de PCBs
static void liberar_lista_pcb(PCB* cabeza) {
    PCB* actual = cabeza;
    while (actual != NULL) {
        PCB* temp = actual;
        actual = actual->siguiente;
        free(temp);
    }
}

// Función auxiliar para liberar la lista del Diagrama de Gantt
static void liberar_gantt(void) {
    SegmentoGantt* actual = gantt_head;
    while (actual != NULL) {
        SegmentoGantt* temp = actual;
        actual = actual->siguiente;
        free(temp);
    }
    gantt_head = NULL;
    gantt_tail = NULL;
}

/* ============================================================================
 * FUNCIÓN: kernel_destroy
 * Libera toda la memoria dinámica reservada durante la simulación y destruye
 * el mutex de concurrencia.
 * ============================================================================
 */
void kernel_destroy(void) {
    liberar_lista_pcb(ready_queue);
    ready_queue = NULL;

    if (running_proc != NULL) {
        free(running_proc);
        running_proc = NULL;
    }

    liberar_lista_pcb(terminated_list);
    terminated_list = NULL;

    liberar_lista_pcb(batch_queue);
    batch_queue = NULL;

    liberar_gantt();

    pthread_mutex_destroy(&sistema_mutex);
}

/* ============================================================================
 * FUNCIÓN: crear_pcb
 * Asigna memoria dinámica para un nuevo proceso, inicializa sus campos y métricas
 * y le asigna un PID único autoincremental.
 * ============================================================================
 */
PCB* crear_pcb(const char* nombre, int burst, int prioridad, int arrival) {
    PCB* p = (PCB*)malloc(sizeof(PCB));
    if (!p) return NULL;

    p->pid = ++ultimo_pid;
    snprintf(p->nombre, sizeof(p->nombre), "%s", nombre);
    p->burst_time = burst;
    p->tiempo_restante = burst;
    p->prioridad = prioridad;
    p->estado = ESTADO_NEW;
    p->arrival_time = arrival;
    p->start_time = -1;
    p->completion_time = -1;
    p->waiting_time = 0;
    p->turnaround_time = 0;
    p->response_time = -1;
    p->siguiente = NULL;

    return p;
}

/* ============================================================================
 * FUNCIÓN: encolar_ready
 * Inserta un proceso al final de la cola de listos (disciplina FIFO).
 * ============================================================================
 */
void encolar_ready(PCB* pcb) {
    if (!pcb) return;
    pcb->siguiente = NULL;

    if (ready_queue == NULL) {
        ready_queue = pcb;
    } else {
        PCB* actual = ready_queue;
        while (actual->siguiente != NULL) {
            actual = actual->siguiente;
        }
        actual->siguiente = pcb;
    }
}

/* ============================================================================
 * FUNCIÓN: encolar_batch
 * Inserta un proceso en la cola de trabajos por lote ordenado por Arrival Time.
 * ============================================================================
 */
void encolar_batch(PCB* pcb) {
    if (!pcb) return;
    pcb->siguiente = NULL;

    if (batch_queue == NULL || pcb->arrival_time < batch_queue->arrival_time) {
        pcb->siguiente = batch_queue;
        batch_queue = pcb;
        return;
    }

    PCB* actual = batch_queue;
    while (actual->siguiente != NULL && actual->siguiente->arrival_time <= pcb->arrival_time) {
        actual = actual->siguiente;
    }
    pcb->siguiente = actual->siguiente;
    actual->siguiente = pcb;
}

/* ============================================================================
 * FUNCIÓN: buscar_pcb_global
 * Busca un proceso por su PID en cualquiera de los estados del sistema:
 * en ejecución (running_proc), en cola de listos (ready_queue), en terminados
 * (terminated_list) o en lote pendiente (batch_queue).
 * ============================================================================
 */
PCB* buscar_pcb_global(int pid) {
    if (running_proc != NULL && running_proc->pid == pid) {
        return running_proc;
    }

    PCB* cur = ready_queue;
    while (cur != NULL) {
        if (cur->pid == pid) return cur;
        cur = cur->siguiente;
    }

    cur = terminated_list;
    while (cur != NULL) {
        if (cur->pid == pid) return cur;
        cur = cur->siguiente;
    }

    cur = batch_queue;
    while (cur != NULL) {
        if (cur->pid == pid) return cur;
        cur = cur->siguiente;
    }

    return NULL;
}

// Función auxiliar para quitar espacios en blanco a los extremos de una cadena
static char* trim(char* str) {
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;

    char* end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

/* ============================================================================
 * FUNCIÓN: cargar_archivo_batch
 * Modo por lotes: Lee un archivo de traza estructurado e inserta masivamente
 * los procesos en el sistema respetando sus tiempos simulados de llegada.
 * Formato esperado:
 *   # NOMBRE, BURST_TIME, PRIORIDAD, ARRIVAL_TIME
 *   Job_A, 4, 2, 0
 * ============================================================================
 */
int cargar_archivo_batch(const char* ruta_archivo) {
    FILE* f = fopen(ruta_archivo, "r");
    if (!f) {
        return -1; // Error: archivo no existe o sin permisos
    }

    char linea[256];
    int cargados = 0;

    while (fgets(linea, sizeof(linea), f) != NULL) {
        char* l = trim(linea);
        // Ignorar comentarios o líneas en blanco
        if (l[0] == '#' || l[0] == '\0') {
            continue;
        }

        // Parsear: NOMBRE, BURST, PRIORIDAD, ARRIVAL
        char nombre[50];
        int burst = 0, prioridad = 3, arrival = 0;

        char* token = strtok(l, ",");
        if (!token) continue;
        snprintf(nombre, sizeof(nombre), "%s", trim(token));

        token = strtok(NULL, ",");
        if (!token) continue;
        burst = atoi(trim(token));

        token = strtok(NULL, ",");
        if (!token) continue;
        prioridad = atoi(trim(token));

        token = strtok(NULL, ",");
        if (!token) continue;
        arrival = atoi(trim(token));

        if (burst <= 0) continue;
        if (prioridad < 1 || prioridad > 5) prioridad = 3;
        if (arrival < 0) arrival = 0;

        PCB* nuevo = crear_pcb(nombre, burst, prioridad, arrival);
        if (!nuevo) continue;

        // Si el tiempo de llegada es menor o igual al tick actual, entra a READY inmediatamente
        if (arrival <= clock_ticks) {
            nuevo->estado = ESTADO_READY;
            encolar_ready(nuevo);
        } else {
            // Si es para un tiempo futuro, va a la cola de pendientes por lote (batch_queue)
            nuevo->estado = ESTADO_NEW;
            encolar_batch(nuevo);
        }
        cargados++;
    }

    fclose(f);
    return cargados;
}
