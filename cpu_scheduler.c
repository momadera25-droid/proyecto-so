#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "kernel.h"

/* ============================================================================
 * MODULO: cpu_scheduler.c
 * RESPONSABILIDAD:
 *   - Simula el motor de ejecución del procesador (CPU) en un hilo concurrente.
 *   - Gestiona el avance del reloj discreto (clock_ticks).
 *   - Aplica las políticas de planificación:
 *       1. Round Robin (RR) con alternancia por Quantum.
 *       2. Shortest Remaining Time First (SRTF) con desapropiación inmediata.
 *   - Gestiona la transición de procesos en lote (Arrival Time).
 *   - Registra el diagrama de Gantt y contabiliza los tiempos para las métricas.
 * ============================================================================
 */

// Función auxiliar para retardo multiplataforma en milisegundos
static void sleep_ms(int ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif
}

/* ============================================================================
 * FUNCIÓN: registrar_gantt
 * Registra o extiende un bloque en el historial del Diagrama de Gantt.
 * Si el proceso que se está ejecutando es el mismo del último bloque registrado,
 * simplemente extiende su tiempo final (ej. de 0-1s pasa a 0-2s) para no saturar.
 * ============================================================================
 */
void registrar_gantt(const char* nombre, int inicio, int fin) {
    if (inicio >= fin) return;

    if (gantt_tail != NULL && strcmp(gantt_tail->nombre, nombre) == 0 && gantt_tail->tick_fin == inicio) {
        gantt_tail->tick_fin = fin;
        return;
    }

    SegmentoGantt* nuevo = (SegmentoGantt*)malloc(sizeof(SegmentoGantt));
    if (!nuevo) return;

    snprintf(nuevo->nombre, sizeof(nuevo->nombre), "%s", nombre);
    nuevo->tick_inicio = inicio;
    nuevo->tick_fin = fin;
    nuevo->siguiente = NULL;

    if (gantt_head == NULL) {
        gantt_head = nuevo;
        gantt_tail = nuevo;
    } else {
        gantt_tail->siguiente = nuevo;
        gantt_tail = nuevo;
    }
}

/* ============================================================================
 * FUNCIÓN: extraer_mejor_srtf
 * Busca en la cola de listos (ready_queue) el proceso con la ráfaga restante
 * más corta (Shortest Remaining Time). En caso de empate, respeta el orden de llegada (FIFO).
 * Extrae el nodo de la cola enlazada y lo retorna.
 * ============================================================================
 */
static PCB* extraer_mejor_srtf(void) {
    if (ready_queue == NULL) return NULL;

    PCB* actual = ready_queue;
    PCB* mejor = ready_queue;
    PCB* prev = NULL;
    PCB* prev_mejor = NULL;

    while (actual != NULL) {
        if (actual->tiempo_restante < mejor->tiempo_restante) {
            mejor = actual;
            prev_mejor = prev;
        }
        prev = actual;
        actual = actual->siguiente;
    }

    // Desconectar el mejor de la lista enlazada
    if (prev_mejor == NULL) {
        ready_queue = mejor->siguiente;
    } else {
        prev_mejor->siguiente = mejor->siguiente;
    }
    mejor->siguiente = NULL;
    return mejor;
}

/* ============================================================================
 * FUNCIÓN: extraer_primero_rr
 * Extrae el primer proceso del frente de la cola de listos (disciplina FIFO).
 * ============================================================================
 */
static PCB* extraer_primero_rr(void) {
    if (ready_queue == NULL) return NULL;

    PCB* primero = ready_queue;
    ready_queue = ready_queue->siguiente;
    primero->siguiente = NULL;
    return primero;
}

/* ============================================================================
 * FUNCIÓN: transferir_trabajos_batch
 * Revisa la cola de trabajos cargados por lote (batch_queue). Aquellos cuyo
 * tiempo simulado de llegada sea menor o igual al reloj actual (arrival_time <= clock_ticks)
 * son admitidos en el sistema y transferidos a la cola de LISTOS (ready_queue).
 * ============================================================================
 */
static void transferir_trabajos_batch(void) {
    PCB* actual = batch_queue;
    PCB* prev = NULL;

    while (actual != NULL) {
        if (actual->arrival_time <= clock_ticks) {
            PCB* admitir = actual;
            if (prev == NULL) {
                batch_queue = actual->siguiente;
                actual = batch_queue;
            } else {
                prev->siguiente = actual->siguiente;
                actual = prev->siguiente;
            }
            admitir->siguiente = NULL;
            admitir->estado = ESTADO_READY;
            encolar_ready(admitir);
        } else {
            prev = actual;
            actual = actual->siguiente;
        }
    }
}

/* ============================================================================
 * HILO PRINCIPAL: cpu_scheduler_thread_func
 * Simula el reloj de la CPU en ciclos discretos (+1 tick) y ejecuta la política
 * de planificación configurada (RR o SRTF).
 * ============================================================================
 */
void* cpu_scheduler_thread_func(void* arg) {
    (void)arg;
    int rr_ticks_consumidos = 0; // Contador de ticks consumidos en el quantum actual (para Round Robin)

    while (!sim_exit) {
        // Si el simulador está en pausa (o no ha iniciado con 'start'), dormimos brevemente
        if (!sim_running) {
            sleep_ms(50);
            continue;
        }

        // ====================================================================
        // SECCIÓN CRÍTICA: Bloqueo del mutex para manipular las colas del kernel
        // ====================================================================
        pthread_mutex_lock(&sistema_mutex);

        // 1. Admitir trabajos en lote que hayan llegado en este tick
        transferir_trabajos_batch();

        // 2. LÓGICA DE PLANIFICACIÓN Y DESAPROPIACIÓN (PREEMPTION)
        if (sched_algorithm == PLANIF_SRTF) {
            // ALGORITMO SRTF:
            // Si hay un proceso corriendo, comprobamos si en la cola de listos existe
            // otro proceso con menor tiempo restante que el actual.
            if (running_proc != NULL) {
                PCB* candidato = ready_queue;
                PCB* mejor_en_cola = NULL;
                while (candidato != NULL) {
                    if (mejor_en_cola == NULL || candidato->tiempo_restante < mejor_en_cola->tiempo_restante) {
                        mejor_en_cola = candidato;
                    }
                    candidato = candidato->siguiente;
                }

                // Si encontramos uno estrictamente más corto, DESAPROPIAMOS al que está corriendo
                if (mejor_en_cola != NULL && mejor_en_cola->tiempo_restante < running_proc->tiempo_restante) {
                    running_proc->estado = ESTADO_READY;
                    encolar_ready(running_proc);
                    running_proc = NULL;
                }
            }

            // Si la CPU quedó libre (o no tenía proceso), seleccionamos el más corto
            if (running_proc == NULL) {
                running_proc = extraer_mejor_srtf();
                if (running_proc != NULL) {
                    running_proc->estado = ESTADO_RUNNING;
                }
            }
        } 
        else if (sched_algorithm == PLANIF_RR) {
            // ALGORITMO ROUND ROBIN:
            // Si el proceso en CPU ya consumió su quantum completo y aún quedan procesos esperando
            if (running_proc != NULL && rr_ticks_consumidos >= rr_quantum) {
                if (ready_queue != NULL) {
                    // Desapropiar: pasa de RUNNING a READY y va al final de la cola
                    running_proc->estado = ESTADO_READY;
                    encolar_ready(running_proc);
                    running_proc = NULL;
                    rr_ticks_consumidos = 0;
                } else {
                    // Si no hay nadie más en cola, continúa ejecutándose renovando quantum
                    rr_ticks_consumidos = 0;
                }
            }

            // Si la CPU está libre, tomamos el siguiente del frente de la cola (FIFO)
            if (running_proc == NULL) {
                running_proc = extraer_primero_rr();
                if (running_proc != NULL) {
                    running_proc->estado = ESTADO_RUNNING;
                    rr_ticks_consumidos = 0;
                }
            }
        }

        // 3. EJECUCIÓN DEL CICLO DE CPU (1 TICK)
        int tick_inicio = clock_ticks;

        if (running_proc != NULL) {
            // Si es la primera vez que el proceso entra a la CPU, calculamos el Tiempo de Respuesta (RT)
            if (running_proc->start_time == -1) {
                running_proc->start_time = clock_ticks;
                running_proc->response_time = running_proc->start_time - running_proc->arrival_time;
                if (running_proc->response_time < 0) running_proc->response_time = 0;
            }

            // Consumo de 1 unidad de ráfaga
            running_proc->tiempo_restante--;
            cpu_busy_ticks++;
            if (sched_algorithm == PLANIF_RR) {
                rr_ticks_consumidos++;
            }

            // Registrar segmento en Gantt
            registrar_gantt(running_proc->nombre, tick_inicio, tick_inicio + 1);

            // ¿Culminó el proceso su ráfaga total?
            if (running_proc->tiempo_restante <= 0) {
                running_proc->estado = ESTADO_TERMINATED;
                running_proc->completion_time = clock_ticks + 1;
                
                // Cálculo de métricas finales del proceso
                running_proc->turnaround_time = running_proc->completion_time - running_proc->arrival_time;
                running_proc->waiting_time = running_proc->turnaround_time - running_proc->burst_time;
                if (running_proc->waiting_time < 0) running_proc->waiting_time = 0;

                // Mover al final de la lista de procesos terminados para mantener orden correlativo
                running_proc->siguiente = NULL;
                if (terminated_list == NULL) {
                    terminated_list = running_proc;
                } else {
                    PCB* t = terminated_list;
                    while (t->siguiente != NULL) {
                        t = t->siguiente;
                    }
                    t->siguiente = running_proc;
                }

                running_proc = NULL;
                rr_ticks_consumidos = 0;
            }
        } else {
            // CPU OCIOSA (IDLE): No hay procesos listos en este instante
            registrar_gantt("[IDLE]", tick_inicio, tick_inicio + 1);
            rr_ticks_consumidos = 0;
        }

        // 4. ACTUALIZAR TIEMPOS DE ESPERA (WAITING TIME) PARA LOS QUE ESTÁN EN READY
        PCB* en_espera = ready_queue;
        while (en_espera != NULL) {
            en_espera->waiting_time++;
            en_espera = en_espera->siguiente;
        }

        // 5. INCREMENTAR EL RELOJ SIMULADO DISCRETO
        clock_ticks++;

        // Obtenemos una copia local de speed_ms antes de soltar el mutex
        int retardo_actual = speed_ms;

        pthread_mutex_unlock(&sistema_mutex);
        // ====================================================================
        // FIN SECCIÓN CRÍTICA
        // ====================================================================

        // Control de velocidad de simulación: se ejecuta fuera del mutex para no bloquear la CLI
        sleep_ms(retardo_actual);
    }

    return NULL;
}
