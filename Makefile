CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2
TARGET = simulador_kernel.exe
OBJS = Principal.o Interfaz_CL.o kernel_core.o cpu_scheduler.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET)

Principal.o: Principal.c kernel.h
	$(CC) $(CFLAGS) -c Principal.c

Interfaz_CL.o: Interfaz_CL.c kernel.h
	$(CC) $(CFLAGS) -c Interfaz_CL.c

kernel_core.o: kernel_core.c kernel.h
	$(CC) $(CFLAGS) -c kernel_core.c

cpu_scheduler.o: cpu_scheduler.c kernel.h
	$(CC) $(CFLAGS) -c cpu_scheduler.c

clean:
	del /Q $(OBJS) $(TARGET) 2>nul || rm -f $(OBJS) $(TARGET)

.PHONY: all clean
