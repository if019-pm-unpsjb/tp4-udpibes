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

void enviarMensajeError(int socket_udp, struct sockaddr_in direccion_cliente, int codigo_err, char *mensaje_error)
{

    char paquete_error[TAM_MAX_POSIBLE];
    uint16_t opcode_error = htons(ERROR);
    uint16_t codigo_error_htons = htons(codigo_err); // 0 es not defined
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

    // PREGUNTAR A FRAN
    const char *filename = descripcion;
    const char *modo = descripcion + strlen(descripcion) + 1;

    printf("Archivo: %s\n", filename);
    printf("Modo: %s\n", modo);

    FILE *archivo = fopen(filename, "rb");
    if (archivo == NULL)
    {
        perror("Error al abrir el archivo");
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
    tv.tv_sec = 1;  // 1 segundo
    tv.tv_usec = 0; // 0 microsegundos

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

            // a chequear si va <2 o <3
            if (intentos < 2) // intenta 3 veces como mucho
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
    // int contador = 900000;
    struct timeval tv;
    tv.tv_sec = 1;  // 1 segundo
    tv.tv_usec = 0; // 0 microsegundos

    setsockopt(socket_udp, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    char ack[4];
    *(uint16_t *)&ack[0] = htons(ACK);
    *(uint16_t *)&ack[2] = htons(0);

    /*   ack.opcode = htons(ACK);
      ack.bloque = htons(0); */
    int intentos = 0;

    // enviar primer ack
    while (intentos < 3)
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
    if (intentos == 3)
    {
        enviarMensajeError(socket_udp, direccion_cliente, ERR_NOT_DEFINED, "No se pudo enviar el ACK");
        fclose(archivo);
        perror("Error al enviar el ACK");
        return false;
    }
    intentos = 0;
    char paquete[TAM_MAX_POSIBLE];

    // PREGUNTAR A FRAN SI LOS DOS LADOS ESPERAN Y VUELVEN A MANDAR
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
            fclose(archivo);
            return false;
        }

        // IMPORTANTE aca no deberia terminar. tiene que pedir de nuevo el que corresponde?
        // IMPORTANTE aca no deberia terminar. tiene que pedir de nuevo el que corresponde?
        // IMPORTANTE aca no deberia terminar. tiene que pedir de nuevo el que corresponde?
        // IMPORTANTE aca no deberia terminar. tiene que pedir de nuevo el que corresponde?
        if (bloque != bloque_esperado)
        {
            fprintf(stderr, "Bloque recibido fuera de orden: esperado %d, recibido %d\n", bloque_esperado, bloque);
            fclose(archivo);
            return false;
        }

        // La cantidad de datos es bytes_recibidos - 4 bytes (2 para opcode y 2 para bloque)

        // Escribir datos en el archivo
        size_t escritos = fwrite(datos, 1, datos_len, archivo);

        if (escritos != datos_len)
        {
            perror("Error al escribir en el archivo");
            // no estoy seguro de este error
            enviarMensajeError(socket_udp, direccion_cliente, ERR_DISK_FULL, "Error escribiendo el archivo");
            fclose(archivo);
            return false;
        }

        // Enviar ACK
        *(uint16_t *)&ack[0] = htons(ACK);
        *(uint16_t *)&ack[2] = htons(bloque);

    // usleep(contador); // 400000 microsegundos = 0.4 segundos
    // contador = contador + 900000;

    // PREGUNTAR A FRAN: conviene hacer una etiqueta o un while?
    enviarack:
        if (sendto(socket_udp, &ack, sizeof(ack), 0, (struct sockaddr *)&direccion_cliente, tam) < 0)
        {

            if (intentos < 2) // intenta 3 veces como mucho
            {
                printf("Se envia nuevamente el ACK. Intento numero: %d \n", intentos);
                intentos++; // si no se recibio el ACK, reintentar
                goto enviarack;
            }
            else
            {
                enviarMensajeError(socket_udp, direccion_cliente, ERR_NOT_DEFINED, "Se envio el mismo ACK 3 veces sin respuesta.");
                perror("Error al enviar ACK");
                fclose(archivo);
                return false;
            }
            enviarMensajeError(socket_udp, direccion_cliente, ERR_NOT_DEFINED, "No se pudo enviar el ACK");

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
    // servidor.sin_addr.s_addr = INADDR_LOOPBACK;

    // Hace un bind en el socket con la estructura. El * es un puntero, el & es para que genere el puntero
    bind(socket_udp, (struct sockaddr *)&servidor, sizeof(servidor));

    // struct mensaje_tftp mensaje;

    // char modo[] = "octet"; // Modo (netascii, octet o mail)

    // 2 bytes para opcode + nombre + 1 byte nulo + modo + 1 byte nulo
    char mensaje[TAM_MAX_POSIBLE]; // tamaño máximo posible

    struct sockaddr_in direccion_cliente;
    socklen_t tam_direccion = sizeof(direccion_cliente);
    while (1)
    {

        printf("Esperando mensaje...\n");
        ssize_t bytes_recibidos = recvfrom(socket_udp, &mensaje, sizeof(mensaje), 0,
                                           (struct sockaddr *)&direccion_cliente, &tam_direccion);

        // se me ocurre que aca se puede hacer el fork?
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
                enviarArchivo(descripcion, socket_udp, direccion_cliente);
            }
            else
            {
                printf("El archivo '%s' no existe\n", descripcion);
                enviarMensajeError(socket_udp, direccion_cliente, ERR_FILE_NOT_FOUND, "El archivo no existe");
            }
            break;

        case WRQ:
            printf("Mensaje de tipo WRQ\n");
            recibirArchivo(descripcion, socket_udp, direccion_cliente);
            break;
        case DATA:
            printf("Error, primer mensaje de tipo DATA\n");
            enviarMensajeError(socket_udp, direccion_cliente, ERR_ILLEGAL_TFTP_OPERATION, "Error, primer mensaje de tipo DATA");
            break;
        case ACK:
            printf("Mensaje de tipo ACK\n");

            enviarMensajeError(socket_udp, direccion_cliente, ERR_ILLEGAL_TFTP_OPERATION, "Error, primer mensaje de tipo ACK");

            break;
        case ERROR:
            printf("Mensaje de tipo ERROR\n");
            enviarMensajeError(socket_udp, direccion_cliente, ERR_ILLEGAL_TFTP_OPERATION, "Error, primer mensaje de tipo ERROR");
            break;

        default:
            enviarMensajeError(socket_udp, direccion_cliente, ERR_ILLEGAL_TFTP_OPERATION, "Error, opcode desconocido");
            break;
        }
        reestablecerTimeout(socket_udp);
    }
    exit(EXIT_SUCCESS);
}