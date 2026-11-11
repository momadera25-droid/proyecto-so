#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <ctype.h>
#include "kernel.h"

/* ============================================================================
 * MODULO: Interfaz_CL.c
 * RESPONSABILIDAD:
 *   - Implementa el Hilo CLI (Interfaz de Línea de Comandos).
 *   - Lee la entrada del usuario de manera interactiva sin bloquear el sistema.
 *   - Valida estrictamente la sintaxis y los tipos de datos de cada comando.
 *   - Despliega las tablas de estado, inspección del PCB, estadísticas y Gantt
 *     siguiendo con precisión milimétrica los formatos exigidos en la Sección 5.
 * ============================================================================
 */

#define MAX_INPUT_LEN 256

// Convierte un enum EstadoProceso a su representación en texto para la salida
static const char* estado_a_string(EstadoProceso est) {
    switch (est) {
        case ESTADO_NEW:        return "NEW";
        case ESTADO_READY:      return "READY";
        case ESTADO_RUNNING:    return "RUNNING";
        case ESTADO_WAITING:    return "WAITING";
        case ESTADO_TERMINATED: return "TERMINATED";
        default:                return "UNKNOWN";
    }
}

/* ============================================================================
 * HILO CLI: cli_thread_func
 * Bucle interactivo que procesa los comandos introducidos por el usuario.
 * ============================================================================
 */
void* cli_thread_func(void* arg) {
    (void)arg;
    char input_line[MAX_INPUT_LEN];

    printf("====================================================================\n");
    printf("   SIMULADOR DE KERNEL Y GESTOR DE PROCESOS (SHELL INTERACTIVA)     \n");
    printf("   Proyecto Sistemas Operativos - UCAB                              \n");
    printf("====================================================================\n");
    printf("Escribe 'help' para ver los comandos disponibles o 'exit' para salir.\n\n");

    while (!sim_exit) {
        printf("> ");
        fflush(stdout);

        if (fgets(input_line, sizeof(input_line), stdin) == NULL) {
            break; // Manejo seguro de fin de archivo (EOF) o cierre de tubería
        }

        // Limpiar retornos de carro y saltos de línea
        input_line[strcspn(input_line, "\r\n")] = '\0';

        // Omitir líneas en blanco
        char* p = input_line;
        while (isspace((unsigned char)*p)) p++;
        if (*p == '\0') {
            continue;
        }

        // Tokenización estricta por espacios en blanco
        char *tokens[10];
        int token_count = 0;

        char *token = strtok(input_line, " \t");
        while (token != NULL && token_count < 10) {
            tokens[token_count++] = token;
            token = strtok(NULL, " \t");
        }

        if (token_count == 0) continue;

        char* cmd = tokens[0];

        // ====================================================================
        // 1. COMANDO: exit
        // ====================================================================
        if (strcmp(cmd, "exit") == 0) {
            if (token_count != 1) {
                printf("[ERROR DE SINTAXIS]: El comando 'exit' no recibe parametros.\n");
            } else {
                printf("Deteniendo el simulador de kernel y liberando recursos...\n");
                pthread_mutex_lock(&sistema_mutex);
                sim_exit = 1;
                sim_running = 0;
                pthread_mutex_unlock(&sistema_mutex);
                break;
            }
        }

        // ====================================================================
        // 2. COMANDO: help
        // ====================================================================
        else if (strcmp(cmd, "help") == 0) {
            if (token_count != 1) {
                printf("[ERROR DE SINTAXIS]: El comando 'help' no recibe parametros.\n");
            } else {
                printf("\n--- MANUAL DE COMANDOS DE LA CLI ---\n");
                printf("  create <nombre> <burst> [prioridad] : Crea un nuevo proceso (prioridad 1-5, por defecto 3)\n");
                printf("  load <archivo>                     : Carga un archivo de traza por lotes (batch mode)\n");
                printf("  ps                                 : Lista el estado actual de todos los procesos\n");
                printf("  pcb <PID>                          : Muestra el detalle extendido del Bloque de Control de Proceso\n");
                printf("  set_sched <RR|SRTF> [quantum]      : Cambia la politica del planificador (defecto RR quantum 2)\n");
                printf("  set_speed <ms_por_tick>            : Ajusta la velocidad del reloj simulado (50 a 5000 ms)\n");
                printf("  start                              : Inicia o reanuda la ejecucion de la CPU\n");
                printf("  pause                              : Detiene temporalmente el reloj para inspeccion estatica\n");
                printf("  gantt                              : Imprime el Diagrama de Gantt acumulado en formato texto\n");
                printf("  stats                              : Despliega las metricas de rendimiento del sistema\n");
                printf("  exit                               : Detiene los hilos y cierra la simulacion de forma limpia\n\n");
            }
        }

        // ====================================================================
        // 3. COMANDO: create <nombre> <burst> [prioridad]
        // ====================================================================
        else if (strcmp(cmd, "create") == 0) {
            if (token_count < 3 || token_count > 4) {
                printf("[ERROR DE SINTAXIS]: Cantidad o tipo de parametros invalido.\n");
                printf("Uso correcto: create <nombre> <burst> [prioridad]\n");
            } else {
                char *nombre = tokens[1];
                char *endptr_burst, *endptr_prio;
                long burst = strtol(tokens[2], &endptr_burst, 10);

                if (*endptr_burst != '\0') {
                    printf("[ERROR DE TIPO]: El tiempo de rafaga (burst) debe ser un numero entero.\n");
                } else if (burst <= 0) {
                    printf("[ERROR DE PARAMETRO]: El tiempo de rafaga debe ser mayor a 0.\n");
                } else {
                    int prioridad = 3;
                    int prioridad_valida = 1;

                    if (token_count == 4) {
                        long prio = strtol(tokens[3], &endptr_prio, 10);
                        if (*endptr_prio != '\0') {
                            printf("[ERROR DE TIPO]: La prioridad debe ser un numero entero.\n");
                            prioridad_valida = 0;
                        } else if (prio < 1 || prio > 5) {
                            printf("[ERROR DE PARAMETRO]: La prioridad debe estar en el rango de 1 a 5.\n");
                            prioridad_valida = 0;
                        } else {
                            prioridad = (int)prio;
                        }
                    }

                    if (prioridad_valida) {
                        pthread_mutex_lock(&sistema_mutex);
                        PCB* nuevo = crear_pcb(nombre, (int)burst, prioridad, clock_ticks);
                        if (!nuevo) {
                            printf("[ERROR]: Fallo al reservar memoria para el PCB.\n");
                        } else {
                            nuevo->estado = ESTADO_READY;
                            encolar_ready(nuevo);
                            printf("[EXITO]: Proceso '%s' creado con PID %d -> Burst: %ld, Prioridad: %d (Arrival: %d)\n",
                                   nuevo->nombre, nuevo->pid, burst, prioridad, nuevo->arrival_time);
                        }
                        pthread_mutex_unlock(&sistema_mutex);
                    }
                }
            }
        }

        // ====================================================================
        // 4. COMANDO: load <archivo>
        // ====================================================================
        else if (strcmp(cmd, "load") == 0) {
            if (token_count != 2) {
                printf("[ERROR DE SINTAXIS]: Parametros insuficientes o excedidos.\n");
                printf("Uso correcto: load <archivo>\n");
            } else {
                pthread_mutex_lock(&sistema_mutex);
                int cargados = cargar_archivo_batch(tokens[1]);
                if (cargados < 0) {
                    printf("[ERROR]: No se pudo abrir o leer el archivo '%s'. Verifique existencia y permisos.\n", tokens[1]);
                } else {
                    printf("[EXITO]: Se cargaron %d procesos desde '%s' respetando sus Arrival Times.\n", cargados, tokens[1]);
                }
                pthread_mutex_unlock(&sistema_mutex);
            }
        }

        // ====================================================================
        // 5. COMANDO: ps
        // ====================================================================
        else if (strcmp(cmd, "ps") == 0) {
            if (token_count != 1) {
                printf("[ERROR DE SINTAXIS]: El comando 'ps' no recibe parametros adicionales.\n");
            } else {
                pthread_mutex_lock(&sistema_mutex);
                printf("\nPID  NOMBRE          ESTADO      BURST RESTANTE  PRIORIDAD\n");
                printf("--------------------------------------------------------------\n");

                int total_procesos = 0;

                // 1. Proceso en ejecución (RUNNING)
                if (running_proc != NULL) {
                    char burst_str[20];
                    snprintf(burst_str, sizeof(burst_str), "%ds", running_proc->tiempo_restante);
                    printf("%-4d %-15s %-11s %-15s %-9d\n",
                           running_proc->pid, running_proc->nombre, "RUNNING",
                           burst_str, running_proc->prioridad);
                    total_procesos++;
                }

                // 2. Procesos en cola de listos (READY)
                PCB* cur = ready_queue;
                while (cur != NULL) {
                    char burst_str[20];
                    snprintf(burst_str, sizeof(burst_str), "%ds", cur->tiempo_restante);
                    printf("%-4d %-15s %-11s %-15s %-9d\n",
                           cur->pid, cur->nombre, "READY",
                           burst_str, cur->prioridad);
                    total_procesos++;
                    cur = cur->siguiente;
                }

                // 3. Procesos terminados (TERMINATED)
                cur = terminated_list;
                while (cur != NULL) {
                    char burst_str[20];
                    snprintf(burst_str, sizeof(burst_str), "%ds", cur->tiempo_restante);
                    printf("%-4d %-15s %-11s %-15s %-9d\n",
                           cur->pid, cur->nombre, "TERMINATED",
                           burst_str, cur->prioridad);
                    total_procesos++;
                    cur = cur->siguiente;
                }

                // 4. Procesos pendientes por llegar (NEW)
                cur = batch_queue;
                while (cur != NULL) {
                    char burst_str[20];
                    snprintf(burst_str, sizeof(burst_str), "%ds", cur->tiempo_restante);
                    printf("%-4d %-15s %-11s %-15s %-9d\n",
                           cur->pid, cur->nombre, "NEW",
                           burst_str, cur->prioridad);
                    total_procesos++;
                    cur = cur->siguiente;
                }

                if (total_procesos == 0) {
                    printf("(No hay procesos registrados en el sistema)\n");
                }
                printf("\n");
                pthread_mutex_unlock(&sistema_mutex);
            }
        }

        // ====================================================================
        // 6. COMANDO: pcb <PID>
        // ====================================================================
        else if (strcmp(cmd, "pcb") == 0) {
            if (token_count != 2) {
                printf("[ERROR DE SINTAXIS]: Parametros insuficientes.\n");
                printf("Uso correcto: pcb <PID>\n");
            } else {
                char* endptr;
                long pid = strtol(tokens[1], &endptr, 10);
                if (*endptr != '\0') {
                    printf("[ERROR DE TIPO]: El PID debe ser un numero entero positivo.\n");
                } else if (pid <= 0) {
                    printf("[ERROR DE PARAMETRO]: El PID debe ser mayor a 0.\n");
                } else {
                    pthread_mutex_lock(&sistema_mutex);
                    PCB* proc = buscar_pcb_global((int)pid);
                    if (proc == NULL) {
                        printf("[ERROR]: El proceso con PID %ld no existe en el sistema.\n", pid);
                    } else {
                        printf("\n==================================================\n");
                        printf("CONTROL BLOCK DE PROCESO (PCB) - PID: %d\n", proc->pid);
                        printf("==================================================\n");
                        printf("Nombre del Proceso : %s\n", proc->nombre);
                        printf("Estado Actual      : %s\n", estado_a_string(proc->estado));
                        printf("Prioridad          : %d\n", proc->prioridad);
                        printf("Tiempo de Llegada  : %d ticks\n", proc->arrival_time);
                        printf("Ráfaga Inicial     : %d ticks\n", proc->burst_time);
                        printf("Ráfaga Restante    : %d ticks\n", proc->tiempo_restante);
                        printf("Tiempo de Espera   : %d ticks\n", proc->waiting_time);
                        
                        if (proc->estado == ESTADO_TERMINATED) {
                            printf("Tiempo de Retorno  : %d ticks\n", proc->turnaround_time);
                        } else {
                            printf("Tiempo de Retorno  : N/A (En ejecucion)\n");
                        }

                        if (proc->response_time >= 0) {
                            printf("Tiempo de Respuesta: %d ticks\n", proc->response_time);
                        } else {
                            printf("Tiempo de Respuesta: N/A (Aun no ingresa a CPU)\n");
                        }
                        printf("==================================================\n\n");
                    }
                    pthread_mutex_unlock(&sistema_mutex);
                }
            }
        }

        // ====================================================================
        // 7. COMANDO: set_sched <algoritmo> [quantum]
        // ====================================================================
        else if (strcmp(cmd, "set_sched") == 0) {
            if (token_count < 2 || token_count > 3) {
                printf("[ERROR DE SINTAXIS]: Parametros invalidos.\n");
                printf("Uso correcto: set_sched <RR|SRTF> [quantum]\n");
            } else {
                char* algo = tokens[1];
                if (strcmp(algo, "RR") != 0 && strcmp(algo, "SRTF") != 0) {
                    printf("[ERROR DE PARAMETRO]: Algoritmo no reconocido. Algoritmos validos: RR, SRTF.\n");
                } else {
                    int nuevo_q = 2; // Por defecto
                    int q_valido = 1;

                    if (token_count == 3) {
                        char* endptr;
                        long q = strtol(tokens[2], &endptr, 10);
                        if (*endptr != '\0') {
                            printf("[ERROR DE TIPO]: El quantum debe ser un numero entero positivo.\n");
                            q_valido = 0;
                        } else if (q <= 0) {
                            printf("[ERROR DE PARAMETRO]: El quantum debe ser mayor a 0.\n");
                            q_valido = 0;
                        } else {
                            nuevo_q = (int)q;
                        }
                    }

                    if (q_valido) {
                        pthread_mutex_lock(&sistema_mutex);
                        if (strcmp(algo, "RR") == 0) {
                            sched_algorithm = PLANIF_RR;
                            rr_quantum = nuevo_q;
                            printf("[CONFIGURACION]: Planificador cambiado a Round Robin (RR) con Quantum = %d.\n", rr_quantum);
                        } else {
                            sched_algorithm = PLANIF_SRTF;
                            printf("[CONFIGURACION]: Planificador cambiado a Shortest Remaining Time First (SRTF).\n");
                        }
                        pthread_mutex_unlock(&sistema_mutex);
                    }
                }
            }
        }

        // ====================================================================
        // 8. COMANDO: set_speed <ms_por_tick>
        // ====================================================================
        else if (strcmp(cmd, "set_speed") == 0) {
            if (token_count != 2) {
                printf("[ERROR DE SINTAXIS]: Parametros invalidos.\n");
                printf("Uso correcto: set_speed <ms_por_tick>\n");
            } else {
                char* endptr;
                long speed = strtol(tokens[1], &endptr, 10);
                if (*endptr != '\0') {
                    printf("[ERROR DE TIPO]: La velocidad debe ser un numero entero.\n");
                } else if (speed < 50 || speed > 5000) {
                    printf("[ERROR DE PARAMETRO]: La velocidad debe estar entre 50 y 5000 ms por tick.\n");
                } else {
                    pthread_mutex_lock(&sistema_mutex);
                    speed_ms = (int)speed;
                    printf("[CONFIGURACION]: Velocidad del reloj establecida a %d ms por tick.\n", speed_ms);
                    pthread_mutex_unlock(&sistema_mutex);
                }
            }
        }

        // ====================================================================
        // 9. COMANDO: start
        // ====================================================================
        else if (strcmp(cmd, "start") == 0) {
            if (token_count != 1) {
                printf("[ERROR DE SINTAXIS]: El comando 'start' no recibe parametros.\n");
            } else {
                pthread_mutex_lock(&sistema_mutex);
                sim_running = 1;
                printf("[ESTADO]: Hilo Motor de Scheduling INICIADO / REANUDADO (Clock actual: %d ticks).\n", clock_ticks);
                pthread_mutex_unlock(&sistema_mutex);
            }
        }

        // ====================================================================
        // 10. COMANDO: pause
        // ====================================================================
        else if (strcmp(cmd, "pause") == 0) {
            if (token_count != 1) {
                printf("[ERROR DE SINTAXIS]: El comando 'pause' no recibe parametros.\n");
            } else {
                pthread_mutex_lock(&sistema_mutex);
                sim_running = 0;
                printf("[ESTADO]: Simulador en PAUSA (Clock detenido en tick %d para inspeccion estatica).\n", clock_ticks);
                pthread_mutex_unlock(&sistema_mutex);
            }
        }

        // ====================================================================
        // 11. COMANDO: gantt
        // ====================================================================
        else if (strcmp(cmd, "gantt") == 0) {
            if (token_count != 1) {
                printf("[ERROR DE SINTAXIS]: El comando 'gantt' no recibe parametros.\n");
            } else {
                pthread_mutex_lock(&sistema_mutex);
                printf("\nDIAGRAMA DE GANTT (0 - %d ticks):\n", clock_ticks);
                if (gantt_head == NULL) {
                    printf("(Aun no se han registrado ciclos de ejecucion)\n");
                } else {
                    SegmentoGantt* s = gantt_head;
                    int primero = 1;
                    while (s != NULL) {
                        if (!primero) {
                            printf(" -> ");
                        }
                        if (strcmp(s->nombre, "[IDLE]") == 0) {
                            printf("[IDLE]");
                        } else {
                            printf("[%d-%ds: %s]", s->tick_inicio, s->tick_fin, s->nombre);
                        }
                        primero = 0;
                        s = s->siguiente;
                    }
                    printf("\n");
                }
                printf("\n");
                pthread_mutex_unlock(&sistema_mutex);
            }
        }

        // ====================================================================
        // 12. COMANDO: stats
        // ====================================================================
        else if (strcmp(cmd, "stats") == 0) {
            if (token_count != 1) {
                printf("[ERROR DE SINTAXIS]: El comando 'stats' no recibe parametros.\n");
            } else {
                pthread_mutex_lock(&sistema_mutex);

                int total_finalizados = 0;
                int total_procesos = 0;
                double sum_wt = 0.0;
                double sum_tat = 0.0;
                double sum_rt = 0.0;
                int count_rt = 0;

                // Contar procesos finalizados
                PCB* c = terminated_list;
                while (c != NULL) {
                    total_finalizados++;
                    total_procesos++;
                    sum_wt += c->waiting_time;
                    sum_tat += c->turnaround_time;
                    if (c->response_time >= 0) {
                        sum_rt += c->response_time;
                        count_rt++;
                    }
                    c = c->siguiente;
                }

                // Contar en ejecución
                if (running_proc != NULL) {
                    total_procesos++;
                    if (running_proc->response_time >= 0) {
                        sum_rt += running_proc->response_time;
                        count_rt++;
                    }
                }

                // Contar en cola de listos
                c = ready_queue;
                while (c != NULL) {
                    total_procesos++;
                    if (c->response_time >= 0) {
                        sum_rt += c->response_time;
                        count_rt++;
                    }
                    c = c->siguiente;
                }

                // Contar pendientes por lote
                c = batch_queue;
                while (c != NULL) {
                    total_procesos++;
                    c = c->siguiente;
                }

                double avg_wt = (total_finalizados > 0) ? (sum_wt / total_finalizados) : 0.0;
                double avg_tat = (total_finalizados > 0) ? (sum_tat / total_finalizados) : 0.0;
                double avg_rt = (count_rt > 0) ? (sum_rt / count_rt) : 0.0;
                double uso_cpu = (clock_ticks > 0) ? (((double)cpu_busy_ticks / clock_ticks) * 100.0) : 0.0;

                printf("\nESTADÍSTICAS DEL SISTEMA:\n");
                printf("- Procesos Finalizados       : %d / %d\n", total_finalizados, total_procesos);
                printf("- Tiempo Promedio de Espera  : %.2f ticks\n", avg_wt);
                printf("- Tiempo Promedio de Retorno : %.2f ticks\n", avg_tat);
                printf("- Tiempo Promedio de Respuesta: %.2f ticks\n", avg_rt);
                printf("- Uso de CPU                 : %.1f%%\n\n", uso_cpu);

                pthread_mutex_unlock(&sistema_mutex);
            }
        }

        // ====================================================================
        // COMANDO DESCONOCIDO
        // ====================================================================
        else {
            printf("[ERROR DE SINTAXIS]: Comando '%s' no reconocido. Escriba 'help' para ver la lista de comandos.\n", cmd);
        }
    }

    return NULL;
}
