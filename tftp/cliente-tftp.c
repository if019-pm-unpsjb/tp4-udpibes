#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <ctype.h>
#include "constantes.h"

// Imprime los paquetes en un formato lindo
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

    // Argumentos
    char *ip = argv[1];
    int puerto = atoi(argv[2]);
    char *archivo = argv[3];

    // Verificar si el archivo ya existe en el cliente
    FILE *archivo_destino = fopen(archivo, "rb");
    if (archivo_destino != NULL)
    {
        fclose(archivo_destino);
        fprintf(stderr, "Error: el archivo '%s' ya existe en el cliente.\n", archivo);
        exit(EXIT_FAILURE);
    }

    // archivo_destino aun no se abre aca
    archivo_destino = NULL;
    int archivo_creado = 0;

    // Crear socket UDP
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        perror("Error al crear socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in servidor;
    socklen_t servidor_len = sizeof(servidor);
    memset(&servidor, 0, sizeof(servidor));
    servidor.sin_family = AF_INET;
    servidor.sin_port = htons(puerto);
    inet_pton(AF_INET, ip, &servidor.sin_addr);

    char modo[] = "octet"; // Modo (netascii, octet o mail)
    size_t archivo_len = strlen(archivo);
    size_t modo_len = strlen(modo);

    // 2 bytes para opcode + nombre + 1 byte nulo + modo + 1 byte nulo
    size_t solicitud_len = 2 + archivo_len + 1 + modo_len + 1;
    char buffer[TAM_MAX_POSIBLE]; // tamaño máximo posible

    // Escribir opcode de 2 bytes
    buffer[0] = 0;
    buffer[1] = RRQ;

    // Escribir filename
    memcpy(&buffer[2], archivo, archivo_len);
    buffer[2 + archivo_len] = '\0';

    // Escribir modo
    memcpy(&buffer[3 + archivo_len], modo, modo_len);
    buffer[3 + archivo_len + modo_len] = '\0';

    // Enviar RRQ
    if (sendto(sock, buffer, solicitud_len, 0,
               (struct sockaddr *)&servidor, servidor_len) < 0)
    {
        perror("Error al enviar RRQ");
        close(sock);
        exit(EXIT_FAILURE);
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

    // Esperar respuesta del servidor
    while (1)
    {
        ssize_t bytes_recibidos = recvfrom(sock, &paquete, sizeof(paquete), 0,
                                           (struct sockaddr *)&servidor, &servidor_len);
        if (bytes_recibidos < 0)
        {
            perror("Error al recibir DATA");
            close(sock);
            exit(EXIT_FAILURE);
        }

        uint16_t opcode = ntohs(paquete.opcode);
        uint16_t bloque = ntohs(paquete.bloque);
        size_t datos_len = bytes_recibidos - ENCABEZADO; // Le resta el encabezado

        if (opcode != DATA)
        {
            fprintf(stderr, "\nError: se esperaba DATA, pero se recibió opcode %d\n", opcode);
            break;
        }

        // Imprimir contenido recibido
        imprimir_paquete(bloque, paquete.datos, datos_len);

        // Crear archivo recien al recibir el primer paquete valido
        if (!archivo_creado)
        {
            archivo_destino = fopen(archivo, "wbx");
            if (!archivo_destino)
            {
                perror("\nNo se pudo crear el archivo local");
                close(sock);
                exit(EXIT_FAILURE);
            }
            archivo_creado = 1;
        }

        // Guardar en archivo
        fwrite(paquete.datos, 1, datos_len, archivo_destino);

        // Enviar ACK
        ack.opcode = htons(ACK);
        ack.bloque = htons(bloque);
        printf("\nEnviando ACK:\n");
        printf("  Opcode: %d\n", ntohs(ack.opcode));
        printf("  Bloque: %d\n\n", ntohs(ack.bloque));

        if (sendto(sock, &ack, sizeof(ack), 0,
                   (struct sockaddr *)&servidor, servidor_len) < 0)
        {
            perror("Error al enviar ACK");
            break;
        }

        // Último paquete
        if (datos_len < MAX_DATA)
        {
            break;
        }
    }

    printf("\nTransferencia finalizada.\n\n");
    close(sock);
    if (archivo_destino != NULL)
    {
        fclose(archivo_destino);
    }
    return 0;
}