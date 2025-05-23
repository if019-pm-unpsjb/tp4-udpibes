#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#define MAX_DESC 500
#define MAX_DATA 512

struct mensaje_tftp
{
    uint16_t opcode;
    char descripcion[500];
};

const int RRQ = 01;
const int WRQ = 02;
const int DATA = 03;
const int ACK = 04;
const int ERROR = 05;

int main(int argc, char *argv[])
{

    int socket_udp;

    socket_udp = socket(AF_INET, SOCK_DGRAM, 0);

    if (argc < 4)
    {
        fprintf(stderr, "Uso: %s <IP> <Puerto> <RRQ|WRQ> <Archivo>", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *dir = argv[1];
    char *port = argv[2];
    char *comando = argv[3];
    char *archivo_nombre = argc >= 5 ? argv[4] : "";

    struct sockaddr_in servidor;
    memset(&servidor, 0, sizeof(servidor));
    servidor.sin_family = AF_INET;
    servidor.sin_port = htons(atoi(port));
    servidor.sin_addr.s_addr = inet_addr(dir);

    // Crear el mensaje inicial (RRQ o WRQ)
    struct mensaje_tftp mensaje;
    memset(&mensaje, 0, sizeof(mensaje));

    if (strcmp(comando, "RRQ") == 0)
        mensaje.opcode = RRQ;
    else if (strcmp(comando, "WRQ") == 0)
        mensaje.opcode = WRQ;
    else
    {
        fprintf(stderr, "Comando no reconocido: %s", comando);
        close(socket_udp);
        exit(EXIT_FAILURE);
    }

    strncpy(mensaje.descripcion, archivo_nombre, MAX_DESC - 1);

    socklen_t servidor_len = sizeof(servidor);
    ssize_t bytes_enviados = sendto(socket_udp, &mensaje, sizeof(mensaje), 0, (struct sockaddr *)&servidor, servidor_len);
    if (bytes_enviados < 0)
    {
        perror("Error al enviar mensaje");
        close(socket_udp);
        exit(EXIT_FAILURE);
    }

    printf("Solicitud enviada: opcode = %d, archivo = %s", mensaje.opcode, mensaje.descripcion);

    if (mensaje.opcode == RRQ) {
        FILE *archivo = fopen(archivo_nombre, "wb");
        if (!archivo)
        {
            perror("No se pudo abrir archivo para escritura");
            close(socket_udp);
            exit(EXIT_FAILURE);
        }

        struct
        {
            uint16_t opcode;
            uint16_t bloque;
            char data[MAX_DATA];
        } data_pkt;

        while (1)
        {
            ssize_t recvd = recvfrom(socket_udp, &data_pkt, sizeof(data_pkt), 0, (struct sockaddr *)&servidor, &servidor_len);
            if (recvd < 0)
            {
                perror("Error al recibir DATA");
                fclose(archivo);
                close(socket_udp);
                exit(EXIT_FAILURE);
            }

            if (ntohs(data_pkt.opcode) == ERROR)
            {
                fprintf(stderr, "Servidor devolvia ERROR");
                fclose(archivo);
                close(socket_udp);
                exit(EXIT_FAILURE);
            }

            size_t data_size = recvd - 4; // 4 bytes de cabecera (opcode + bloque)
            fwrite(data_pkt.data, 1, data_size, archivo);

            // Enviar ACK
            struct
            {
                uint16_t opcode;
                uint16_t bloque;
            } ack;
            ack.opcode = ACK;
            ack.bloque = data_pkt.bloque;
            sendto(socket_udp, &ack, sizeof(ack), 0, (struct sockaddr *)&servidor, servidor_len);

            if (data_size < MAX_DATA)
            {
                printf("Archivo recibido completo.\n");
            }
        }

        fclose(archivo);

    } else if (mensaje.opcode == WRQ) {
        FILE *archivo = fopen(archivo_nombre, "rb");
        if (!archivo)
        {
            perror("No se pudo abrir archivo para lectura");
            close(socket_udp);
            exit(EXIT_FAILURE);
        }

        struct
        {
            uint16_t opcode;
            uint16_t bloque;
            char data[MAX_DATA];
        } data_pkt;

        uint16_t bloque = 1;
        while (1)
        {
            size_t leidos = fread(data_pkt.data, 1, MAX_DATA, archivo);
            data_pkt.opcode = ACK;
            data_pkt.bloque = htons(bloque);

            sendto(socket_udp, &data_pkt, leidos + 4, 0, (struct sockaddr *)&servidor, servidor_len);

            // Esperar ACK
            struct
            {
                uint16_t opcode;
                uint16_t bloque;
            } ack;
            ssize_t recvd = recvfrom(socket_udp, &ack, sizeof(ack), 0, (struct sockaddr *)&servidor, &servidor_len);
            if (recvd < 0 || ntohs(ack.opcode) == ERROR)
            {
                fprintf(stderr, "Error al recibir ACK");
                fclose(archivo);
                close(socket_udp);
                exit(EXIT_FAILURE);
            }

            if (leidos < MAX_DATA)
            {
                // Enviar bloque final vacÃ­o si el archivo termina justo en mÃºltiplo de 512
                if (leidos == MAX_DATA)
                {
                    bloque++;
                    data_pkt.opcode = htons(DATA);
                    data_pkt.bloque = htons(bloque);
                    sendto(socket_udp, &data_pkt, 4, 0, (struct sockaddr *)&servidor, servidor_len);
                    recvfrom(socket_udp, &ack, sizeof(ack), 0, (struct sockaddr *)&servidor, &servidor_len);
                }
                printf("Archivo enviado completo.\n");
                break;
            }

            bloque++;
        }

        fclose(archivo);
    }

    close(socket_udp);
    return 0;
}