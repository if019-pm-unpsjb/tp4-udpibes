#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <stdbool.h>
#include "constantes.h"
#include <asm-generic/socket.h>

typedef struct
{
    int socket;              //  socket de comunicacion
    struct sockaddr_in addr; // Direccion del otro cliente
    char nombre[MAX_TAM_NOMBRE_USUARIO];
} ConexionChat;

typedef struct
{
    char nombre_archivo[MAX_TAM_NOMBRE_ARCHIVO];
    int tamano_archivo;
} Datos_archivo;

char ultimo_nombre_receptor[MAX_TAM_NOMBRE_USUARIO];

int socket_chat_activo = -1;                            // Almacena el socket actual del chat si esta activo
ConexionChat *conexion_chat_actual = NULL;              // Puntero a la estructura de la conexion de chat activa
pthread_mutex_t chat_mutex = PTHREAD_MUTEX_INITIALIZER; // Mutex para sincronizar el acceso a la conexion actual
char nombre_personal[MAX_TAM_NOMBRE_USUARIO];           // Reservamos espacio para el nombre

void enviar_archivo(int socket, const char *nombre_archivo)
{
    if (socket < 0 || nombre_archivo == NULL)
    {
        fprintf(stderr, "Socket o nombre de archivo inválido.\n");
        return;
    }

    FILE *archivo = fopen(nombre_archivo, "rb");
    if (!archivo)
    {
        perror("No se pudo abrir el archivo");
        return;
    }

    // Obtener tamaño del archivo
    fseek(archivo, 0, SEEK_END);
    int tam_archivo = ftell(archivo);
    rewind(archivo);

    // Armar estructura
    Datos_archivo info;
    strncpy(info.nombre_archivo, nombre_archivo, MAX_TAM_NOMBRE_ARCHIVO);
    info.nombre_archivo[MAX_TAM_NOMBRE_ARCHIVO - 1] = '\0'; // Asegura terminación
    info.tamano_archivo = tam_archivo;

    // Enviar estructura al cliente
    if (send(socket, &info, sizeof(info), 0) < 0)
    {
        perror("Error al enviar metadatos del archivo");
        fclose(archivo);
        return;
    }
    // Enviar el contenido en bloques
    char buffer[TAM_PAQUETE];
    size_t leidos;
    while ((leidos = fread(buffer, 1, TAM_PAQUETE, archivo)) > 0)
    {
        if (send(socket, buffer, leidos, 0) < 0)
        {
            perror("Error al enviar datos del archivo");
            break;
        }
    }

    fclose(archivo);
    printf("Archivo '%s' enviado con éxito.\n", nombre_archivo);
}
void recibir_archivo(int socket)
{
    if (socket < 0)
    {
        fprintf(stderr, "Socket inválido.\n");
        return;
    }

    // Recibir los metadatos
    Datos_archivo info;
    int recibidos = recv(socket, &info, sizeof(info), 0);
    if (recibidos <= 0)
    {
        perror("Error al recibir metadatos del archivo");
        return;
    }

    printf("Recibiendo archivo '%s' de %d bytes...\n", info.nombre_archivo, info.tamano_archivo);

    // Abrir archivo con el nombre original
    FILE *archivo = fopen(info.nombre_archivo, "wb");
    if (!archivo)
    {
        perror("No se pudo crear archivo local");
        return;
    }

    // Recibir el archivo en bloques
    char buffer[TAM_PAQUETE];
    int total_recibido = 0;
    while (total_recibido < info.tamano_archivo)
    {
        int restante = info.tamano_archivo - total_recibido;
        int a_recibir = (restante < TAM_PAQUETE) ? restante : TAM_PAQUETE;

        int bytes = recv(socket, buffer, a_recibir, 0);
        if (bytes <= 0)
        {
            perror("Error al recibir datos del archivo");
            break;
        }

        fwrite(buffer, 1, bytes, archivo);
        total_recibido += bytes;
    }

    fclose(archivo);
    printf("Archivo '%s' recibido correctamente.\n", info.nombre_archivo);
}

// Hilo que escucha mensajes entrantes en un chat activo
void *escuchar_chat(void *arg)
{
    ConexionChat *conexion = (ConexionChat *)arg;
    char buffer[TAM_PAQUETE]; // Buffer para almacenar mensajes

    while (1)
    {
        int bytes = recv(conexion->socket, buffer, TAM_PAQUETE, 0); // Espera mensajes del otro cliente
        if (bytes <= 0)
        {
            printf("\33[2K\rSe cerro la conexion de chat con %s:%d.\n> ",
                   inet_ntoa(conexion->addr.sin_addr),
                   ntohs(conexion->addr.sin_port));
            fflush(stdout);

            close(conexion->socket); // Cierra el socket
            free(conexion);          // Libera la memoria asignada a la estructura

            // Protegemos el acceso a la variable global con un mutex
            pthread_mutex_lock(&chat_mutex);
            if (conexion_chat_actual == conexion)
            {
                conexion_chat_actual = NULL;
            }
            pthread_mutex_unlock(&chat_mutex);

            pthread_exit(NULL); // Finaliza el hilo
        }
        if (strncmp(buffer, "/archivo ", 9) == 0)
        {
            recibir_archivo(conexion->socket);
        }
        else
        {
            printf("\33[2K\r[%s]: %s\n> ", conexion->nombre, buffer);
            fflush(stdout);
        }
        // buffer[0] = '\0';
        bzero(buffer, sizeof(buffer));
    }
    return NULL;
}

// Crea un socket que escucha conexiones entrantes en el puerto indicado
int crear_socket_escucha(int puerto)
{
    int escucha_socket = socket(AF_INET, SOCK_STREAM, 0); // Crea socket TCP
    if (escucha_socket < 0)
    {
        perror("socket escucha");
        exit(1);
    }

    struct sockaddr_in escucha_addr;
    memset(&escucha_addr, 0, sizeof(escucha_addr)); // Limpia la estructura
    escucha_addr.sin_family = AF_INET;
    escucha_addr.sin_port = htons(puerto);     // Convierte a formato de red
    escucha_addr.sin_addr.s_addr = INADDR_ANY; // Acepta conexiones de cualquier IP

    int opt = 1;
    if (setsockopt(escucha_socket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt) < 0)) {
        perror("setsocketopt");
        exit(EXIT_FAILURE);
    }
    if (bind(escucha_socket, (struct sockaddr *)&escucha_addr, sizeof(escucha_addr)) < 0)
    {
        perror("bind escucha");
        exit(1);
    }

    if (listen(escucha_socket, 5) < 0)
    {
        perror("listen escucha");
        exit(1);
    }

    return escucha_socket; // Devuelve el descriptor del socket
}

// Maneja una conexion entrante desde otro cliente
void iniciar_conexion_entrante(int escucha_socket)
{
    struct sockaddr_in cliente_addr;
    socklen_t addr_len = sizeof(cliente_addr);

    int nuevo_socket = accept(escucha_socket, (struct sockaddr *)&cliente_addr, &addr_len); // Acepta nueva conexion
    if (nuevo_socket < 0)
    {
        perror("accept");
        return;
    }

    char nombre_entrante[TAM_PAQUETE_NOMBRE];
    int bytes = recv(nuevo_socket, nombre_entrante, TAM_PAQUETE_NOMBRE, 0);

    if (bytes < 0)
    {
        perror("recv nombre");
    }
    else
    {
        char nombre_auxiliar[MAX_TAM_NOMBRE_USUARIO];

        if (strncmp(nombre_entrante, "/nombre ", 8) == 0)
        {
            sscanf(nombre_entrante + 8, "%s", nombre_auxiliar);

            // snprintf(mensaje, sizeof(mensaje), "/nombre %s\n", nombre_auxiliar);
        }
        printf("\33[2K\rCliente se conecto para chatear: %s:%d\n> ",
               inet_ntoa(cliente_addr.sin_addr), ntohs(cliente_addr.sin_port));
        fflush(stdout);

        // Crea una nueva estructura para manejar la conexion
        ConexionChat *conexion = malloc(sizeof(ConexionChat));
        conexion->socket = nuevo_socket;
        conexion->addr = cliente_addr;
        strncpy(conexion->nombre, nombre_auxiliar, MAX_TAM_NOMBRE_USUARIO);

        // Guarda la conexion actual como la activa
        pthread_mutex_lock(&chat_mutex);
        socket_chat_activo = nuevo_socket;
        conexion_chat_actual = conexion;
        pthread_mutex_unlock(&chat_mutex);

        // Crea un hilo para escuchar mensajes de esta conexion
        pthread_t thread;
        pthread_create(&thread, NULL, escuchar_chat, conexion);
        pthread_detach(thread); // No se espera su finalizacion
    }
}

// Inicia una conexion de salida a otro cliente
void iniciar_conexion_salida(char *ip, int puerto, const char *nombre_receptor)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0); // Crea socket TCP
    struct sockaddr_in destino;
    memset(&destino, 0, sizeof(destino));
    printf("\33[2K\rIntentando conectar a %s:%d...\n> ", ip, puerto);
    fflush(stdout);

    destino.sin_family = AF_INET;
    destino.sin_port = htons(puerto);
    inet_pton(AF_INET, ip, &destino.sin_addr); // Convierte la IP a binario

    if (connect(sock, (struct sockaddr *)&destino, sizeof(destino)) < 0)
    {
        perror("connect");
        return;
    }

    char nombre[TAM_PAQUETE_NOMBRE];
    snprintf(nombre, sizeof(nombre), "/nombre %s\n", nombre_personal);
    send(sock, nombre, TAM_PAQUETE_NOMBRE, 0);

    printf("\33[2K\rConectado a %s:%d\n> ", ip, puerto);
    fflush(stdout);

    // Asigna la nueva conexion como activa
    ConexionChat *conexion = malloc(sizeof(ConexionChat));
    conexion->socket = sock;
    conexion->addr = destino;
    strncpy(conexion->nombre, nombre_receptor, MAX_TAM_NOMBRE_USUARIO);

    pthread_mutex_lock(&chat_mutex);
    conexion_chat_actual = conexion;
    pthread_mutex_unlock(&chat_mutex);

    // Inicia hilo para escuchar mensajes del otro cliente
    pthread_t thread;
    pthread_create(&thread, NULL, escuchar_chat, conexion);
    pthread_detach(thread);
}
#define TAM_MAX_MENSAJE 1024

void enviar_con_prefijo(int servidor_socket, const char *linea)
{
    const char *prefijo = "/msg ";
    size_t largo_prefijo = strlen(prefijo);
    size_t largo_mensaje = strlen(linea);
    size_t total = largo_prefijo + largo_mensaje;

    // Reservamos buffer suficiente para concatenar
    char *mensaje_completo = malloc(total + 1);
    if (!mensaje_completo)
    {
        perror("malloc");
        return;
    }

    // Concatenamos "/msg " + linea
    strcpy(mensaje_completo, prefijo);
    strcat(mensaje_completo, linea);

    // Enviamos en fragmentos de hasta 1024 bytes
    size_t enviados = 0;
    while (enviados < total)
    {
        size_t por_enviar = TAM_MAX_MENSAJE;
        if (total - enviados < TAM_MAX_MENSAJE)
            por_enviar = total - enviados;

        ssize_t bytes = send(servidor_socket, mensaje_completo + enviados, por_enviar, 0);
        if (bytes < 0)
        {
            perror("send");
            break;
        }
        enviados += bytes;
    }

    free(mensaje_completo);
}

int main(int argc, char *argv[])
{
    if (argc != 4)
    {
        printf("Uso: %s <ip_server> <puerto_server> <puerto_escucha>\n", argv[0]);
        return 1;
    }

    char *ip_server = argv[1];
    int puerto_server = atoi(argv[2]);
    int puerto_escucha = atoi(argv[3]);

    // Conecta con el servidor central
    int servidor_socket = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in servidor_addr;
    memset(&servidor_addr, 0, sizeof(servidor_addr));
    servidor_addr.sin_family = AF_INET;
    servidor_addr.sin_port = htons(puerto_server);
    inet_pton(AF_INET, ip_server, &servidor_addr.sin_addr);

    if (connect(servidor_socket, (struct sockaddr *)&servidor_addr, sizeof(servidor_addr)) < 0)
    {
        perror("connect servidor");
        return 1;
    }

    int disponible = 0;
    while (!disponible)
    {
        printf("\33[2K\rIngrese su nombre con el que lo reconocerán otros usuarios: \n> ");
        fflush(stdout);

        scanf("%31s", nombre_personal); // Leemos hasta 99 caracteres (dejamos 1 para el '\0')

        char mensaje_nombre[MAX_TAM_PAQUETE_NOMBRE_USUARIO];
        snprintf(mensaje_nombre, sizeof(mensaje_nombre), "/nombre %s\n", nombre_personal);
        send(servidor_socket, mensaje_nombre, strlen(mensaje_nombre), 0);

        char buffer[BUFFER_SIZE];
        int bytes = recv(servidor_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes > 0)
        {
            buffer[bytes] = '\0'; // Aseguramos que sea una cadena válida
            // printf("DEBUG: recibido [%s]\n", buffer);

            if (strncmp(buffer, "/error", 6) == 0)
            {
                printf("\33[2K\r\nNombre rechazado: %s ,por favor ingrese otro nombre\n> ", buffer + 7); // Muestra el mensaje de error
                fflush(stdout);
            }
            else if (strncmp(buffer, "/ok", 3) == 0)
            {
                printf("\33[2K\r\nNombre aceptado.\n> ");
                fflush(stdout);

                disponible = 1; // Salís del bucle
            }
            else
            {
                printf("\33[2K\r\nRespuesta desconocida del servidor: %s\n> ", buffer);
                fflush(stdout);
            }
        }
        else
        {
            perror("Error al recibir respuesta del servidor");
            break;
        }
    }

    // Envia al servidor el puerto en el que este cliente estara escuchando
    char mensaje[64];
    snprintf(mensaje, sizeof(mensaje), "/puerto %d\n", puerto_escucha);
    send(servidor_socket, mensaje, strlen(mensaje), 0);

    // Crea el socket de escucha para otros clientes
    int escucha_socket = crear_socket_escucha(puerto_escucha);

    printf("\33[2K\rCliente iniciado. Esperando conexiones en puerto %d...\n> ", puerto_escucha);
    fflush(stdout);

    // Prepara para monitorear multiples entradas con select()
    fd_set read_fds;

    // ojo que deberia ser el mas grande de todos
    int fdmax = (escucha_socket > servidor_socket) ? escucha_socket : servidor_socket;

    while (1)
    {
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);    // Entrada estandar (teclado)
        FD_SET(servidor_socket, &read_fds); // Servidor central
        FD_SET(escucha_socket, &read_fds);  // Clientes entrantes

        if (select(fdmax + 1, &read_fds, NULL, NULL, NULL) == -1)
        {
            perror("select");
            exit(1);
        }

        // Entrada del usuario
        if (FD_ISSET(STDIN_FILENO, &read_fds))
        {
            char linea[BUFFER_SIZE];
            fgets(linea, sizeof(linea), stdin); // Lee comando o mensaje del usuario
            printf("\33[2K\r\n> ");
            fflush(stdout);
            size_t len = strlen(linea);
            if (len > 0 && linea[len - 1] == '\n')
            {
                linea[len - 1] = '\0';
            }
            if (strncmp(linea, "/c ", 3) == 0)
            {
                char mensaje[64];

                sscanf(linea + 3, "%31s", ultimo_nombre_receptor); // guarda el nombre ingresado

                snprintf(mensaje, sizeof(mensaje), "/c %s\n", ultimo_nombre_receptor);
                send(servidor_socket, mensaje, strlen(mensaje), 0);
            }
            else if (strcmp(linea, "/info") == 0)
            {
                send(servidor_socket, linea, strlen(linea), 0);
            }
            else
            {
                pthread_mutex_lock(&chat_mutex);
                if (conexion_chat_actual != NULL) // Si hay una conexion de chat activa, envia alli
                {
                    if (strncmp(linea, "/archivo ", 9) == 0) // ✅ BIEN
                    {
                        send(conexion_chat_actual->socket, linea, strlen(linea), 0);
                        char nombre_archivo[256];                   // Asegurate de que sea suficientemente grande
                        sscanf(linea + 9, "%255s", nombre_archivo); // Extrae el nombre (sin espacios)
                        enviar_archivo(conexion_chat_actual->socket, nombre_archivo);
                    }
                    else
                    {
                        // enviar_con_prefijo(servidor_socket, linea);

                        send(conexion_chat_actual->socket, linea, strlen(linea), 0);
                    }
                }
                else
                {
                    // Si no, envia al servidor
                    send(servidor_socket, linea, strlen(linea), 0);
                }
                pthread_mutex_unlock(&chat_mutex);
            }
            linea[0] = '\0';
        }

        // Respuesta del servidor
        if (FD_ISSET(servidor_socket, &read_fds))
        {
            char buffer[BUFFER_SIZE];
            int bytes = recv(servidor_socket, buffer, sizeof(buffer) - 1, 0);
            if (bytes <= 0)
            {
                printf("\33[2K\rDesconectado del servidor.\n> ");
                fflush(stdout);

                break;
            }
            buffer[bytes] = '\0';

            if (strncmp(buffer, "/conectar ", 10) == 0)
            {
                // Si el servidor pide que conecte a un cliente
                char ip[INET_ADDRSTRLEN];
                int puerto;
                sscanf(buffer + 10, "%s %d", ip, &puerto);
                iniciar_conexion_salida(ip, puerto, ultimo_nombre_receptor);
            }
            else
            {
                // Mensaje comun del servidor
                printf("\33[2K\r[Servidor] %s\n> ", buffer);
                fflush(stdout);
            }
        }

        // Nueva conexion entrante de otro cliente
        if (FD_ISSET(escucha_socket, &read_fds))
        {
            iniciar_conexion_entrante(escucha_socket);
        }
    }

    close(servidor_socket);
    close(escucha_socket);
    return 0;
}
