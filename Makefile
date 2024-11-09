.PHONY: clean client server

all: client server

FORCE:

client: FORCE
	clang ./client.c -o client -lz

server: FORCE
	clang ./server.c -o server -lz

clean:
	rm -f client server
