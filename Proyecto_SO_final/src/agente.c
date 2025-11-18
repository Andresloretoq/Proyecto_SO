#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

/* ============================
   Definiciones compartidas
   ============================ */

#define MAX_NOMBRE 64
#define MAX_PIPE   128

typedef enum {
    MSG_REGISTRO,
    MSG_REGISTRO_OK,
    MSG_SOLICITUD,
    MSG_RESPUESTA,
    MSG_FIN
} TipoMensaje;

typedef struct {
    TipoMensaje tipo;
    char agente[MAX_NOMBRE];
    char familia[MAX_NOMBRE];
    int hora;
    int personas;
    char pipeRespuesta[MAX_PIPE];
    int codigoRespuesta;
    int horaAsignada;
} Mensaje;


int fdRecibe = -1;          
int fdRespuesta = -1;       
int horaActualSimulacion = 0;
char nombreAgente[MAX_NOMBRE] = {0};



void error(const char *msg);
void crearPipeSiNoExiste(const char *nombre);
int abrirPipeLectura(const char *nombre);
int abrirPipeEscritura(const char *nombre);
void enviarMensaje(int fd, Mensaje *m);
int recibirMensaje(int fd, Mensaje *m);

void registrarAgente(const char *nombre, const char *pipeRecibe, const char *pipeRespuesta);
void enviarSolicitudes(const char *fileSolicitud);
static void imprimirUso(const char *prog);



void error(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

void crearPipeSiNoExiste(const char *nombre) {
    if (mkfifo(nombre, 0666) == -1) {
        if (errno != EEXIST) {
            perror("mkfifo");
            exit(EXIT_FAILURE);
        }
    }
}

int abrirPipeLectura(const char *nombre) {
    int fd = open(nombre, O_RDONLY);
    if (fd == -1) error("open lectura");
    return fd;
}

int abrirPipeEscritura(const char *nombre) {
    int fd = open(nombre, O_WRONLY);
    if (fd == -1) error("open escritura");
    return fd;
}

void enviarMensaje(int fd, Mensaje *m) {
    ssize_t n = write(fd, m, sizeof(Mensaje));
    if (n != sizeof(Mensaje)) error("write mensaje");
}

int recibirMensaje(int fd, Mensaje *m) {
    ssize_t n = read(fd, m, sizeof(Mensaje));
    if (n == -1) error("read mensaje");
    return (int)n;
}

/* ============================
   Lógica del agente
   ============================ */

static void imprimirUso(const char *prog) {
    fprintf(stderr,
            "Uso: %s -s nombreAgente -a fileSolicitud -p pipeRecibe\n",
            prog);
}


void registrarAgente(const char *nombre, const char *pipeRecibe, const char *pipeRespuesta) {
    Mensaje m;

  
    crearPipeSiNoExiste(pipeRespuesta);

   
    fdRecibe = abrirPipeEscritura(pipeRecibe);

    
    memset(&m, 0, sizeof(Mensaje));
    m.tipo = MSG_REGISTRO;
    strncpy(m.agente, nombre, sizeof(m.agente) - 1);
    strncpy(m.pipeRespuesta, pipeRespuesta, sizeof(m.pipeRespuesta) - 1);

    enviarMensaje(fdRecibe, &m);


    fdRespuesta = abrirPipeLectura(pipeRespuesta);

    if (recibirMensaje(fdRespuesta, &m) <= 0) {
        error("Error recibiendo MSG_REGISTRO_OK");
    }

    if (m.tipo != MSG_REGISTRO_OK) {
        fprintf(stderr, "Agente %s: respuesta inesperada al registrar.\n", nombre);
    }

    horaActualSimulacion = m.hora;

    printf("Agente %s registrado. Hora actual de simulacion: %d\n",
           nombre, horaActualSimulacion);
}

/* Envío de solicitudes desde el CSV */
void enviarSolicitudes(const char *fileSolicitud) {
    FILE *f = fopen(fileSolicitud, "r");
    if (!f) {
        perror("No se pudo abrir archivo de solicitudes");
        exit(EXIT_FAILURE);
    }

    char familia[MAX_NOMBRE];
    int hora, personas;
    Mensaje m;

    while (fscanf(f, "%63[^,],%d,%d\n", familia, &hora, &personas) == 3) {

        if (hora < horaActualSimulacion) {
            printf("Agente %s: solicitud ignorada para familia %s, "
                   "hora %d (hora actual simulacion: %d)\n",
                   nombreAgente, familia, hora, horaActualSimulacion);
            continue;
        }

        memset(&m, 0, sizeof(Mensaje));
        m.tipo = MSG_SOLICITUD;
        strncpy(m.agente, nombreAgente, sizeof(m.agente) - 1);
        strncpy(m.familia, familia, sizeof(m.familia) - 1);
        m.hora = hora;
        m.personas = personas;

        printf("Agente %s: enviando solicitud -> Familia: %s, Hora: %d, Personas: %d\n",
               nombreAgente, familia, hora, personas);

        enviarMensaje(fdRecibe, &m);

        if (recibirMensaje(fdRespuesta, &m) <= 0) {
            error("Error recibiendo respuesta del controlador");
        }

        printf("Agente %s: respuesta para familia %s -> "
               "horaSolicitada=%d, personas=%d, codigoRespuesta=%d, horaAsignada=%d\n",
               nombreAgente, m.familia, m.hora, m.personas,
               m.codigoRespuesta, m.horaAsignada);

        sleep(2);
    }

    fclose(f);
}

/* main */
int main(int argc, char *argv[]) {
    char archivo[128] = {0};
    char pipeRecibe[128] = {0};
    char pipeRespuesta[128] = {0};

    int opt;
    int flagNombre = 0, flagArchivo = 0, flagPipe = 0;

    while ((opt = getopt(argc, argv, "s:a:p:")) != -1) {
        switch (opt) {
            case 's':
                strncpy(nombreAgente, optarg, sizeof(nombreAgente) - 1);
                nombreAgente[sizeof(nombreAgente) - 1] = '\0';
                flagNombre = 1;
                break;
            case 'a':
                strncpy(archivo, optarg, sizeof(archivo) - 1);
                archivo[sizeof(archivo) - 1] = '\0';
                flagArchivo = 1;
                break;
            case 'p':
                strncpy(pipeRecibe, optarg, sizeof(pipeRecibe) - 1);
                pipeRecibe[sizeof(pipeRecibe) - 1] = '\0';
                flagPipe = 1;
                break;
            default:
                imprimirUso(argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if (!flagNombre || !flagArchivo || !flagPipe) {
        imprimirUso(argv[0]);
        exit(EXIT_FAILURE);
    }

    snprintf(pipeRespuesta, sizeof(pipeRespuesta),
             "pipe_resp_%s", nombreAgente);

    registrarAgente(nombreAgente, pipeRecibe, pipeRespuesta);
    enviarSolicitudes(archivo);

    printf("Agente %s termina.\n", nombreAgente);

    if (fdRecibe != -1) close(fdRecibe);
    if (fdRespuesta != -1) close(fdRespuesta);

    return 0;
}
