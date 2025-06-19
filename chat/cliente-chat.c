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
#include "./imprimir/imprimir_mensaje.h"

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

ConexionChat usuarios_conectado[MAX_USUARIOS_CONECTADOS];
int cantidad_usuarios_conectados = 0;

void eliminar_conexion_por_socket(ConexionChat usuarios[], int socket_objetivo)
{
    for (int i = 0; i < cantidad_usuarios_conectados; i++)
    {
        if (usuarios[i].socket == socket_objetivo)
        {
            // Mover elementos hacia la izquierda
            for (int j = i; j < cantidad_usuarios_conectados; j++)
            {
                usuarios[j] = usuarios[j + 1];
            }
            break;
        }
    }
    memset(&usuarios_conectado[cantidad_usuarios_conectados], 0, sizeof(ConexionChat));

    cantidad_usuarios_conectados--;
}

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
    if (send(socket, &info, TAM_PAQUETE, 0) < 0)
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
        if (send(socket, buffer, TAM_PAQUETE, 0) < 0)
        {
            perror("Error al enviar datos del archivo");
            break;
        }
    }

    fclose(archivo);
    imprimirMensaje("Archivo '%s' enviado con éxito.", 1, nombre_archivo);
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
    int recibidos = recv(socket, &info, TAM_PAQUETE, 0);
    if (recibidos <= 0)
    {
        perror("Error al recibir metadatos del archivo");
        return;
    }

    imprimirMensaje("Recibiendo archivo '%s' de %d bytes, enviado por %s", 1, info.nombre_archivo, info.tamano_archivo, conexion_chat_actual->nombre);
    char nombre_archivo[MAX_TAM_NOMBRE_ARCHIVO + MAX_TAM_NOMBRE_USUARIO];
    snprintf(nombre_archivo, sizeof(nombre_archivo), "%s_%s", conexion_chat_actual->nombre, info.nombre_archivo);

    // Abrir archivo con el nombre original
    FILE *archivo = fopen(nombre_archivo, "wb");
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
        // int restante = info.tamano_archivo - total_recibido;
        // int a_recibir = (restante < TAM_PAQUETE) ? restante : TAM_PAQUETE;

        int bytes = recv(socket, buffer, TAM_PAQUETE, 0);
        if (bytes <= 0)
        {
            perror("Error al recibir datos del archivo");
            break;
        }

        fwrite(buffer, 1, bytes, archivo);
        total_recibido += bytes;
    }

    fclose(archivo);
    imprimirMensaje("Archivo '%s' recibido correctamente.", 1, info.nombre_archivo);
}

// Hilo que escucha mensajes entrantes en un chat activo
void *escuchar_chat(void *arg)
{
    ConexionChat *conexion = (ConexionChat *)arg;
    char buffer[TAM_PAQUETE]; // Buffer para almacenar mensajes

    while (1)
    {
        int bytes = recv(conexion->socket, buffer, TAM_PAQUETE, 0); // Espera mensajes del otro cliente
        buffer[bytes] = '\0';                                       // <- Muy importante para evitar basura y residuos
        if (bytes <= 0)
        {
            imprimirMensaje("Se cerro la conexion de chat con %s. ", 1, conexion->nombre);
            eliminar_conexion_por_socket(usuarios_conectado, conexion->socket);

            // Reducís el contador total
            memset(&usuarios_conectado[cantidad_usuarios_conectados], 0, sizeof(ConexionChat));

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
            imprimirMensaje("[%s]: %s ", 1, conexion->nombre, buffer);
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
    if (setsockopt(escucha_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
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
        imprimirMensaje("Cliente se conecto para chatear: %s ", 1, nombre_auxiliar);

        // Crea una nueva estructura para manejar la conexion
        ConexionChat *conexion = malloc(sizeof(ConexionChat));
        conexion->socket = nuevo_socket;
        conexion->addr = cliente_addr;
        strncpy(conexion->nombre, nombre_auxiliar, MAX_TAM_NOMBRE_USUARIO);
        usuarios_conectado[cantidad_usuarios_conectados] = *conexion;
        cantidad_usuarios_conectados++;

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
    imprimirMensaje("Intentando conectar a %s...\n ", 1, nombre_receptor);

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

    imprimirMensaje("Conectado a %s ", 1, nombre_receptor);

    // Asigna la nueva conexion como activa
    ConexionChat *conexion = malloc(sizeof(ConexionChat));
    conexion->socket = sock;
    conexion->addr = destino;
    strncpy(conexion->nombre, nombre_receptor, MAX_TAM_NOMBRE_USUARIO);
    usuarios_conectado[cantidad_usuarios_conectados] = *conexion;

    cantidad_usuarios_conectados++;

    pthread_mutex_lock(&chat_mutex);
    conexion_chat_actual = conexion;
    pthread_mutex_unlock(&chat_mutex);

    // Inicia hilo para escuchar mensajes del otro cliente
    pthread_t thread;
    pthread_create(&thread, NULL, escuchar_chat, conexion);
    pthread_detach(thread);
}

void enviar_mensaje_o_archivo(char linea[BUFFER_SIZE], ConexionChat conexion)
{
    if (strncmp(linea, "/archivo ", 9) == 0) // ✅ BIEN
    {
        send(conexion.socket, linea, TAM_PAQUETE, 0);
        char nombre_archivo[256];                   // Asegurate de que sea suficientemente grande
        sscanf(linea + 9, "%255s", nombre_archivo); // Extrae el nombre (sin espacios)
        enviar_archivo(conexion.socket, nombre_archivo);
    }
    else
    {
        send(conexion.socket, linea, TAM_PAQUETE, 0);
    }
}

bool existe_conexion(char nombre[MAX_TAM_NOMBRE_USUARIO])
{
    for (int i = 0; i < cantidad_usuarios_conectados; i++)
    {
        if (strcmp(usuarios_conectado[i].nombre, nombre) == 0)
        {
            return true;
        }
    }
    return false;
}
bool existe_conexion_ip_puerto(const char *ip, int puerto)
{
    for (int i = 0; i < cantidad_usuarios_conectados; i++)
    {
        char ip_existente[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(usuarios_conectado[i].addr.sin_addr), ip_existente, INET_ADDRSTRLEN);

        int puerto_existente = ntohs(usuarios_conectado[i].addr.sin_port);
        if (strcmp(ip, ip_existente) == 0 && puerto == puerto_existente)
        {
            return true;
        }
    }
    return false;
}

void conexion_si_existe(char nombre[MAX_TAM_NOMBRE_USUARIO])
{
    for (int i = 0; i < cantidad_usuarios_conectados; i++)
    {
        if (strcmp(usuarios_conectado[i].nombre, nombre) == 0)
        {
            conexion_chat_actual = &usuarios_conectado[i];
            return;
        }
    }
    return;
}

int main(int argc, char *argv[])
{
    if (argc != 4)
    {
        imprimirMensaje("Uso: %s <ip_server> <puerto_server> <puerto_escucha>", 1, argv[0]);
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
        exit(EXIT_FAILURE);
    }

    int disponible = 0;
    while (!disponible)
    {
        imprimirMensaje("Ingrese el nombre con el que lo reconocerán otros usuarios: \n ", 1);

        scanf("%31s", nombre_personal); // Leemos hasta 99 caracteres (dejamos 1 para el '\0')

        char mensaje_nombre[MAX_TAM_PAQUETE_NOMBRE_USUARIO];
        snprintf(mensaje_nombre, sizeof(mensaje_nombre), "/nombre %s\n", nombre_personal);
        send(servidor_socket, mensaje_nombre, MAX_TAM_PAQUETE_NOMBRE_USUARIO, 0);

        char buffer[BUFFER_SIZE];
        int bytes = recv(servidor_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes > 0)
        {
            buffer[bytes] = '\0'; // Aseguramos que sea una cadena válida
            // printf("DEBUG: recibido [%s]\n", buffer);

            if (strncmp(buffer, "/error", 6) == 0)
            {
                imprimirMensaje("\nNombre rechazado, por favor ingrese otro nombre\n ", 1); // Muestra el mensaje de error
            }
            else if (strncmp(buffer, "/ok", 3) == 0)
            {
                imprimirMensaje("Nombre aceptado. ", 1);

                disponible = 1; // Salís del bucle
            }
            else
            {
                imprimirMensaje("Respuesta desconocida del servidor: %s ", 1, buffer);
            }
        }
        else
        {
            perror("Error al recibir respuesta del servidor");
            exit(EXIT_FAILURE);
        }
    }

    imprimirMensaje("\nComandos posibles:\n\
>/info -> lista todos los usuarios disponibles\n\
>/c nombre_usuario -> establece una conexion con el usuario que tenga ese nombre\n\
>/c @all -> envia mensaje a todos los usuarios con los que se ha conectado previamente\n\
>/c @allall -> envia mensaje a todos los usuarios conectados en el servidor\n\
>/actual -> informa con quien estas chateando\n\
>/archivo nombre_archivo -> envia archivo al usuario con el que esta conectado\n\
 ",
                    1);

    // Envia al servidor el puerto en el que este cliente estara escuchando
    char mensaje[64];
    snprintf(mensaje, sizeof(mensaje), "/puerto %d\n", puerto_escucha);
    send(servidor_socket, mensaje, strlen(mensaje), 0);

    // Crea el socket de escucha para otros clientes
    int escucha_socket = crear_socket_escucha(puerto_escucha);

    imprimirMensaje("Cliente iniciado. Esperando conexiones en puerto %d...\n ", 1, puerto_escucha);

    // Prepara para monitorear multiples entradas con select()
    fd_set read_fds;

    // ojo que deberia ser el mas grande de todos
    int fdmax = (escucha_socket > servidor_socket) ? escucha_socket : servidor_socket;

    bool difusion = false;
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

            size_t len = strlen(linea);
            if (len > 0 && linea[len - 1] == '\n')
            {
                linea[len - 1] = '\0';
            }
            if ((strcmp(linea, "/c @all") == 0) || (strcmp(linea, "/c all") == 0))
            {
                difusion = true;
            }
            else if (strcmp(linea, "/c @allall") == 0)
            {
                // VER EL TEMA DE QUE LOS RECEPTORES NO MUESTRA SU NOMBRE. TRATE DE HACER QUE LE PREGUNTARA A LOS CLIENTES EL NOMBRE Y QUE RESPONDIERAN CON EL MISMO (SIN METER AL SERVER), PERO NO FUNCIONO Y ME PARECE UN BARDO
                difusion = true;
                ultimo_nombre_receptor[0] = '\0'; // array vacio porque no hay un unico receptor
                send(servidor_socket, linea, strlen(linea), 0);
            }
            else if (strncmp(linea, "/c ", 3) == 0)
            {
                char mensaje[TAM_PAQUETE];
                difusion = false;

                sscanf(linea + 3, "%31s", ultimo_nombre_receptor); // guarda el nombre ingresado
                if (existe_conexion(ultimo_nombre_receptor))
                {
                    conexion_si_existe(ultimo_nombre_receptor);
                }
                else
                {
                    snprintf(mensaje, sizeof(mensaje), "/c %s\n", ultimo_nombre_receptor);
                    send(servidor_socket, mensaje, strlen(mensaje), 0);
                }
            }
            else if (strcmp(linea, "/info") == 0)
            {
                send(servidor_socket, linea, strlen(linea), 0);
            }
            else if (strcmp(linea, "/actual") == 0)
            {
                pthread_mutex_lock(&chat_mutex);
                if (difusion)
                {
                    imprimirMensaje("Estas en modo difusion (@all) ", 1);
                }
                else if (conexion_chat_actual != NULL)
                {
                    imprimirMensaje("Estas chateando con: %s ", 1, conexion_chat_actual->nombre);
                }
                else
                {
                    imprimirMensaje("No hay chat activo\n ", 1);
                }
                pthread_mutex_unlock(&chat_mutex);
            }
            else
            {

                // FILTRO DE MENSAJES VACÍOS
                char *p = linea;
                while (*p == ' ')
                    p++;
                if (*p == '\0')
                {
                    // Si es vacío, sólo mostrá el prompt y salí
                    imprimirMensaje("", 1);
                    linea[0] = '\0';
                    pthread_mutex_unlock(&chat_mutex); // <-- soltar el mutex si ya lo tomaste
                    continue;                          // vuelve a pedir input
                }

                pthread_mutex_lock(&chat_mutex);

                if (conexion_chat_actual != NULL) // Si hay una conexion de chat activa, envia alli
                {
                    if (difusion)
                    {
                        for (int i = 0; i < cantidad_usuarios_conectados; i++)
                        {
                            enviar_mensaje_o_archivo(linea, usuarios_conectado[i]);
                        }
                    }
                    else
                    {
                        enviar_mensaje_o_archivo(linea, *conexion_chat_actual);
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
            imprimirMensaje("", 1);
        }

        // Respuesta del servidor
        if (FD_ISSET(servidor_socket, &read_fds))
        {
            char buffer[BUFFER_SIZE];
            int bytes = recv(servidor_socket, buffer, sizeof(buffer) - 1, 0);
            if (bytes <= 0)
            {
                imprimirMensaje("Desconectado del servidor. ", 1);

                break;
            }
            buffer[bytes] = '\0';

            if (strncmp(buffer, "/conectar ", 10) == 0)
            {
                // Si el servidor pide que conecte a un cliente
                char ip[INET_ADDRSTRLEN];
                char nombre[MAX_TAM_NOMBRE_USUARIO];
                int puerto;
                // sscanf(buffer + 10, "%s %d %s", ip, &puerto, nombre);
                sscanf(buffer + 10, "%15s %d %31s", ip, &puerto, nombre);

                if (!existe_conexion_ip_puerto(ip, puerto))
                {
                    iniciar_conexion_salida(ip, puerto, nombre);
                }
            }
            else
            {
                // Mensaje comun del servidor
                imprimirMensaje("[Servidor] %s ", 1, buffer);
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
