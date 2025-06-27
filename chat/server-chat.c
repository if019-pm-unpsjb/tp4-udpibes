#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>
#include <sys/select.h>
#include "constantes.h"
#include <asm-generic/socket.h>

// Estructura que representa un cliente conectado
typedef struct
{
    int socket;              // Descriptor de socket del cliente
    struct sockaddr_in addr; // Direccion IP y puerto del cliente
    int puerto_escucha;      // este es el puerto en el que el cliente escucha
    char nombre_usuario[MAX_TAM_NOMBRE_USUARIO];
} Cliente;

Cliente clientes[MAX_CLIENTES]; // Arreglo que almacena los clientes conectados
int numero_clientes = 0;        // Contador actual de clientes conectados

// Funcion que envia al cliente la lista de IPs y puertos conectados
void enviar_lista_usuarios(int cliente_socket)
{
    char lista[TAM_PAQUETE] = "Usuarios conectados:\n";
    int cantidad = 0;

    for (int i = 0; i < numero_clientes; i++)
    {
        if (clientes[i].socket == cliente_socket)
            continue; // Saltear al usuario que hizo la solicitud

        if (clientes[i].nombre_usuario[0] == '\0')
            continue; // Saltear al usuario con nombre vacío

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(clientes[i].addr.sin_addr), ip, INET_ADDRSTRLEN);
        int puerto = clientes[i].puerto_escucha;

        char entrada[128];
        snprintf(entrada, sizeof(entrada), "%s (%s:%d)\n", clientes[i].nombre_usuario, ip, puerto);
        strcat(lista, entrada);
        cantidad++;
    }

    if (cantidad == 0)
    {
        char aviso[] = "No hay otros usuarios conectados.\n";
        send(cliente_socket, aviso, strlen(aviso), 0);
    }
    else
    {
        send(cliente_socket, lista, strlen(lista), 0);
    }
}

// Agrega un nuevo cliente al arreglo de clientes conectados
void agregar_cliente(int nuevo_socket, struct sockaddr_in addr)
{
    if (numero_clientes >= MAX_CLIENTES)
    {
        printf("Maximo de clientes alcanzado.\n");
        close(nuevo_socket);
        return;
    }
    // Guarda el socket del cliente, direccion y aumenta contador
    clientes[numero_clientes].socket = nuevo_socket;
    clientes[numero_clientes].addr = addr;
    numero_clientes++;
}

// Elimina un cliente del arreglo dado su indice
void eliminar_cliente(int indice)
{
    close(clientes[indice].socket);                    // Cierra el socket del cliente
    for (int i = indice; i < numero_clientes - 1; i++) // Desplaza los demas
    {
        clientes[i] = clientes[i + 1];
    }
    numero_clientes--;
}

int nombre_usuario_existe(const char *nombre)
{
    for (int i = 0; i < numero_clientes; i++)
    {
        if (strcmp(clientes[i].nombre_usuario, nombre) == 0)
        {
            return 1; // Ya existe
        }
    }
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        printf("Uso: %s <puerto>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int puerto = atoi(argv[1]);
    int servidor_socket = socket(AF_INET, SOCK_STREAM, 0); // Crea socket tcp
    if (servidor_socket < 0)
    {
        perror("Socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in servidor_addr;                 // Estructura para la direccion del servidor
    memset(&servidor_addr, 0, sizeof(servidor_addr)); // Inicializa todo en 0
    servidor_addr.sin_family = AF_INET;
    servidor_addr.sin_port = htons(puerto);     // Asigna el puerto que se paso com o parametro
    servidor_addr.sin_addr.s_addr = INADDR_ANY; // Acepta conexiones de cualquier ip

    int opt = 1;
    if (setsockopt(servidor_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("setsocketopt");
        exit(EXIT_FAILURE);
    }
    if (bind(servidor_socket, (struct sockaddr *)&servidor_addr, sizeof(servidor_addr)) < 0)
    {
        perror("Bind");
        exit(EXIT_FAILURE);
    }

    listen(servidor_socket, MAX_CLIENTES); // Comienza a escuchar conexiones entrantes
    printf("Servidor escuchando en puerto %d...\n", puerto);

    fd_set master_set, read_fds;
    int fdmax = servidor_socket;

    FD_ZERO(&master_set);
    FD_SET(servidor_socket, &master_set);

    while (1)
    {
        read_fds = master_set;

        if (select(fdmax + 1, &read_fds, NULL, NULL, NULL) == -1)
        {
            perror("Select");
            exit(EXIT_FAILURE);
        }

        for (int i = 0; i <= fdmax; i++) // Recorre todos los posibles fd
        {
            if (FD_ISSET(i, &read_fds)) // Si hay actividad en este fd
            {
                if (i == servidor_socket) // Si es el socket del servidor: nueva conexion
                {
                    struct sockaddr_in cliente_addr;
                    socklen_t addrlen = sizeof(cliente_addr);
                    int nuevo_socket = accept(servidor_socket, (struct sockaddr *)&cliente_addr, &addrlen); // Acepta nueva conexion
                    if (nuevo_socket == -1)
                    {
                        perror("Accept");
                    }
                    else
                    {
                        FD_SET(nuevo_socket, &master_set); // Añade nuevo socket al conjunto
                        if (nuevo_socket > fdmax)
                            fdmax = nuevo_socket; // Actualiza el maximo descriptor

                        agregar_cliente(nuevo_socket, cliente_addr); // Añade el cliente a la lista
                        // enviar_lista_ips(nuevo_socket);              // Envia la lista de ips al nuevo cliente

                        // Muestra informacion del nuevo cliente
                        printf("Nuevo cliente conectado: IP: %s , Puerto: %d\n", inet_ntoa(cliente_addr.sin_addr), ntohs(cliente_addr.sin_port));
                    }
                }
                else // Si no es el socket del servidor, es un cliente enviando datos
                {
                    char buffer[TAM_PAQUETE];
                    size_t len = strlen(buffer);
                    if (len > 0 && buffer[len - 1] == '\n')
                        buffer[len - 1] = '\0';
                    if (len > 0 && buffer[len - 1] == '\r')
                        buffer[len - 1] = '\0';
                    int bytes_recibidos = recv(i, buffer, TAM_PAQUETE, 0); // Recibe datos del cliente
                    if (bytes_recibidos <= 0)                              // Si se desconecto o hubo error
                    {
                        for (int j = 0; j < numero_clientes; j++)
                        {
                            if (clientes[j].socket == i) // Encuentra el cliente desconectado
                            {
                                printf("Cliente desconectado: %s\n", inet_ntoa(clientes[j].addr.sin_addr));
                                eliminar_cliente(j); // Elimina de la lista
                                break;
                            }
                        }
                        FD_CLR(i, &master_set); // Lo saca del conjunto de select()
                    }
                    else // Se recibieron datos correctamente
                    {
                        buffer[bytes_recibidos] = '\0'; // Agrega /0
                        printf("Comando recibido de socket %d: %s\n", i, buffer);
                        printf("Bytes recibidos: %d\n", bytes_recibidos);
                        printf("Comando recibido de socket %d: [%s]\n", i, buffer);

                        if (strncmp(buffer, "/nombre ", 8) == 0)
                        {
                            char nombre[MAX_TAM_NOMBRE_USUARIO];
                            sscanf(buffer + 8, "%s", nombre);
                            char msg[MAX_RESPUESTA_SERVIDOR_NOMBRE];
                            if (nombre_usuario_existe(nombre))
                            {
                                strcpy(msg, "/error\n");
                                send(i, msg, MAX_RESPUESTA_SERVIDOR_NOMBRE, 0);
                            }
                            else
                            {
                                strcpy(msg, "/ok\n");   
                                send(i, msg, MAX_RESPUESTA_SERVIDOR_NOMBRE, 0);
                                for (int j = 0; j < numero_clientes; j++)
                                {
                                    if (clientes[j].socket == i)
                                    {
                                        strncpy(clientes[j].nombre_usuario, nombre, MAX_TAM_NOMBRE_USUARIO - 1);
                                        clientes[j].nombre_usuario[MAX_TAM_NOMBRE_USUARIO - 1] = '\0';
                                        enviar_lista_usuarios(i);
                                        break;
                                    }
                                }
                            }
                        }
                        else if (strncmp(buffer, "/puerto ", 8) == 0)
                        {
                            int puerto_escucha;
                            sscanf(buffer + 8, "%d", &puerto_escucha);

                            for (int j = 0; j < numero_clientes; j++)
                            {
                                if (clientes[j].socket == i)
                                {
                                    clientes[j].puerto_escucha = puerto_escucha;
                                    printf("Cliente %s:%d escucha en puerto %d\n",
                                           inet_ntoa(clientes[j].addr.sin_addr),
                                           clientes[j].addr.sin_port,
                                           puerto_escucha);
                                    break;
                                }
                            }
                        }

                        else if (strncmp(buffer, "/c ", 3) == 0)
                        {
                            char nombre_destino[MAX_TAM_NOMBRE_USUARIO];
                            sscanf(buffer + 3, "%s", nombre_destino);

                            int idx_emisor = -1;
                            for (int j = 0; j < numero_clientes; j++)
                            {
                                if (clientes[j].socket == i)
                                {
                                    idx_emisor = j;
                                    break;
                                }
                            }

                            if (strcmp(nombre_destino, "@allall") == 0)
                            {
                                // Modo difusión
                                for (int j = 0; j < numero_clientes; j++)
                                {
                                    if (j != idx_emisor)
                                    {
                                        char msg[TAM_PAQUETE];
                                        snprintf(msg, sizeof(msg), "/conectar %s %d %s\n",
                                                 inet_ntoa(clientes[j].addr.sin_addr),
                                                 clientes[j].puerto_escucha, clientes[j].nombre_usuario);
                                        send(clientes[idx_emisor].socket, msg, TAM_PAQUETE, 0);
                                    }
                                }
                                printf("Cliente %s pidió conexión con todos\n", clientes[idx_emisor].nombre_usuario);
                            }
                            else
                            {
                                // Conexión normal 1 a 1
                                int idx_receptor = -1;
                                for (int j = 0; j < numero_clientes; j++)
                                {
                                    if (strcmp(clientes[j].nombre_usuario, nombre_destino) == 0)
                                    {
                                        idx_receptor = j;
                                        break;
                                    }
                                }

                                if (idx_receptor != -1 && idx_emisor != -1)
                                {
                                    char msg[TAM_PAQUETE];
                                    snprintf(msg, sizeof(msg), "/conectar %s %d %s\n",
                                             inet_ntoa(clientes[idx_receptor].addr.sin_addr),
                                             clientes[idx_receptor].puerto_escucha, clientes[idx_receptor].nombre_usuario);
                                    send(clientes[idx_emisor].socket, msg, TAM_PAQUETE, 0);

                                    printf("Le dije a %s que se conecte con %s\n",
                                           clientes[idx_emisor].nombre_usuario,
                                           clientes[idx_receptor].nombre_usuario);
                                }
                            }
                        }
                        else if (strncmp(buffer, "/info", 5) == 0)
                        {
                            enviar_lista_usuarios(i);
                        }
                        else
                        {
                            char msg[] = "Comando no reconocido.\n";
                            send(i, msg, TAM_PAQUETE, 0);
                        }
                    }
                }
            }
        }
    }

    return 0;
}