CC=gcc
CFLAGS=-Wall -Werror -g -pthread
BIN=./bin

PROGS=server-chat cliente-chat

.PHONY: all
all: $(PROGS)

LIST=$(addprefix $(BIN)/, $(PROGS))

server-chat: server-chat.c 
	$(CC) -o ./$@ $^ $(CFLAGS)

cliente-chat: cliente-chat.c imprimir/imprimir_mensaje.c
	$(CC) -o ./$@ $^ $(CFLAGS)
	
.PHONY: clean
clean:
	rm -f $(LIST) ./server-chat ./cliente-chat