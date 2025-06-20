CC=gcc
CFLAGS=-Wall -Werror -g -pthread

CLIENT_SRC=cliente/cliente-tftp.c
SERVER_SRC=servidor/server-tftp.c

cliente-tftp: $(CLIENT_SRC)
	$(CC) -o cliente/cliente-tftp $^ $(CFLAGS)

server-tftp: $(SERVER_SRC)
	$(CC) -o servidor/server-tftp $^ $(CFLAGS)

.PHONY: all clean zip

all: cliente-tftp server-tftp

clean:
	rm -f cliente/cliente-tftp servidor/server-tftp

zip:
	git archive --format zip --output ${USER}-TP4.zip HEAD