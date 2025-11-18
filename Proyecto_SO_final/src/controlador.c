#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <pthread.h>



#define MAX_NOMBRE 64
#define MAX_PIPE   128
#define MIN_HORA   7
#define MAX_HORA   19

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
    int codigoRespuesta;   /* 1=OK, 2=REPROG, 3=NEGADA_EXTEMP, 4=NEGADA_SIN_OPCION */
    int horaAsignada;
} Mensaje;

typedef struct Reserva {
    char familia[MAX_NOMBRE];
    int personas;
    int horaInicio;
    int horaFin;      /* horaInicio + 2 */
    struct Reserva *sig;
} Reserva;

typedef struct AgenteInfo {
    char nombre[MAX_NOMBRE];
    char pipeRespuesta[MAX_PIPE];
    int fdRespuesta;
    struct AgenteInfo *sig;
} AgenteInfo;

typedef struct {
    int ocupacion[24];      
    int horaActual;
    int horaFin;
    int horaIni;
    int aforo;
    int cantNegadas;
    int cantReprog;
    int cantAceptadasOriginal;
    Reserva *reservas;
} EstadoParque;



EstadoParque parque;
char pipePrincipal[128];
int fdPrincipal = -1;
int horaPorSegundo = 1;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
AgenteInfo *listaAgentes = NULL;
int simulacionActiva = 1;



void error(const char *msg);
void crearPipeSiNoExiste(const char *nombre);
int abrirPipeLectura(const char *nombre);
int abrirPipeEscritura(const char *nombre);
void enviarMensaje(int fd, Mensaje *m);
int recibirMensaje(int fd, Mensaje *m);

void inicializarControlador(int horaIni, int horaFin, int segHoras, int aforo, const char *pipeRecibe);
void *hiloSolicitudes(void *arg);
void *hiloReloj(void *arg);
void procesarRegistro(Mensaje *m);
void procesarSolicitud(Mensaje *m);
int verificarBloqueDisponible(int horaInicio, int personas);
void reservarFamilia(const char *familia, int personas, int horaInicio);
AgenteInfo *buscarAgente(const char *nombre);
AgenteInfo *agregarAgente(const char *nombre, const char *pipeRespuesta);
void imprimirEstadoHora();
void enviarMensajeFinAgentes();
void generarReporteFinal();
static void imprimirUso(const char *prog);
 

void error(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

void crearPipeSiNoExiste(const char *nombre) {
    if (mkfifo(nombre, 0666) == -1) {
        if (errno != EEXIST) {
            error("mkfifo");
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
    if (n != sizeof(Mensaje)) {
        error("write mensaje");
    }
}

int recibirMensaje(int fd, Mensaje *m) {
    ssize_t n = read(fd, m, sizeof(Mensaje));
    if (n == -1) {
        error("read mensaje");
    }
    return (int)n;
}

/* ============================
   Manejo de agentes
   ============================ */

AgenteInfo *buscarAgente(const char *nombre) {
    AgenteInfo *act = listaAgentes;
    while (act) {
        if (strcmp(act->nombre, nombre) == 0) return act;
        act = act->sig;
    }
    return NULL;
}

AgenteInfo *agregarAgente(const char *nombre, const char *pipeRespuesta) {
    AgenteInfo *nuevo = (AgenteInfo *)malloc(sizeof(AgenteInfo));
    if (!nuevo) error("malloc AgenteInfo");

    memset(nuevo, 0, sizeof(AgenteInfo));
    strncpy(nuevo->nombre, nombre, sizeof(nuevo->nombre) - 1);
    strncpy(nuevo->pipeRespuesta, pipeRespuesta, sizeof(nuevo->pipeRespuesta) - 1);
    nuevo->fdRespuesta = abrirPipeEscritura(pipeRespuesta);

    nuevo->sig = listaAgentes;
    listaAgentes = nuevo;
    return nuevo;
}

/* ============================
   Manejo de reservas/parque
   ============================ */

int verificarBloqueDisponible(int horaInicio, int personas) {
    if (horaInicio < parque.horaIni) return 0;
    if (horaInicio + 1 > parque.horaFin) return 0;
    if (horaInicio < MIN_HORA || horaInicio > MAX_HORA) return 0;
    if (horaInicio + 1 < MIN_HORA || horaInicio + 1 > MAX_HORA) return 0;

    if (parque.ocupacion[horaInicio] + personas > parque.aforo) return 0;
    if (parque.ocupacion[horaInicio + 1] + personas > parque.aforo) return 0;

    return 1;
}

void reservarFamilia(const char *familia, int personas, int horaInicio) {
    Reserva *r = (Reserva *)malloc(sizeof(Reserva));
    if (!r) error("malloc Reserva");

    memset(r, 0, sizeof(Reserva));
    strncpy(r->familia, familia, sizeof(r->familia) - 1);
    r->personas = personas;
    r->horaInicio = horaInicio;
    r->horaFin = horaInicio + 2;

    r->sig = parque.reservas;
    parque.reservas = r;

    parque.ocupacion[horaInicio] += personas;
    parque.ocupacion[horaInicio + 1] += personas;
}

/* ============================
   Impresión estado por hora
   ============================ */

void imprimirEstadoHora() {
    int hora = parque.horaActual;
    int salenTotal = 0;
    int entranTotal = 0;

    printf("--------------------------------------------------\n");
    printf("Hora actual de simulacion: %d\n", hora);

    printf("Salen familias: ");
    Reserva *r = parque.reservas;
    int primero = 1;
    while (r) {
        if (r->horaFin == hora) {
            if (!primero) printf(", ");
            printf("%s(%d)", r->familia, r->personas);
            salenTotal += r->personas;
            primero = 0;
        }
        r = r->sig;
    }
    if (primero) printf("ninguna");
    printf(" -> Total que salen: %d\n", salenTotal);

    printf("Entran familias: ");
    r = parque.reservas;
    primero = 1;
    while (r) {
        if (r->horaInicio == hora) {
            if (!primero) printf(", ");
            printf("%s(%d)", r->familia, r->personas);
            entranTotal += r->personas;
            primero = 0;
        }
        r = r->sig;
    }
    if (primero) printf("ninguna");
    printf(" -> Total que entran: %d\n", entranTotal);

    if (hora >= parque.horaIni && hora <= parque.horaFin) {
        printf("Ocupacion programada para la hora %d: %d personas\n",
               hora, parque.ocupacion[hora]);
    }

    printf("--------------------------------------------------\n");
}

/* ============================
   Reporte final
   ============================ */

void generarReporteFinal() {
    int h;
    int maxOcup = -1, minOcup = 1000000;
    int horasMax[24], horasMin[24];
    int nMax = 0, nMin = 0;

    for (h = parque.horaIni; h <= parque.horaFin; h++) {
        int occ = parque.ocupacion[h];
        if (maxOcup == -1 || occ > maxOcup) {
            maxOcup = occ;
            nMax = 0;
            horasMax[nMax++] = h;
        } else if (occ == maxOcup) {
            horasMax[nMax++] = h;
        }

        if (minOcup == 1000000 || occ < minOcup) {
            minOcup = occ;
            nMin = 0;
            horasMin[nMin++] = h;
        } else if (occ == minOcup) {
            horasMin[nMin++] = h;
        }
    }

    printf("\n============= REPORTE FINAL DEL CONTROLADOR =============\n");

    printf("Horas pico (mayor ocupacion = %d personas): ", maxOcup);
    for (int i = 0; i < nMax; i++) {
        if (i > 0) printf(", ");
        printf("%d", horasMax[i]);
    }
    printf("\n");

    printf("Horas valle (menor ocupacion = %d personas): ", minOcup);
    for (int i = 0; i < nMin; i++) {
        if (i > 0) printf(", ");
        printf("%d", horasMin[i]);
    }
    printf("\n");

    printf("Cantidad de solicitudes negadas: %d\n", parque.cantNegadas);
    printf("Cantidad de solicitudes aceptadas en su hora original: %d\n",
           parque.cantAceptadasOriginal);
    printf("Cantidad de solicitudes reprogramadas: %d\n", parque.cantReprog);

    printf("=========================================================\n");
}



void enviarMensajeFinAgentes() {
    Mensaje m;
    memset(&m, 0, sizeof(Mensaje));
    m.tipo = MSG_FIN;

    AgenteInfo *a = listaAgentes;
    while (a) {
        enviarMensaje(a->fdRespuesta, &m);
        a = a->sig;
    }
}

/* ============================
   Lógica de registro y solicitud
   ============================ */

void procesarRegistro(Mensaje *m) {
    pthread_mutex_lock(&lock);

    AgenteInfo *ag = buscarAgente(m->agente);
    if (!ag) {
        ag = agregarAgente(m->agente, m->pipeRespuesta);
    }

    Mensaje resp;
    memset(&resp, 0, sizeof(Mensaje));
    resp.tipo = MSG_REGISTRO_OK;
    strncpy(resp.agente, m->agente, sizeof(resp.agente) - 1);
    resp.hora = parque.horaActual;

    pthread_mutex_unlock(&lock);

    enviarMensaje(ag->fdRespuesta, &resp);
}

void procesarSolicitud(Mensaje *m) {
    pthread_mutex_lock(&lock);

    Mensaje resp;
    memset(&resp, 0, sizeof(Mensaje));
    resp.tipo = MSG_RESPUESTA;
    strncpy(resp.agente, m->agente, sizeof(resp.agente) - 1);
    strncpy(resp.familia, m->familia, sizeof(resp.familia) - 1);
    resp.hora = m->hora;
    resp.personas = m->personas;
    resp.horaAsignada = -1;
    resp.codigoRespuesta = 0;

    int personas = m->personas;
    int horaReq = m->hora;

    int codigo = 0;
    int horaAsign = -1;

    if (personas > parque.aforo) {
        codigo = 4;
        parque.cantNegadas++;
    } else if (horaReq > parque.horaFin) {
        codigo = 4;
        parque.cantNegadas++;
    } else {
        int extemporanea = (horaReq < parque.horaActual);

        if (!extemporanea && verificarBloqueDisponible(horaReq, personas)) {
            horaAsign = horaReq;
            reservarFamilia(m->familia, personas, horaAsign);
            codigo = 1;
            parque.cantAceptadasOriginal++;
        } else {
            int startSearch = parque.horaActual;
            if (startSearch < parque.horaIni) startSearch = parque.horaIni;

            for (int h = startSearch; h <= parque.horaFin - 1; h++) {
                if (verificarBloqueDisponible(h, personas)) {
                    horaAsign = h;
                    reservarFamilia(m->familia, personas, horaAsign);
                    break;
                }
            }

            if (horaAsign != -1) {
                codigo = 2;
                parque.cantReprog++;
            } else {
                codigo = 4;
                parque.cantNegadas++;
            }
        }
    }

    resp.codigoRespuesta = codigo;
    resp.horaAsignada = horaAsign;

    AgenteInfo *ag = buscarAgente(m->agente);

    pthread_mutex_unlock(&lock);

    if (ag) {
        enviarMensaje(ag->fdRespuesta, &resp);
    } else {
        fprintf(stderr, "Controlador: no se encontro agente %s para responder\n", m->agente);
    }

    printf("Controlador: peticion de agente %s, familia %s, hora %d, personas %d -> codigoRespuesta=%d, horaAsignada=%d\n",
           m->agente, m->familia, m->hora, m->personas, resp.codigoRespuesta, resp.horaAsignada);
}

/* ============================
   Hilos
   ============================ */

void *hiloSolicitudes(void *arg) {
    (void)arg;
    Mensaje m;

    while (1) {
        int n = recibirMensaje(fdPrincipal, &m);
        if (n <= 0) {
            if (!simulacionActiva) break;
            continue;
        }

        if (m.tipo == MSG_REGISTRO) {
            procesarRegistro(&m);
        } else if (m.tipo == MSG_SOLICITUD) {
            procesarSolicitud(&m);
        }
    }

    return NULL;
}

void *hiloReloj(void *arg) {
    (void)arg;

    while (1) {
        sleep(horaPorSegundo);

        pthread_mutex_lock(&lock);

        if (parque.horaActual >= parque.horaFin) {
            pthread_mutex_unlock(&lock);
            break;
        }

        parque.horaActual++;
        imprimirEstadoHora();

        pthread_mutex_unlock(&lock);
    }

    simulacionActiva = 0;
    enviarMensajeFinAgentes();

    if (fdPrincipal != -1) {
        close(fdPrincipal);
        fdPrincipal = -1;
    }

    return NULL;
}

/* ============================
   Inicialización y main
   ============================ */

static void imprimirUso(const char *prog) {
    fprintf(stderr,
            "Uso: %s -i horaIni -f horaFin -s segHoras -t aforo -p pipeRecibe\n",
            prog);
}

void inicializarControlador(int horaIni, int horaFin, int segHoras, int aforo, const char *pipeRecibe) {
    memset(&parque, 0, sizeof(EstadoParque));

    parque.horaActual = horaIni;
    parque.horaIni = horaIni;
    parque.horaFin = horaFin;
    parque.aforo = aforo;

    horaPorSegundo = segHoras;
    strncpy(pipePrincipal, pipeRecibe, sizeof(pipePrincipal) - 1);

    crearPipeSiNoExiste(pipeRecibe);

    fdPrincipal = open(pipeRecibe, O_RDWR);
    if (fdPrincipal == -1) {
        error("open pipeRecibe O_RDWR");
    }
}

int main(int argc, char *argv[]) {
    int horaIni = 0, horaFin = 0, segHoras = 0, aforo = 0;
    char pipeRecibe[128] = {0};

    int opt;
    int flagI = 0, flagF = 0, flagS = 0, flagT = 0, flagP = 0;

    while ((opt = getopt(argc, argv, "i:f:s:t:p:")) != -1) {
        switch (opt) {
            case 'i':
                horaIni = atoi(optarg);
                flagI = 1;
                break;
            case 'f':
                horaFin = atoi(optarg);
                flagF = 1;
                break;
            case 's':
                segHoras = atoi(optarg);
                flagS = 1;
                break;
            case 't':
                aforo = atoi(optarg);
                flagT = 1;
                break;
            case 'p':
                strncpy(pipeRecibe, optarg, sizeof(pipeRecibe) - 1);
                pipeRecibe[sizeof(pipeRecibe) - 1] = '\0';
                flagP = 1;
                break;
            default:
                imprimirUso(argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if (!flagI || !flagF || !flagS || !flagT || !flagP) {
        imprimirUso(argv[0]);
        exit(EXIT_FAILURE);
    }

    if (horaIni < MIN_HORA || horaIni > MAX_HORA ||
        horaFin < MIN_HORA || horaFin > MAX_HORA ||
        horaIni > horaFin || segHoras <= 0 || aforo <= 0) {
        fprintf(stderr, "Parametros invalidos.\n");
        imprimirUso(argv[0]);
        exit(EXIT_FAILURE);
    }

    inicializarControlador(horaIni, horaFin, segHoras, aforo, pipeRecibe);

    pthread_t thSolicitudes, thReloj;
    pthread_create(&thSolicitudes, NULL, hiloSolicitudes, NULL);
    pthread_create(&thReloj, NULL, hiloReloj, NULL);

    pthread_join(thSolicitudes, NULL);
    pthread_join(thReloj, NULL);

    generarReporteFinal();

    Reserva *r = parque.reservas;
    while (r) {
        Reserva *tmp = r;
        r = r->sig;
        free(tmp);
    }

    AgenteInfo *a = listaAgentes;
    while (a) {
        AgenteInfo *tmp = a;
        if (tmp->fdRespuesta != -1) close(tmp->fdRespuesta);
        a = a->sig;
        free(tmp);
    }

    return 0;
}
