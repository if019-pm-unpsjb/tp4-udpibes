#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <unistd.h>

#define MAX_DESC 500
#define MAX_DATA 512

struct mensaje_tftp
{
    uint16_t opcode;
    char descripcion[500];
};

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
        char datos[512];
    } paquete;

    struct
    {
        uint16_t opcode;
        uint16_t bloque;
    } ack;

    socklen_t tam = sizeof(direccion_cliente);
    size_t bytes_leidos;
    uint16_t bloque_num = 1;

    do
    {
        printf("Tamanio de paquete %ld\n", sizeof(paquete.datos));
        bytes_leidos = fread(paquete.datos, 1, sizeof(paquete.datos), archivo);
        paquete.opcode = htons(3); // DATA
        paquete.bloque = htons(bloque_num);

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
        if (recvd < 0)
        {
            perror("Error al recibir ACK");
            fclose(archivo);
            return false;
        }

        if (ntohs(ack.opcode) != 4 || ntohs(ack.bloque) != bloque_num)
        {
            fprintf(stderr, "ACK inválido\n");
            fclose(archivo);
            return false;
        }

        bloque_num++;

    } while (bytes_leidos == 512);

    fclose(archivo);
    return true;
}

int main(int argc, char *argv[])
{
    int socket_udp;

    // Primer parametro: se usa la internet, segundo parametro: si es o no orientado a la conexion, el ultimo numero siempre es 0 porque indica al protocolo que corresponde si es orientado a la conexion o datagramas. Porque cuando lo crearon pensaron que iba a haber mas protocolos para cada uno. Al final TCP termino siendo orientado a la conexion y UDP no orientado a la conexion y no hubieron mas protocolos para cada uno.
    socket_udp = socket(AF_INET, SOCK_DGRAM, 0);

    // Se debe invocar el programa haciendo ./cliente Puerto. Entonces:
    char *port = argv[1];

    // sockaddr_in = socket addres y el in significa internet
    struct sockaddr_in cliente;

    // Pone la estructura llena de 0's
    memset(&cliente, 0, sizeof(cliente));

    // Le pasamos tipo de direccion, internet y puerto a la estructura de datos
    cliente.sin_family = AF_INET;
    cliente.sin_port = htons(atoi(port));
    cliente.sin_addr.s_addr = INADDR_ANY;

    // Hace un bind en el socket con la estructura. El * es un puntero, el & es para que genere el puntero
    bind(socket_udp, (struct sockaddr *)&cliente, sizeof(cliente));

    struct mensaje_tftp mensaje;
    mensaje.opcode = ntohs(mensaje.opcode);

    struct sockaddr_in direccion_cliente;
    socklen_t tam_direccion = sizeof(direccion_cliente);
    bool salir = false;
    while (!salir)
    {

        printf("Esperando mensaje...\n");
        ssize_t bytes_recibidos = recvfrom(socket_udp, &mensaje, sizeof(mensaje), 0,
                                           (struct sockaddr *)&direccion_cliente, &tam_direccion);
        printf("Mensaje recibido: %zd bytes\n", bytes_recibidos);

        /*   struct mensaje_tftp ack;
          ack.opcode = 4; // ACK por defecto
   */
        if (bytes_recibidos < 0)
        {
            perror("recvfrom");
            exit(EXIT_FAILURE);
        }

        printf("Mensaje de tipo %d recibido\n", mensaje.opcode);

        switch (mensaje.opcode)
        {
        case 1:
            printf("Mensaje de tipo RRQ\n");
            if (existeArchivo(mensaje))
            {
                printf("El archivo '%s' existe\n", mensaje.descripcion);
            }
            else
            {
                printf("El archivo '%s' no existe\n", mensaje.descripcion);
                salir = true;
                break;
            }
            strcpy(mensaje.descripcion, "Solicitud para leer un archivo");
            enviarArchivo(mensaje, socket_udp, direccion_cliente);
            break;
        case 2:
            printf("Mensaje de tipo WRQ\n");
            strcpy(mensaje.descripcion, "Solicitud para escribir un archivo");
            break;
        case 3:
            printf("Mensaje de tipo DATA\n");
            if (bytes_recibidos < 512)
            {
                salir = true;
            }

            break;
        case 4:
            printf("Mensaje de tipo ACK\n");

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
        printf("Recibido:\n");
        printf("  Opcode: %d\n", mensaje.opcode);
        printf("  Descripción: %s\n", mensaje.descripcion);
    }
    exit(EXIT_SUCCESS);
}