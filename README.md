# Sistema de Reservas – Proyecto de Sistemas Operativos

Este repositorio contiene la implementación de un **sistema de reservas concurrente** desarrollado en C utilizando **procesos POSIX**, **hilos** y **pipes nominales**.  
El sistema simula el funcionamiento de un parque con aforo limitado y múltiples agentes solicitando reservas simultáneamente.

---

## Características principales

- Arquitectura **cliente/servidor**:
  - **Controlador**: gestiona aforo, horas y solicitudes.
  - **Agentes**: envían solicitudes desde archivos CSV.
- Comunicación mediante **pipes nominales (FIFO)**.
- Manejo de **concurrencia con pthreads**.
- Validación de:
  - Aforo
  - Bloques de dos horas consecutivas
  - Solicitudes extemporáneas
  - Reprogramaciones automáticas
- Simulación configurable del paso del tiempo.
- Reporte final con:
  - Horas pico y valle
  - Solicitudes aceptadas
  - Solicitudes reprogramadas
  - Solicitudes negadas

---

## Estructura del proyecto

Proyecto_SO_final/
├── build/ # Ejecutables generados
├── src/ # Código fuente
│ ├── controlador.c
│ ├── agente.c
│ ├── utils.c
├── data/ # Archivos CSV de prueba
├── Makefile
└── README.md
---

##  Compilación


```bash
make
Los ejecutables se generarán en la carpeta build/.

 Ejecución
1. Crear pipe (si no existe)
bash
Copiar código
rm -f pipeRecibe
mkfifo pipeRecibe
2. Ejecutar el Controlador
bash
./build/controlador -i 7 -f 19 -s 1 -t 30 -p pipeRecibe
Parámetros:

Flag	Significado
-i	Hora inicial
-f	Hora final
-s	Segundos que dura 1 hora simulada
-t	Aforo máximo del parque
-p	Pipe por el que recibe solicitudes

3. Ejecutar un Agente
bash
./build/agente -s A -a data/solicitudes_A.csv -p pipeRecibe
Parámetros:

Flag	Significado
-s	Nombre del agente
-a	Archivo CSV con solicitudes
-p	Pipe hacia el controlador

 Pruebas recomendadas
Aceptación de reservas simples

Reprogramación por aforo lleno

Solicitudes extemporáneas

Múltiples agentes concurrentes

Negación por falta de bloques disponibles

Manejo de fin de simulación

Requisitos
Sistema operativo Linux o macOS

Compilador GCC

Soporte POSIX para:

mkfifo

pthread

fork / procesos

comunicación por archivos FIFO

Autoría
Proyecto desarrollado como parte del curso Sistemas Operativos – Facultad de Ingeniería
Por: Andres Loreto y Santiago Hernandez
