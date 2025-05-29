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
    u_int8_t separador;
};

void enviarMensajeError(int socket_udp, struct sockaddr_in direccion_cliente, int codigo_error, char *mensaje_error)
{
    struct mensaje_error error;

    error.opcode = ERROR;
    error.errorcode = codigo_error;
    strcpy(error.errmsg, error.errmsg);
    error.separador = 0;
    sendto(socket_udp, &error, sizeof(error), 0, (struct sockaddr *)&direccion_cliente, sizeof(direccion_cliente));
}

bool existeArchivo(struct mensaje_tftp mensaje)
{
    return access(mensaje.descripcion, F_OK) == 0;
}

bool enviarArchivo(struct mensaje_tftp mensaje, int socket_udp, struct sockaddr_in direccion_cliente)
{
    FILE *archivo = fopen(mensaje.descripcion, "rb");
    printf("Descripcion del mensaje %s\n", mensaje.descripcion);
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

label:
    do
    {
        bytes_leidos = fread(paquete.datos, 1, sizeof(paquete.datos), archivo);
        printf("Cantidad de datos enviados: %ld bytes\n", bytes_leidos);
        paquete.opcode = htons(DATA); // DATA
        paquete.bloque = htons(bloque_num);

        if (sendto(socket_udp, &paquete, 4 + bytes_leidos, 0,
                   (struct sockaddr *)&direccion_cliente, tam) < 0)
        {
            perror("Error al enviar datos");
            fclose(archivo);
            return false;
        }

        /*      // esperar un segundo
             struct timeval tv;
             tv.tv_sec = 1;  // 1 segundo
             tv.tv_usec = 0; // 0 microsegundos

             setsockopt(socket_udp, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
      */
        // Esperar ACK del cliente
        ssize_t recvd = recvfrom(socket_udp, &ack, sizeof(ack), 0,
                                 (struct sockaddr *)&direccion_cliente, &tam);
        if (recvd < 0)
        {
            printf("Entro aca\n");
            goto label;
            perror("Error al recibir ACK");
            fclose(archivo);
            return false;
        }

        if (ntohs(ack.opcode) != ACK || ntohs(ack.bloque) != bloque_num)
        {
            fprintf(stderr, "ACK inválido\n");
            fclose(archivo);
            return false;
        }
        printf("ACK Recibido: Bloque %d\n", ntohs(ack.bloque));
        bloque_num++;

    } while (bytes_leidos == MAX_DATA); // a chequear

    fclose(archivo);
    return true;
}

bool recibirArchivo(struct mensaje_tftp mensaje, int socket_udp, struct sockaddr_in direccion_cliente)
{
    if (access(mensaje.descripcion, F_OK) == 0)
    {
        printf("El archivo '%s' ya existe. Operación cancelada.\n", mensaje.descripcion);
        enviarMensajeError(socket_udp, direccion_cliente, ERR_FILE_EXISTS, "El archivo ya existe");
        return false;
    }
    FILE *archivo = fopen(mensaje.descripcion, "w");
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
            //IMPORTANTE aca no deberia terminar. tiene que pedir de nuevo el que corresponde?
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
            fclose(archivo);
            return false;
        }

        // Enviar ACK
        ack.opcode = htons(ACK);
        ack.bloque = htons(bloque);

        if (sendto(socket_udp, &ack, sizeof(ack), 0, (struct sockaddr *)&direccion_cliente, tam) < 0)
        {
            perror("Error al enviar ACK");
            fclose(archivo);
            return false;
        }

        if (datos_len < MAX_DATA)
        {
            // Último paquete recibido, termina recepción
            break;
        }

        bloque_esperado++;
    }

    fclose(archivo);
    return true;
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
    servidor.sin_port = htons(PUERTO_SERVIDOR);
    servidor.sin_addr.s_addr = INADDR_ANY;

    // Hace un bind en el socket con la estructura. El * es un puntero, el & es para que genere el puntero
    bind(socket_udp, (struct sockaddr *)&servidor, sizeof(servidor));

    // ver estructura
    struct mensaje_tftp mensaje;

    struct sockaddr_in direccion_cliente;
    socklen_t tam_direccion = sizeof(direccion_cliente);
    bool salir = false;
    while (!salir)
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

                // Guardar nombre original antes de reemplazarlo
                char nombre_archivo[500];
                strcpy(nombre_archivo, mensaje.descripcion);

                // Opcional: agregar descripción informativa si querés
                strcpy(mensaje.descripcion, "Solicitud para leer un archivo");

                // Volver a poner el nombre correcto para enviarArchivo
                strcpy(mensaje.descripcion, nombre_archivo);

                enviarArchivo(mensaje, socket_udp, direccion_cliente);
            }
            else
            {
                printf("El archivo '%s' no existe\n", mensaje.descripcion);
                enviarMensajeError(socket_udp, direccion_cliente, ERR_FILE_NOT_FOUND, "El archivo no existe");
                salir = true;
            }
            break;

        case 2:
            printf("Mensaje de tipo WRQ\n");
            strcpy(mensaje.descripcion, "Solicitud para escribir un archivo");
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
            salir = true;
            break;

        default:
            break;
        }
        /*         strcpy(ack.descripcion, "ACK");

                // Enviar ACK al cliente
                sendto(socket_udp, &ack, sizeof(ack), 0, (struct sockaddr *)&direccion_cliente, sizeof(direccion_cliente));
         */
        /*  printf("Recibido:\n");
         printf("  Opcode: %d\n", mensaje.opcode);
         printf("  Descripción: %s\n", mensaje.descripcion); */
    }
    exit(EXIT_SUCCESS);
}