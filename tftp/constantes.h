#ifndef CONSTANTES_H
#define CONSTANTES_H

#define MAX_PAQUETE 516
#define TAM_MAX_POSIBLE 516
#define MAX_DATA 512
#define MAX_DESCRIPCION 500
#define RRQ 1
#define WRQ 2
#define DATA 3
#define ACK 4
#define ERROR 5

#define PUERTO_SERVIDOR 6969

#define MAX_REINTENTOS 3

#define TIMEOUT_SEGUNDOS 1

#define MAX_REINTENTOS 3

//error codes
#define ERR_NOT_DEFINED 0   //Not defined, see error message (if any).
#define ERR_FILE_NOT_FOUND 1    //File not found.
#define ERR_ACCES_VIOLATION 2   //Access violation.
#define ERR_DISK_FULL 3         //Disk full or allocation exceeded.
#define ERR_ALLOCATION_EXCEEDED 3       //Disk full or allocation exceeded.
#define ERR_ILLEGAL_TFTP_OPERATION 4     //Illegal TFTP operation.
#define ERR_UNKNOWN_TID 5       //Unknown transfer ID.
#define ERR_FILE_EXISTS 6       //File already exists.
#define ERR_NO_SUCH_USER 7      //No such user.

typedef struct mensaje_tftp
{
    uint16_t opcode;
    char descripcion[MAX_DESCRIPCION];
} mensaje_tftp;
#endif 