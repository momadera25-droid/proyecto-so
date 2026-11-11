#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "kernel.h"

/* ============================================================================
 * PROYECTO SISTEMAS OPERATIVOS - FASE I (UCAB)
 * SIMULADOR DE KERNEL Y GESTOR DE PROCESOS (SHELL & SCHEDULING ENGINE)
 * ============================================================================
 * ARCHIVO: Principal.c
 * RESPONSABILIDAD:
 *   - Punto de entrada (main) de la aplicación.
 *   - Inicializa las estructuras globales y el cerrojo de sincronización (mutex).
 *   - Genera de forma concurrente los dos hilos principales POSIX:
 *       1. Hilo CLI: Interfaz interactiva de comandos del usuario.
 *       2. Hilo Motor de Scheduling: Simulación del reloj de CPU y algoritmos.
 *   - Sincroniza la finalización ordenada de ambos hilos y libera los recursos.
 * ============================================================================
 */

int main(void) {
    // 1. Inicializar estructuras del kernel, colas y el mutex global
    kernel_init();

    pthread_t cli_thread;
    pthread_t cpu_thread;

    // 2. Crear el Hilo Motor de Scheduling (CPU)
    // Este hilo corre en segundo plano gestionando los ticks del reloj y ejecutando procesos
    if (pthread_create(&cpu_thread, NULL, cpu_scheduler_thread_func, NULL) != 0) {
        perror("[ERROR CRITICO]: No se pudo crear el Hilo Motor de Scheduling");
        kernel_destroy();
        return 1;
    }

    // 3. Crear el Hilo CLI (Interfaz de Consola)
    // Este hilo atiende los comandos del usuario de forma reactiva sin bloquear la CPU
    if (pthread_create(&cli_thread, NULL, cli_thread_func, NULL) != 0) {
        perror("[ERROR CRITICO]: No se pudo crear el Hilo CLI");
        sim_exit = 1;
        pthread_join(cpu_thread, NULL);
        kernel_destroy();
        return 1;
    }

    // 4. El hilo principal espera a que el usuario termine su sesión en la CLI (comando 'exit')
    pthread_join(cli_thread, NULL);

    // 5. Garantizar que el motor de CPU se detenga
    sim_exit = 1;
    pthread_join(cpu_thread, NULL);

    // 6. Liberación de memoria dinámica y destrucción limpia del mutex
    kernel_destroy();

    printf("\n[SISTEMA]: Simulador finalizado exitosamente. Memoria y recursos liberados.\n");
    return 0;
}
