#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <ctype.h>
#include "../constantes.h"
#include <sys/time.h>

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

// Imprime los paquetes en un formato lindo
void imprimir_data(const char *tipo, uint16_t bloque, const char *datos, size_t len)
{
    printf("\n--- Paquete %s - Bloque #%d, bytes: %zu ---\n", tipo, bloque, len);

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

void imprimir_ack(const char *tipo, uint16_t bloque, int reintentos)
{
    printf("\n--- ACK %s ---\n", tipo);
    printf("  Bloque: %d\n", bloque);
    if (reintentos >= 0)
        printf("Reintentos del mismo bloque: %d\n", reintentos);
    printf("\n");
}

int esperar_datos(int sock, int segundos)
{
    fd_set readfds;
    struct timeval timeout;

    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);

    timeout.tv_sec = segundos;
    timeout.tv_usec = 0;

    // select devuelve:
    // 1 si hay datos listos para leer en sock
    // 0 si se agotó el tiempo
    //-1 si hubo error
    return select(sock + 1, &readfds, NULL, NULL, &timeout);
}

int main(int argc, char *argv[])
{

    if (argc != 5)
    {
        fprintf(stderr, "Uso: %s <ip> <puerto> <RRQ|WRQ> <archivo>\n", argv[0]);
        return EXIT_FAILURE;
    }

    // Argumentos
    char *ip = argv[1];
    int puerto = atoi(argv[2]);
    char *modo_operacion = argv[3];
    char *archivo = argv[4];

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

    // Construir solicitud (RRQ o WRQ)
    uint16_t opcode = (strcmp(modo_operacion, "RRQ") == 0) ? RRQ : (strcmp(modo_operacion, "WRQ") == 0) ? WRQ
                                                                                                        : 0;

    if (opcode == 0)
    {
        fprintf(stderr, "Operación inválida: debe ser RRQ o WRQ.\n");
        return EXIT_FAILURE;
    }

    FILE *archivo_destino = NULL;
    FILE *archivo_origen = NULL;

    // Validaciones de archivo antes de enviar solicitud
    if (opcode == RRQ)
    {
        archivo_destino = fopen(archivo, "rb");
        if (archivo_destino != NULL)
        {
            fclose(archivo_destino);
            fprintf(stderr, "Error: el archivo '%s' ya existe en el cliente.\n", archivo);
            return EXIT_FAILURE;
        }
    }
    else if (opcode == WRQ)
    {
        archivo_origen = fopen(archivo, "rb");
        if (!archivo_origen)
        {
            perror("Error al abrir archivo para enviar");
            return EXIT_FAILURE;
        }
    }

    // Escribir opcode de 2 bytes
    buffer[0] = 0;
    buffer[1] = opcode;

    // Escribir filename
    memcpy(&buffer[2], archivo, archivo_len);
    buffer[2 + archivo_len] = '\0';

    // Escribir modo
    memcpy(&buffer[3 + archivo_len], modo, modo_len);
    buffer[3 + archivo_len + modo_len] = '\0';

    // Enviar solicitud
    if (sendto(sock, buffer, solicitud_len, 0, (struct sockaddr *)&servidor, servidor_len) < 0)
    {
        perror("Error al enviar solicitud");
        close(sock);
        return EXIT_FAILURE;
    }

    if (opcode == RRQ)
    {
        // Verificar si el archivo ya existe en el cliente
        FILE *archivo_destino = fopen(archivo, "rb");
        if (archivo_destino != NULL)
        {
            fclose(archivo_destino);
            fprintf(stderr, "Error: el archivo '%s' ya existe en el cliente.\n", archivo);
            exit(EXIT_FAILURE);
        }

        archivo_destino = NULL;
        int archivo_creado = 0;

        char buffer[TAM_MAX_POSIBLE];
        uint16_t bloque_esperado = 1;
        uint16_t ultimo_bloque_ack = 0;
        int reintentos_mismo_bloque = 0;
       // int contador = 400000;
        while (1)
        {
            int intentos = 0;

            while (intentos < MAX_REINTENTOS)
            {
                fd_set readfds;
                struct timeval timeout;
                FD_ZERO(&readfds);
                FD_SET(sock, &readfds);

                timeout.tv_sec = TIMEOUT_SEGUNDOS;
                timeout.tv_usec = 0;

                int ready = select(sock + 1, &readfds, NULL, NULL, &timeout);

                if (ready < 0)
                {
                    perror("Error en select()");
                    close(sock);
                    exit(EXIT_FAILURE);
                }
                else if (ready == 0)
                {
                    intentos++;
                    fprintf(stderr, "Timeout esperando DATA del servidor. Reintentando... (intento %d)\n", intentos);
                    continue;
                }

                // Si llegamos aquí, hay datos disponibles para leer
                /* ssize_t bytes_recibidos = recvfrom(sock, buffer, sizeof(buffer), 0,
                                                   (struct sockaddr *)&servidor, &servidor_len);

                if (bytes_recibidos < 0)
                {
                    perror("Error al recibir DATA");
                    close(sock);
                    exit(EXIT_FAILURE);
                }
 */
                // seguir con el flujo normal: procesar DATA o ERROR
                break;
            }

            if (intentos == MAX_REINTENTOS)
            {
                fprintf(stderr, "No se recibió respuesta del servidor después de %d intentos. Abortando.\n", MAX_REINTENTOS);
                close(sock);
                exit(EXIT_FAILURE);
            }

            ssize_t bytes_recibidos = recvfrom(sock, buffer, sizeof(buffer), 0,
                                               (struct sockaddr *)&servidor, &servidor_len);
/*             usleep(contador); // 400000 microsegundos = 0.4 segundos
            contador = contador + 900000;
 */
            if (bytes_recibidos < 0)
            {
                perror("Error al recibir DATA");
                fprintf(stderr, "No se pudo establecer la conexión con el servidor o este no respondió.\n");
                close(sock);
                exit(EXIT_FAILURE);
            }

            uint16_t opcode_recibido = ntohs(*(uint16_t *)&buffer[0]);

            if (opcode_recibido == ERROR)
            {
                uint16_t codigo_error = ntohs(*(uint16_t *)&buffer[2]);
                char *mensaje_error = &buffer[4];
                fprintf(stderr, "Error del servidor (código %d): %s\n", codigo_error, mensaje_error);
                break;
            }

            if (opcode_recibido != DATA)
            {
                fprintf(stderr, "\nError: se esperaba DATA, pero se recibió opcode %d\n", opcode_recibido);
                break;
            }

            uint16_t bloque = ntohs(*(uint16_t *)&buffer[2]);
            size_t datos_len = bytes_recibidos - 4;
            char *datos = &buffer[4];

            //imprimir_data("recibido", bloque, datos, datos_len);

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

            if (bloque == bloque_esperado)
            {
                fwrite(datos, 1, datos_len, archivo_destino);
                bloque_esperado++;
                reintentos_mismo_bloque = 0;
            }
            else if (bloque == ultimo_bloque_ack)
            {
                reintentos_mismo_bloque++;
                if (reintentos_mismo_bloque >= 3)
                {
                    fprintf(stderr, "\nSe enviaron 3 ACKs seguidos para el mismo bloque %d. Terminando transferencia.\n", bloque);

                    enviarMensajeError(sock, servidor, 0, "Demasiados reintentos con el mismo bloque");

                    break;
                }
            }

            else
            {
                // Si llega un bloque inesperado que no es repetido, ignorar
                continue;
            }

            // Construir ACK
            char ack[4];
            *(uint16_t *)&ack[0] = htons(ACK);
            *(uint16_t *)&ack[2] = htons(bloque);

            //imprimir_ack("enviado", bloque, reintentos_mismo_bloque);

            if (sendto(sock, ack, sizeof(ack), 0, (struct sockaddr *)&servidor, servidor_len) < 0)
            {
                perror("Error al enviar ACK");
                break;
            }

            ultimo_bloque_ack = bloque;

            if (datos_len < MAX_DATA)
            {
                break;
            }
        }

        if (archivo_destino)
            fclose(archivo_destino);

        printf("\nTransferencia finalizada.\n\n");
    }
    else
    {
        FILE *archivo_origen = fopen(archivo, "rb");
        if (!archivo_origen)
        {
            perror("No se pudo abrir el archivo de origen");
            close(sock);
            exit(EXIT_FAILURE);
        }

        char buffer_envio[TAM_MAX_POSIBLE];
        char buffer_recepcion[TAM_MAX_POSIBLE];
        socklen_t servidor_len = sizeof(servidor);

        struct timeval tv;
        tv.tv_sec = TIMEOUT_SEGUNDOS;
        tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        // Esperar ACK de bloque 0 (respuesta al WRQ)
        // Esperar ACK de bloque 0 o ERROR
        int intentos = 0;
        while (intentos < MAX_REINTENTOS)
        {
            ssize_t bytes_recibidos = recvfrom(sock, buffer_recepcion, sizeof(buffer_recepcion), 0,
                                               (struct sockaddr *)&servidor, &servidor_len);

            if (bytes_recibidos < 0)
            {
                intentos++;
                printf("Timeout esperando ACK de bloque 0. Reintentando... (intento %d)\n", intentos);
                continue;
            }

            uint16_t opcode_recibido;
            memcpy(&opcode_recibido, &buffer_recepcion[0], 2);
            opcode_recibido = ntohs(opcode_recibido);

            if (opcode_recibido == ERROR)
            {
                uint16_t codigo_error;
                memcpy(&codigo_error, &buffer_recepcion[2], 2);
                codigo_error = ntohs(codigo_error);
                char *mensaje_error = &buffer_recepcion[4];

                fprintf(stderr, "Error del servidor (código %d): %s\n", codigo_error, mensaje_error);
                fclose(archivo_origen);
                close(sock);
                exit(EXIT_FAILURE);
            }

            if (opcode_recibido == ACK)
            {
                uint16_t bloque_ack;
                memcpy(&bloque_ack, &buffer_recepcion[2], 2);
                bloque_ack = ntohs(bloque_ack);

                if (bloque_ack == 0)
                {
                    //imprimir_ack("recibido", bloque_ack, intentos);
                    break; // ACK válido
                }
            }

            fprintf(stderr, "Paquete inesperado recibido esperando ACK 0 (opcode %d)\n", opcode_recibido);
        }

        if (intentos == 3)
        {
            fprintf(stderr, "No se recibió ACK de bloque 0 tras 3 intentos. Enviando ERROR y cerrando.\n");

            enviarMensajeError(sock, servidor, 0, "No ACK bloque 0");

            fclose(archivo_origen);
            close(sock);
            exit(EXIT_FAILURE);
        }

        // Comenzar envío de bloques DATA
        uint16_t bloque = 1;
        size_t bytes_leidos;

        do
        {
            bytes_leidos = fread(&buffer_envio[4], 1, MAX_DATA, archivo_origen);
            *(uint16_t *)&buffer_envio[0] = htons(DATA);
            *(uint16_t *)&buffer_envio[2] = htons(bloque);

            intentos = 0;

            while (intentos < 3)
            {
                printf("Enviando bloque número: %d\n", bloque);

                if (sendto(sock, buffer_envio, 4 + bytes_leidos, 0,
                           (struct sockaddr *)&servidor, servidor_len) < 0)
                {
                    perror("Error al enviar DATA");
                    fclose(archivo_origen);
                    close(sock);
                    exit(EXIT_FAILURE);
                }

             //   imprimir_data("enviado", bloque, &buffer_envio[4], bytes_leidos);

                ssize_t recibidos = recvfrom(sock, buffer_recepcion, sizeof(buffer_recepcion), 0,
                                             (struct sockaddr *)&servidor, &servidor_len);

                if (recibidos < 0)
                {
                    intentos++;
                    printf("Timeout esperando ACK del bloque %d. Reintentando... (intento %d)\n", bloque, intentos);
                    continue;
                }

                uint16_t opcode_recv = ntohs(*(uint16_t *)&buffer_recepcion[0]);

                if (opcode_recv == ERROR)
                {
                    uint16_t codigo_error = ntohs(*(uint16_t *)&buffer_recepcion[2]);
                    char *mensaje_error = &buffer_recepcion[4];
                    fprintf(stderr, "Error del servidor (código %d): %s\n", codigo_error, mensaje_error);
                    fclose(archivo_origen);
                    close(sock);
                    exit(EXIT_FAILURE);
                }

                if (opcode_recv == ACK)
                {
                    uint16_t bloque_ack = ntohs(*(uint16_t *)&buffer_recepcion[2]);
                    if (bloque_ack == bloque)
                    {
                        //imprimir_ack("recibido", bloque_ack, intentos);
                        break;
                    }
                    else
                    {
                        fprintf(stderr, "ACK inválido. Esperado: %d, recibido: %d\n", bloque, bloque_ack);
                    }
                }
                else
                {
                    fprintf(stderr, "Opcode inesperado (%d) esperando ACK\n", opcode_recv);
                }
            }

            if (intentos == 3)
            {
                fprintf(stderr, "No se recibió ACK del bloque %d tras 3 intentos. Enviando ERROR y cerrando.\n", bloque);

                enviarMensajeError(sock, servidor, 0, "No ACK de bloque");

                fclose(archivo_origen);
                close(sock);
                exit(EXIT_FAILURE);
            }

            bloque++;

        } while (bytes_leidos == MAX_DATA); // Finaliza cuando se lee un bloque con menos de 512 bytes

        fclose(archivo_origen);
        printf("\nTransferencia finalizada exitosamente.\n\n");
    }

    close(sock);
    if (archivo_destino != NULL)
    {
        fclose(archivo_destino);
    }
    return 0;
}