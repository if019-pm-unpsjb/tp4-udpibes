[![Review Assignment Due Date](https://classroom.github.com/assets/deadline-readme-button-22041afd0340ce965d47ae6ef1cefeee28c7c493a6346c4f15d667ab976d596c.svg)](https://classroom.github.com/a/4JwrUkEc)
# IF019 - TP4

Trabajo Práctico 4 de Redes y Transmisión de Datos

## Compilar

Para facilitar el inicio del TP, van a encontrar dos archivos `.c`, uno para cada servidor, en donde deberían empezar a implementarlos.

Para compilarlos, utilizar el comando `make`. Por ejemplo, para compilar `server-tftp.c` debe ejecutar el comando `make server-tftp` (sin la extensión `.c`). De manera similar, pueden compilar `server-chat.c`. Si no indican un parámetro a `make`, se compilan ambos programas.

## Entrega

Para la entrega final, generar un archivo zip mediante `make zip` y enviarlo por email.

## Sockets

Llamadas al sistema necesarias:

- [socket()](https://www.man7.org/linux/man-pages/man2/socket.2.html): crea un *socket*.
- [bind()](https://man7.org/linux/man-pages/man2/bind.2.html): asocia un *socket* a una dirección.
- [listen()](https://man7.org/linux/man-pages/man2/listen.2.html): espera por conexiones entrantes.
- [accept()](https://man7.org/linux/man-pages/man2/accept.2.html): acepta una conexión entrante.
- [connect()](https://man7.org/linux/man-pages/man2/connect.2.html): conecta con otro *socket*.
- [close()](https://man7.org/linux/man-pages/man2/close.2.html): cierra la conexión (descriptor).
- [recvfrom()](https://man7.org/linux/man-pages/man2/recvmsg.2.html): recibe un datagrama.
- [sendto()](https://man7.org/linux/man-pages/man2/send.2.html): envía un datagrama.

Estructuras de datos:

- [struct sockaddr](https://man7.org/linux/man-pages/man3/sockaddr.3type.html): estructura genérica utilizada para castear al invocar ciertas llamadas al sistema.
- [struct sockaddr_in](https://man7.org/linux/man-pages/man3/sockaddr.3type.html): estructura que representa una dirección IPv4.

Otras funciones de biblioteca útiles:

- [inet_aton, inet_addr, inet_ntoa](https://man7.org/linux/man-pages/man3/inet_addr.3.html): rutinas de manipulación de direcciones IP.
- [htonl, htons, ntohl, ntohs](https://man7.org/linux/man-pages/man3/htons.3.html): cambia el orden de bytes (host a red o viceversa).

### Orden de invocación (orientado a la conexión)

![stream-syscalls](assets/stream-sockets.png)

### Orden de invocación (Datagramas)

![datagram-syscalls](assets/datagram-socket.png)


## Consideraciones de implementacion del chat
Interfaz y ayuda de comandos

Una vez registrado correctamente el nombre de usuario, el cliente debe informar al usuario sobre los comandos disponibles, mostrando la lista de comandos soportados.

El comando /info permite ver los usuarios conectados haciéndole una consulta al cliente. 

Manejo de buffers y comunicación

Es recomendable limpiar (poner en cero) los buffers antes de armar cada mensaje para evitar fugas de información previa.

Comandos y formato

No se permite el uso de espacios ni saltos de línea dentro de los nombres de usuario ni de los comandos enviados. Los nombres de usuario y comandos deben ser una sola palabra o secuencia continua de caracteres sin espacios.

Envío de archivos  

No se permite recibir un archivo si ya existe otro con el mismo nombre. 


Los archivos se nombran como <nombre emisor>_<nombre archivo>

El servidor se ejecuta como ./server-chat <PUERTO_ESCUCHA>
El cliente se ejecuta como ./cliente-chat <IP_SERVIDOR> <PUERTO_ESCUCHA_SERVIDOR> <PUERTO_ESCUCHA_CLIENTE>