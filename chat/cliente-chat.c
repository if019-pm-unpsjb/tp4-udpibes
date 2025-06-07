#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "constantes.h"

#define BUFFER_SIZE 1024

void *escuchar_chat(void *arg)
{
    int chat_sock = *(int *)arg;
    char buffer[BUFFER_SIZE];

    while (1)
    {
        int len = recv(chat_sock, buffer, BUFFER_SIZE - 1, 0);
        if (len <= 0)
        {
            printf("Chat finalizado o error\n");
            break;
        }
        buffer[len] = '\0';
        printf("\n[Entrante]: %s\n", buffer);
    }

    close(chat_sock);
    free(arg);

    return NULL;
}
void *recibir_mensajes(void *arg)
{
    int chat_sock = *(int *)arg;
    char buffer[BUFFER_SIZE];

    while (1)
    {
        int len = recv(chat_sock, buffer, BUFFER_SIZE - 1, 0);
        if (len <= 0)
        {
            printf("\n[Conexión cerrada]\n");
            break;
        }
        buffer[len] = '\0';
        printf("\n[Entrante]: %s\n> ", buffer);
        fflush(stdout);
    }

    close(chat_sock);
    return NULL;
}

void *enviar_mensajes(void *arg)
{
    int chat_sock = *(int *)arg;
    char buffer[BUFFER_SIZE];

    while (1)
    {
        printf("> ");
        if (fgets(buffer, BUFFER_SIZE, stdin) == NULL)
            break;

        if (send(chat_sock, buffer, strlen(buffer), 0) <= 0)
        {
            printf("\n[Error al enviar mensaje o conexión cerrada]\n");
            break;
        }
    }

    close(chat_sock);
    return NULL;
}

void *escuchar_conexion(void *arg)
{
    int socket_server = *(int *)arg;
    free(arg);

    int sockfd;
    struct sockaddr_in servidor, cliente;
    socklen_t tam_cliente;

    // Crear socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("socket");
        pthread_exit(NULL);
    }

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    servidor.sin_family = AF_INET;
    servidor.sin_addr.s_addr = INADDR_ANY;
    servidor.sin_port = htons(0); // Puerto dinámico
    memset(&(servidor.sin_zero), 0, 8);

    if (bind(sockfd, (struct sockaddr *)&servidor, sizeof(servidor)) == -1)
    {
        perror("bind");
        close(sockfd);
        pthread_exit(NULL);
    }

    struct sockaddr_in dir_asignada;
    socklen_t len = sizeof(dir_asignada);
    if (getsockname(sockfd, (struct sockaddr *)&dir_asignada, &len) == -1)
    {
        perror("getsockname");
        close(sockfd);
        pthread_exit(NULL);
    }

    uint16_t puerto = ntohs(dir_asignada.sin_port);
    printf("Puerto asignado (host): %d\n", puerto);

    // Enviar al servidor principal
    char mensaje[64];
    snprintf(mensaje, sizeof(mensaje), "/puerto %d", puerto);
    send(socket_server, mensaje, strlen(mensaje), 0);

    printf("[*] Escuchando en puerto dinámico %d\n", puerto);

    if (listen(sockfd, 5) == -1)
    {
        perror("listen");
        close(sockfd);
        pthread_exit(NULL);
    }

    while (1)
    {
        tam_cliente = sizeof(struct sockaddr_in);
        int nueva_conexion = accept(sockfd, (struct sockaddr *)&cliente, &tam_cliente);
        if (nueva_conexion == -1)
        {
            perror("accept");
            continue;
        }

        printf("[+] Nueva conexión entrante desde %s:%d\n",
               inet_ntoa(cliente.sin_addr), ntohs(cliente.sin_port));

        int *sock_ptr1 = malloc(sizeof(int));
        int *sock_ptr2 = malloc(sizeof(int));
        *sock_ptr1 = nueva_conexion;
        *sock_ptr2 = nueva_conexion;

        pthread_t hilo_recv, hilo_send;
        pthread_create(&hilo_recv, NULL, recibir_mensajes, sock_ptr1);
        pthread_create(&hilo_send, NULL, enviar_mensajes, sock_ptr2);
    }

    close(sockfd);
    pthread_exit(NULL);
}

int recibir_mensaje(int sock, char *buffer)
{
    int len = recv(sock, buffer, BUFFER_SIZE - 1, 0);
    if (len <= 0)
        return -1;
    buffer[len] = '\0';
    return len;
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        fprintf(stderr, "Uso: %s <ip_servidor> <puerto_servidor>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // parametros son ip servidor y puerto servidor
    const char *server_ip = argv[1];
    int server_port = atoi(argv[2]);

    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];

    // Crear socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        perror("Socket");
        exit(EXIT_FAILURE);
    }

    // Configurar dirección del servidor
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    // server_addr.sin_addr.s_addr = INADDR_ANY;

    inet_pton(AF_INET, server_ip, &server_addr.sin_addr);

    // Conectar al servidor
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Conexión al servidor");
        exit(EXIT_FAILURE);
    }
    pthread_t hilo_escucha;
    int *ptr_socket = malloc(sizeof(int));
    *ptr_socket = sock;

    pthread_create(&hilo_escucha, NULL, escuchar_conexion, ptr_socket);

    printf("Conectado al servidor %s:%d\n", server_ip, server_port);

    // Recibir lista de clientes dada por el servidor
    int len = recv(sock, buffer, BUFFER_SIZE - 1, 0);
    if (len <= 0)
    {
        perror("Error al recibir lista");
        exit(EXIT_FAILURE);
    }
    buffer[len] = '\0';
    printf("Clientes disponibles:\n%s\n", buffer);

    // Pedir comando para chatear
    printf("Usá el comando: /c <ip> <port>\n> ");

    // aca me pierdo pero pide al usuario ip y puerto a la que se quiere conectar
    fgets(buffer, BUFFER_SIZE, stdin);

    // Parsear IP y puerto
    char ip[INET_ADDRSTRLEN];
    int puerto;
    if (sscanf(buffer, "/c %15s %d", ip, &puerto))
    {
        if (puerto > 0 && puerto <= 65535)
        {
            // Armar comando que espera el servidor
            char comando[BUFFER_SIZE];
            snprintf(comando, sizeof(comando), "/c %s:%d", ip, puerto);

            // 🔥 Enviar al servidor el comando reformateado
            send(sock, comando, strlen(comando), 0);
        }
        else
        {
            printf("Puerto inválido.\n");
        }
    }
    else
    {
        printf("Formato incorrecto. Uso: /c <ip> <port>\n");
    }

    // Esperar nueva conexión de chat desde el servidor o redirección

    // Conectarse al otro cliente con IP y puerto indicados
    int chat_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (chat_sock < 0)
    {
        perror("Socket de chat");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in chat_addr;
    chat_addr.sin_family = AF_INET;
    chat_addr.sin_port = htons(puerto);

    // no se que tan necesario es el .s_addr
    if (inet_pton(AF_INET, ip, &chat_addr.sin_addr.s_addr) <= 0)
    {
        fprintf(stderr, "IP inválida\n");
        exit(EXIT_FAILURE);
    }

    if (connect(chat_sock, (struct sockaddr *)&chat_addr, sizeof(chat_addr)) < 0)
    {
        perror("No se pudo conectar al cliente");
        close(chat_sock);
        exit(EXIT_FAILURE);
    }

    printf("Chat iniciado con %s:%d\n", ip, puerto);

    // Hilo para recibir mensajes del otro cliente
    pthread_t hilo_receptor;
    // este hilo unicamente unicamente recibe mensajes del otro cliente
    int *p_chat_sock = malloc(sizeof(int));
    *p_chat_sock = chat_sock;
    pthread_create(&hilo_receptor, NULL, escuchar_chat, p_chat_sock);

    // Enviar mensajes
    while (1)
    {
        printf("> ");
        fgets(buffer, BUFFER_SIZE, stdin);
        send(chat_sock, buffer, strlen(buffer), 0);
    }

    close(sock);
    return 0;
}
