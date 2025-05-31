#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <unistd.h>
#include "constantes.h"
#include <sys/time.h>
#include <ctype.h>

struct mensaje_tftp
{
    uint16_t opcode;
    char descripcion[MAX_DESCRIPCION];
};
struct mensaje_error
{
    uint16_t opcode;
    uint16_t errorcode;
    char errmsg[MAX_DESCRIPCION];
};

void enviarMensajeError(int socket_udp, struct sockaddr_in direccion_cliente, int codigo_error, char *mensaje_error)
{
    struct mensaje_error error;

    error.opcode = ERROR;
    error.errorcode = codigo_error;
    snprintf(error.errmsg, sizeof(error.errmsg), "%s", mensaje_error);

    // Calcular tamaño real del mensaje enviado (hasta el '\0' incluido)
    size_t tamanio_msg = 4 + strlen(error.errmsg) + 1;
    // error.separador = 0;
    sendto(socket_udp, &error, tamanio_msg, 0, (struct sockaddr *)&direccion_cliente, sizeof(direccion_cliente));
}

bool existeArchivo(struct mensaje_tftp mensaje)
{
    return access(mensaje.descripcion, F_OK) == 0;
}

void reestablecerTimeout(int socket_udp)
{
    struct timeval sin_timeout;
    sin_timeout.tv_sec = 0;
    sin_timeout.tv_usec = 0;

    setsockopt(socket_udp, SOL_SOCKET, SO_RCVTIMEO, &sin_timeout, sizeof(sin_timeout));
}

// RRQ
bool enviarArchivo(struct mensaje_tftp mensaje, int socket_udp, struct sockaddr_in direccion_cliente)
{
    FILE *archivo = fopen(mensaje.descripcion, "rb");
    if (archivo == NULL)
    {
        perror("Error al abrir el archivo");
        return false;
    }

    struct
    {
        uint16_t opcode;
        uint16_t bloque;
        char datos[MAX_DATA];
    } paquete;

    struct
    {
        uint16_t opcode;
        uint16_t bloque;
    } ack;

    socklen_t tam = sizeof(direccion_cliente);
    size_t bytes_leidos;
    uint16_t bloque_num = 1;
    int intentos;
    // esperar un segundo
    struct timeval tv;
    tv.tv_sec = 1;  // 1 segundo
    tv.tv_usec = 0; // 0 microsegundos

    setsockopt(socket_udp, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    do
    {
        bytes_leidos = fread(paquete.datos, 1, sizeof(paquete.datos), archivo);
        printf("Cantidad de datos enviados: %ld bytes\n", bytes_leidos);
        paquete.opcode = htons(DATA); // DATA
        paquete.bloque = htons(bloque_num);
        intentos = 0;
    reintentar:
    printf("Enviando bloque numero:%d \n\n",bloque_num);
        if (sendto(socket_udp, &paquete, 4 + bytes_leidos, 0,
                   (struct sockaddr *)&direccion_cliente, tam) < 0)
        {
            perror("Error al enviar datos");
            fclose(archivo);
            return false;
        }

        // Esperar ACK del cliente
        ssize_t recvd = recvfrom(socket_udp, &ack, sizeof(ack), 0,
                                 (struct sockaddr *)&direccion_cliente, &tam);

        printf("\nRecibiendo ACK:\n");
        printf("  Opcode: %d\n", ntohs(ack.opcode));
        printf("  Bloque: %d\n\n", ntohs(ack.bloque));

        if (recvd < 0)
        {
            if (intentos < 2) // intenta 3 veces como mucho
            {
                printf("Se envia nuevamente el dato. Intento numero: %d \n", intentos);
                intentos++; // si no se recibio el ACK, reintentar
                goto reintentar;
            }
            enviarMensajeError(socket_udp, direccion_cliente, ERR_NOT_DEFINED, "Se envio el mismo bloque 3 veces sin respuesta.");
            printf("Entro aca\n");

            perror("Error al recibir ACK");
            fclose(archivo);
            return false;
        }

        if (ntohs(ack.opcode) != ACK || ntohs(ack.bloque) != bloque_num)    //hay que tener en cuenta que se puede errar por 1 en el ack
        {
            printf("ACK inválido %d\n", ntohs(ack.bloque));
            printf("ACK que queria%d\n", bloque_num);
            fclose(archivo);
            return false;
        }
        printf("ACK Recibido: Bloque %d\n", ntohs(ack.bloque));
        bloque_num++;
    } while (bytes_leidos == MAX_DATA); // a chequear

    fclose(archivo);
    return true;
}

// WRQ
bool recibirArchivo(struct mensaje_tftp mensaje, int socket_udp, struct sockaddr_in direccion_cliente)
{
    if (access(mensaje.descripcion, F_OK) == 0)
    {
        printf("El archivo '%s' ya existe. Operación cancelada.\n", mensaje.descripcion);
        enviarMensajeError(socket_udp, direccion_cliente, ERR_FILE_EXISTS, "El archivo ya existe");
        return false;
    }
    FILE *archivo = fopen(mensaje.descripcion, "wb");
    printf("Descripcion del mensaje %s\n", mensaje.descripcion);
    if (archivo == NULL)
    {
        perror("Error al abrir el archivo");
        // que codigo le deberia poner?
        enviarMensajeError(socket_udp, direccion_cliente, ERR_NOT_DEFINED, "No se pudo crear el archivo");
        return false;
    }

    struct
    {
        uint16_t opcode;
        uint16_t bloque;
        char datos[MAX_DATA];
    } paquete;

    struct
    {
        uint16_t opcode;
        uint16_t bloque;
    } ack;

    socklen_t tam = sizeof(direccion_cliente);
    uint16_t bloque_esperado = 1;
    int contador = 900000;

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

        uint16_t opcode = ntohs(paquete.opcode);
        uint16_t bloque = ntohs(paquete.bloque);

        if (opcode != DATA)
        {
            fprintf(stderr, "Paquete recibido con opcode inesperado: %d\n", opcode);
            fclose(archivo);
            return false;
        }

        if (bloque != bloque_esperado)
        {
            // IMPORTANTE aca no deberia terminar. tiene que pedir de nuevo el que corresponde?
            fprintf(stderr, "Bloque recibido fuera de orden: esperado %d, recibido %d\n", bloque_esperado, bloque);
            fclose(archivo);
            return false;
        }

        // La cantidad de datos es bytes_recibidos - 4 bytes (2 para opcode y 2 para bloque)
        size_t datos_len = bytes_recibidos - 4;

        // Escribir datos en el archivo
        size_t escritos = fwrite(paquete.datos, 1, datos_len, archivo);

        if (escritos != datos_len)
        {
            perror("Error al escribir en el archivo");
            // no estoy seguro de este error
            enviarMensajeError(socket_udp, direccion_cliente, ERR_DISK_FULL, "Error escribiendo el archivo");

            fclose(archivo);
            return false;
        }

        // Enviar ACK
        ack.opcode = htons(ACK);
        ack.bloque = htons(bloque);

        usleep(contador); // 400000 microsegundos = 0.4 segundos
        contador = contador + 900000;
        if (sendto(socket_udp, &ack, sizeof(ack), 0, (struct sockaddr *)&direccion_cliente, tam) < 0)
        {
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

    // Hace un bind en el socket con la estructura. El * es un puntero, el & es para que genere el puntero
    bind(socket_udp, (struct sockaddr *)&servidor, sizeof(servidor));

    // ver estructura
    struct mensaje_tftp mensaje;

    struct sockaddr_in direccion_cliente;
    socklen_t tam_direccion = sizeof(direccion_cliente);
    while (1)
    {

        printf("Esperando mensaje...\n");
        ssize_t bytes_recibidos = recvfrom(socket_udp, &mensaje, sizeof(mensaje), 0,
                                           (struct sockaddr *)&direccion_cliente, &tam_direccion);
        printf("Mensaje recibido: %zd bytes\n", bytes_recibidos);
        mensaje.opcode = ntohs(mensaje.opcode);
        printf("Mensaje de tipo %d recibido\n", mensaje.opcode);

        if (bytes_recibidos < 0)
        {
            perror("recvfrom");
            exit(EXIT_FAILURE);
        }

        switch (mensaje.opcode)
        {
        case 1:
            printf("Mensaje de tipo RRQ\n");
            if (existeArchivo(mensaje))
            {
                printf("El archivo '%s' existe\n", mensaje.descripcion);
                enviarArchivo(mensaje, socket_udp, direccion_cliente);
            }
            else
            {
                printf("El archivo '%s' no existe\n", mensaje.descripcion);
                enviarMensajeError(socket_udp, direccion_cliente, ERR_FILE_NOT_FOUND, "El archivo no existe");
            }
            break;

        case 2:
            printf("Mensaje de tipo WRQ\n");
            recibirArchivo(mensaje, socket_udp, direccion_cliente);
            break;
        case 3:
            printf("Error, primer mensaje de tipo DATA\n");
            enviarMensajeError(socket_udp, direccion_cliente, ERR_ILLEGAL_TFTP_OPERATION, "Error, primer mensaje de tipo DATA");
            break;
        case 4:
            printf("Mensaje de tipo ACK\n");

            enviarMensajeError(socket_udp, direccion_cliente, ERR_ILLEGAL_TFTP_OPERATION, "Error, primer mensaje de tipo ACK");

            break;
        case 5:
            printf("Mensaje de tipo ERROR\n");
            enviarMensajeError(socket_udp, direccion_cliente, ERR_ILLEGAL_TFTP_OPERATION, "Error, primer mensaje de tipo ERROR");
            break;

        default:
            break;
        }
        reestablecerTimeout(socket_udp);
    }
    exit(EXIT_SUCCESS);
}