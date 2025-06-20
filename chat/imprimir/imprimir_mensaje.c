#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "imprimir_mensaje.h"

void imprimirMensaje(const char *formato, int mostrarMayor, ...) {
    va_list args;
    va_start(args, mostrarMayor);

    int hayMensaje = (formato && strlen(formato) > 0);

    if (hayMensaje) {
        printf("\33[2K\r");
        vprintf(formato, args);
        printf("\n");
    }

    va_end(args);

    // Si no hay mensaje y se pide mayor, solo muestra el mayor.
    if (!hayMensaje && mostrarMayor) {
        printf("\33[2K\r> ");
    } else if (hayMensaje && mostrarMayor) {
        printf("> ");
    }

    fflush(stdout);
}