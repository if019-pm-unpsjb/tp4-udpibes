#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

struct mensaje_tftp {
    uint16_t opcode;        
    char descripcion[500];   
};

int main(int argc, char* argv[])
{

    int socket_udp;

    //Primer parametro: se usa la internet, segundo parametro: si es o no orientado a la conexion, el ultimo numero siempre es 0 porque indica al protocolo que corresponde si es orientado a la conexion o datagramas. Porque cuando lo crearon pensaron que iba a haber mas protocolos para cada uno. Al final TCP termino siendo orientado a la conexion y UDP no orientado a la conexion y no hubieron mas protocolos para cada uno.
    socket_udp = socket(AF_INET, SOCK_DGRAM, 0);

    //Se debe invocar el programa haciendo ./cliente IP Puerto Msg. Entonces:
    char* dir = argv[1];
    char* port = argv[2];
    char* msg = argv[3];

    //sockaddr_in = socket addres y el in significa internet
    struct sockaddr_in dest;
    
    //Pone la estructura llena de 0's
    memset(&dest,0,sizeof(dest));

    //Le pasamos tipo de direccion, internet y puerto a la estructura de datos
    dest.sin_family=AF_INET;
    dest.sin_port = htons(atoi(port));
    dest.sin_addr.s_addr = inet_addr(dir);

    struct mensaje_tftp mensaje;
    memset(&mensaje, 0, sizeof(mensaje));  // Agregá esto
    mensaje.opcode = 1; 
    strncpy(mensaje.descripcion, msg, sizeof(mensaje.descripcion) - 1);
    mensaje.descripcion[sizeof(mensaje.descripcion) - 1] = '\0'; 

    sendto(socket_udp, &mensaje, sizeof(mensaje), 0, (struct sockaddr*)&dest, sizeof(dest));

    printf("Mensaje enviado: opcode = %d, descripcion = \"%s\"\n", mensaje.opcode, mensaje.descripcion);

    exit(EXIT_SUCCESS);
}