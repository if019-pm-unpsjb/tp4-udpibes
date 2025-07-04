#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <unistd.h>
#include "../constantes.h"
#include <sys/time.h>
#include <ctype.h>
#include <signal.h>

void enviarMensajeError(int socket_udp, struct sockaddr_in direccion_cliente, int codigo_err, char *mensaje_error)
{

    char paquete_error[TAM_MAX_POSIBLE];
    uint16_t opcode_error = htons(ERROR);
    uint16_t codigo_error_htons = htons(codigo_err);
    size_t longitud_mensaje = strlen(mensaje_error);
    size_t tamanio_msg = 4 + longitud_mensaje + 1;

    memcpy(&paquete_error[0], &opcode_error, 2);
    memcpy(&paquete_error[2], &codigo_error_htons, 2);
    memcpy(&paquete_error[4], mensaje_error, longitud_mensaje);
    paquete_error[4 + longitud_mensaje] = '\0'; // Byte final nulo
    sendto(socket_udp, &paquete_error, tamanio_msg, 0, (struct sockaddr *)&direccion_cliente, sizeof(direccion_cliente));
}

bool existeArchivo(const char *descripcion)
{
    return access(descripcion, F_OK) == 0;
}

void reestablecerTimeout(int socket_udp)
{
    struct timeval sin_timeout;
    sin_timeout.tv_sec = 0;
    sin_timeout.tv_usec = 0;

    setsockopt(socket_udp, SOL_SOCKET, SO_RCVTIMEO, &sin_timeout, sizeof(sin_timeout));
}

// RRQ
bool enviarArchivo(const char *descripcion, int socket_udp, struct sockaddr_in direccion_cliente)
{
    const char *filename = descripcion;
    const char *modo = descripcion + strlen(descripcion) + 1;

    printf("Archivo: %s\n", filename);
    printf("Modo: %s\n", modo);

    FILE *archivo = fopen(filename, "rb");
    if (archivo == NULL)
    {
        perror("Error al abrir el archivo");
        enviarMensajeError(socket_udp, direccion_cliente, ERR_NOT_DEFINED, "Hubo un error al abrir el archivo.");
        return false;
    }

    char paquete[TAM_MAX_POSIBLE];

    char datos[MAX_DATA];
    uint16_t bloque;
    uint16_t opcode_data = htons(DATA); // DATA

    socklen_t tam = sizeof(direccion_cliente);
    size_t bytes_leidos;
    uint16_t bloque_num = 1;
    int intentos;
    // esperar un segundo
    struct timeval tv;
    tv.tv_sec = TIMEOUT_SEGUNDOS; // 1 segundo
    tv.tv_usec = 0;               // 0 microsegundos

    setsockopt(socket_udp, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    do
    {
        bytes_leidos = fread(datos, 1, sizeof(datos), archivo);

        printf("Cantidad de datos enviados: %ld bytes\n", bytes_leidos);
        bloque = htons(bloque_num);

        memcpy(&paquete[0], &opcode_data, 2);
        memcpy(&paquete[2], &bloque, 2);
        memcpy(&paquete[4], datos, bytes_leidos);

        intentos = 0;
    reintentar:
        printf("Enviando bloque numero:%d \n\n", bloque_num);
        if (sendto(socket_udp, &paquete, 4 + bytes_leidos, 0,
                   (struct sockaddr *)&direccion_cliente, tam) < 0)
        {
            perror("Error al enviar datos");
            enviarMensajeError(socket_udp, direccion_cliente, ERR_NOT_DEFINED, "Error al enviar datos.");
            fclose(archivo);
            return false;
        }

        // Esperar ACK del cliente
        char ack_buffer[4]; // buffer para recibir 4 bytes

        ssize_t recvd = recvfrom(socket_udp, ack_buffer, sizeof(ack_buffer), 0,
                                 (struct sockaddr *)&direccion_cliente, &tam);
        uint16_t opcode_net, bloque_net;
        memcpy(&opcode_net, &ack_buffer[0], 2);
        memcpy(&bloque_net, &ack_buffer[2], 2);

        uint16_t opcode_recibido = ntohs(opcode_net);
        uint16_t bloque_recibido = ntohs(bloque_net);

        if (recvd < 0)
        {
            if (intentos < MAX_REINTENTOS) // intenta 3 veces como mucho
            {
                printf("Se envia nuevamente el dato. Intento numero: %d \n", intentos);
                intentos++; // si no se recibio el ACK, reintentar
                goto reintentar;
            }
            else
            {
                enviarMensajeError(socket_udp, direccion_cliente, ERR_NOT_DEFINED, "Se envio el mismo bloque 3 veces sin respuesta.");
            }
            enviarMensajeError(socket_udp, direccion_cliente, ERR_NOT_DEFINED, "No se pudo enviar el ACK");
            printf("Entro aca\n");
            perror("Error al recibir ACK");
            fclose(archivo);
            return false;
        }

        if (opcode_recibido != ACK || bloque_recibido != bloque_num) // hay que tener en cuenta que se puede errar por 1 en el ack
        {
            printf("ACK inválido %d\n", bloque_recibido);
            printf("ACK que queria%d\n", bloque_num);
            enviarMensajeError(socket_udp, direccion_cliente, ERR_NOT_DEFINED, "Se recibio un ACK invalido.");
            fclose(archivo);
            return false;
        }
        printf("\nRecibiendo ACK:\n");
        printf("  Opcode: %d\n", opcode_recibido);
        printf("  Bloque: %d\n", bloque_recibido);

        bloque_num++;
    } while (bytes_leidos == MAX_DATA); // a chequear

    fclose(archivo);
    return true;
}

// WRQ
bool recibirArchivo(const char *descripcion, int socket_udp, struct sockaddr_in direccion_cliente)
{
    const char *filename = descripcion;
    const char *modo = descripcion + strlen(descripcion) + 1;
    printf("Archivo: %s\n", filename);
    printf("Modo: %s\n", modo);

    if (access(filename, F_OK) == 0)
    {
        printf("El archivo '%s' ya existe. Operación cancelada.\n", filename);
        enviarMensajeError(socket_udp, direccion_cliente, ERR_FILE_EXISTS, "El archivo ya existe");
        return false;
    }
    FILE *archivo = fopen(filename, "wb");
    if (archivo == NULL)
    {
        perror("Error al abrir el archivo");
        // que codigo le deberia poner?
        enviarMensajeError(socket_udp, direccion_cliente, ERR_NOT_DEFINED, "No se pudo crear el archivo");
        return false;
    }

    socklen_t tam = sizeof(direccion_cliente);
    uint16_t bloque_esperado = 1;
    struct timeval tv;
    tv.tv_sec = TIMEOUT_SEGUNDOS; // 1 segundo
    tv.tv_usec = 0;               // 0 microsegundos

    setsockopt(socket_udp, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    char ack[4];
    *(uint16_t *)&ack[0] = htons(ACK);
    *(uint16_t *)&ack[2] = htons(0);

    int intentos = 0;

    // enviar primer ack
    while (intentos < MAX_REINTENTOS)
    {
        if (sendto(socket_udp, &ack, sizeof(ack), 0, (struct sockaddr *)&direccion_cliente, tam) < 0)
        {
            intentos++;
        }
        else
        {
            break;
        }
    }
    if (intentos == MAX_REINTENTOS)
    {
        enviarMensajeError(socket_udp, direccion_cliente, ERR_NOT_DEFINED, "No se pudo enviar el ACK");
        fclose(archivo);
        perror("Error al enviar el ACK");
        return false;
    }
    intentos = 0;
    char paquete[TAM_MAX_POSIBLE];

    while (1)
    {
        ssize_t bytes_recibidos = recvfrom(socket_udp, &paquete, sizeof(paquete), 0,
                                           (struct sockaddr *)&direccion_cliente, &tam);

        if (bytes_recibidos < 4)
        {
            enviarMensajeError(socket_udp, direccion_cliente, ERR_ILLEGAL_TFTP_OPERATION, "Paquete recibido demasiado pequeño");
            // Paquete demasiado pequeño para ser válido
            fprintf(stderr, "Paquete recibido demasiado pequeño\n");
            fclose(archivo);
            return false;
        }

        uint16_t opcode_recibido = ntohs(*(uint16_t *)&paquete[0]);

        uint16_t bloque = ntohs(*(uint16_t *)&paquete[2]);
        size_t datos_len = bytes_recibidos - 4;
        char *datos = &paquete[4];

        if (opcode_recibido != DATA)
        {
            fprintf(stderr, "Paquete recibido con opcode inesperado: %d\n", opcode_recibido);
            enviarMensajeError(socket_udp, direccion_cliente, ERR_ILLEGAL_TFTP_OPERATION, "Paquete recibido con opcode inesperado");
            fclose(archivo);
            return false;
        }

        if (bloque != bloque_esperado)
        {
            enviarMensajeError(socket_udp, direccion_cliente, ERR_ILLEGAL_TFTP_OPERATION, "Paquete recibido fuera de orden");
            fprintf(stderr, "Bloque recibido fuera de orden: esperado %d, recibido %d\n", bloque_esperado, bloque);
            fclose(archivo);
            return false;
        }

        // Escribir datos en el archivo
        size_t escritos = fwrite(datos, 1, datos_len, archivo);

        if (escritos != datos_len)
        {
            perror("Error al escribir en el archivo");
            // no estoy seguro de este error
            enviarMensajeError(socket_udp, direccion_cliente, ERR_NOT_DEFINED, "Error escribiendo el archivo");
            fclose(archivo);
            return false;
        }

        // Enviar ACK
        *(uint16_t *)&ack[0] = htons(ACK);
        *(uint16_t *)&ack[2] = htons(bloque);

        int ack_enviado = 0;
        for (int i = 0; i < 3; i++)
        {
            if (sendto(socket_udp, &ack, sizeof(ack), 0, (struct sockaddr *)&direccion_cliente, tam) >= 0)
            {
                ack_enviado = 1;
                printf("ACK N: %d enviado correctamente\n", bloque);
                break;
            }
            else
            {
                printf("Se envia nuevamente el ACK. Intento numero: %d\n", i + 1);
            }
        }

        if (!ack_enviado)
        {
            enviarMensajeError(socket_udp, direccion_cliente, ERR_NOT_DEFINED, "Se envio el mismo ACK 3 veces sin respuesta.");
            perror("Error al enviar ACK");
            fclose(archivo);
            return false;
        }

        printf("ACK N: %d enviado\n", bloque);

        if (datos_len < MAX_DATA)
        {
            printf("Transferencia completada\n");
            // Último paquete recibido, termina recepción
            break;
        }

        bloque_esperado++;
    }

    fclose(archivo);
    return true;
}

void imprimir_paquete(uint16_t bloque, const char *datos, size_t len)
{
    printf("\n--- Paquete bloque #%d, bytes: %zu ---\n", bloque, len);

    printf("Data: ");
    for (size_t i = 0; i < len; i++)
    {
        if (isprint((unsigned char)datos[i]))
        {
            putchar(datos[i]);
        }
    }
    printf("\n\n");
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "Uso: %s <puerto>\n", argv[0]);
        return EXIT_FAILURE;
    }
    int socket_udp;

    // Primer parametro: se usa la internet, segundo parametro: si es o no orientado a la conexion, el ultimo numero siempre es 0 porque indica al protocolo que corresponde si es orientado a la conexion o datagramas. Porque cuando lo crearon pensaron que iba a haber mas protocolos para cada uno. Al final TCP termino siendo orientado a la conexion y UDP no orientado a la conexion y no hubieron mas protocolos para cada uno.
    socket_udp = socket(AF_INET, SOCK_DGRAM, 0);

    // sockaddr_in = socket addres y el in significa internet
    struct sockaddr_in servidor;

    // Pone la estructura llena de 0's
    memset(&servidor, 0, sizeof(servidor));

    // Le pasamos tipo de direccion, internet y puerto a la estructura de datos
    servidor.sin_family = AF_INET;
    servidor.sin_port = htons(atoi(argv[1]));
    servidor.sin_addr.s_addr = INADDR_ANY;
    // servidor.sin_addr.s_addr = INADDR_LOOPBACK;

    // Hace un bind en el socket con la estructura. El * es un puntero, el & es para que genere el puntero
    bind(socket_udp, (struct sockaddr *)&servidor, sizeof(servidor));

    // 2 bytes para opcode + nombre + 1 byte nulo + modo + 1 byte nulo
    char mensaje[TAM_MAX_POSIBLE]; // tamaño máximo posible

    struct sockaddr_in direccion_cliente;
    socklen_t tam_direccion = sizeof(direccion_cliente);
    int puerto_base = atoi(argv[1]);
    int contador_puertos = 1; // Comenzamos desde 1 para ir sumando

    while (1)
    {
        printf("Esperando mensaje...\n");
        ssize_t bytes_recibidos = recvfrom(socket_udp, &mensaje, sizeof(mensaje), 0,
                                           (struct sockaddr *)&direccion_cliente, &tam_direccion);
        pid_t p = fork();

        if (p < 0)
        {
            perror("fork");
            continue;
        }

        if (p == 0)
        {
            // Crear nuevo socket
            int nuevo_socket = socket(AF_INET, SOCK_DGRAM, 0);
            if (nuevo_socket < 0)
            {
                perror("socket hijo");
                exit(EXIT_FAILURE);
            }

            // Nueva estructura con nuevo puerto
            struct sockaddr_in direccion_hijo;
            memset(&direccion_hijo, 0, sizeof(direccion_hijo));
            direccion_hijo.sin_family = AF_INET;
            direccion_hijo.sin_port = htons(puerto_base + contador_puertos); // siguiente puerto
            direccion_hijo.sin_addr.s_addr = INADDR_ANY;

            if (bind(nuevo_socket, (struct sockaddr *)&direccion_hijo, sizeof(direccion_hijo)) < 0)
            {
                perror("bind hijo");
                exit(EXIT_FAILURE);
            }
            // Extraer opcode (primeros 2 bytes)
            uint16_t opcode_net;
            memcpy(&opcode_net, &mensaje[0], 2);
            uint16_t opcode = ntohs(opcode_net);

            // El resto del mensaje
            char *descripcion = &mensaje[2];

            printf("Mensaje recibido: %zd bytes\n", bytes_recibidos);
            printf("Mensaje de tipo %d recibido\n", opcode);

            if (bytes_recibidos < 0)
            {
                perror("recvfrom");
                exit(EXIT_FAILURE);
            }

            switch (opcode)
            {
            case RRQ:
                printf("Mensaje de tipo RRQ\n");
                if (existeArchivo(descripcion))
                {
                    printf("El archivo '%s' existe\n", descripcion);
                    enviarArchivo(descripcion, nuevo_socket, direccion_cliente);
                }
                else
                {
                    printf("El archivo '%s' no existe\n", descripcion);
                    enviarMensajeError(nuevo_socket, direccion_cliente, ERR_FILE_NOT_FOUND, "El archivo no existe");
                }
                break;

            case WRQ:
                printf("Mensaje de tipo WRQ\n");
                recibirArchivo(descripcion, nuevo_socket, direccion_cliente);
                break;
            case DATA:
                printf("Error, primer mensaje de tipo DATA\n");
                enviarMensajeError(nuevo_socket, direccion_cliente, ERR_ILLEGAL_TFTP_OPERATION, "Error, primer mensaje de tipo DATA");
                break;
            case ACK:
                printf("Mensaje de tipo ACK\n");

                enviarMensajeError(nuevo_socket, direccion_cliente, ERR_ILLEGAL_TFTP_OPERATION, "Error, primer mensaje de tipo ACK");

                break;
            case ERROR:
                printf("Mensaje de tipo ERROR\n");
                enviarMensajeError(nuevo_socket, direccion_cliente, ERR_ILLEGAL_TFTP_OPERATION, "Error, primer mensaje de tipo ERROR");
                break;

            default:
                enviarMensajeError(nuevo_socket, direccion_cliente, ERR_ILLEGAL_TFTP_OPERATION, "Error, opcode desconocido");
                break;
            }
            close(nuevo_socket);
            printf("Termina el hijo\n");
            exit(0);
        }
        else
        {
            // Padre: sigue escuchando
            contador_puertos++; // para el próximo hijo
        }
    }
    exit(EXIT_SUCCESS);
}