#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <zlib.h>

#define CHUNK_SIZE 1024

/*#define CHUNK 262144*/
#define CHUNK 4096

int conn_to_server(struct sockaddr_in *server, int);
int send_name(int, char *);
int compress_to_socket(FILE *, int, int);
int send_compressed(int, unsigned char *, size_t);

int main(int argc, char *argv[]) {

  if (argc != 2) {
    puts("Incorrect argument count, exiting");
    return 1;
  }

  FILE *input = fopen(argv[1], "r");
  if (!input) {
    perror("Error opening the file");
    return 1;
  }

  int sock;
  struct sockaddr_in server;
  sock = conn_to_server(&server, 8080);
  if (sock < 0) {
    return 1;
  }

  if (send_name(sock, argv[1])) {
    return 1;
  }

  int transfered = compress_to_socket(input, sock, 9);
  if (transfered < 0) {
    perror("Error compressing the file");
    return 1;
  }
  printf("Transfer complete, sent %d bytes\r\n", transfered);

  fclose(input);
  close(sock);
  return 0;
}

int conn_to_server(struct sockaddr_in *server, int port) {
  int sock;

  sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    puts("Could not obtain a socker, exiting");
    return -1;
  }
  puts("Socket created!");

  server->sin_addr.s_addr = INADDR_ANY;
  server->sin_family = AF_INET;
  server->sin_port = htons(port);

  if (connect(sock, (struct sockaddr *)server, sizeof(*server)) < 0) {
    perror("Failed to connect to the server, error thrown");
    return -1;
  }

  puts("Connected!");
  return sock;
}

int send_name(int sock, char *name) {
  size_t namelen = strlen(name);
  char *filename = malloc(namelen + 2);
  memcpy(filename, name, namelen);
  filename[namelen] = '\n';
  filename[namelen + 1] = '\0';
  if (send(sock, filename, namelen + 2, 0) < 0) {
    puts("Could not send the filename, exiting");
    return 1;
  }
  return 0;
}
