client:
	clang ./client.c -o client

server:
	clang ./server.c -o server

clean:
	rm -f client
