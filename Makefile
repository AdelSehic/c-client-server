.PHONY: clean client server

all: client server

FORCE:

client: FORCE
	clang client.c compression.c -o client -lz

server: FORCE
	clang server.c compression.c -o server -lz

clean:
	rm -f client server
