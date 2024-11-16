#include <arpa/inet.h> //inet_addr
#include <assert.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>     //strlen
#include <sys/socket.h> //socets
#include <unistd.h>     //close

/*#define CHUNK 262144*/

FILE *open_write_file(int);
int decompress_from_socket(int, FILE *);
void *conn_handler(void *);

struct connection {
  int socket;
  struct sockaddr_in address;
  FILE *outfile;
};

int main(int argc, char *argv[]) {
  int socket_desc, client_sock, c;
  size_t read_size;
  struct sockaddr_in server, client;
  char client_message[2000];

  // Create socket
  socket_desc = socket(AF_INET, SOCK_STREAM, 0);
  if (socket_desc == -1) {
    puts("Could not create socket");
  }
  puts("Socket created");

  // Prepare the sockaddr_in structure
  server.sin_family = AF_INET;
  server.sin_addr.s_addr = INADDR_ANY;
  server.sin_port = htons(8080);

  // Bind
  if (bind(socket_desc, (struct sockaddr *)&server, sizeof(server)) < 0) {
    // print the error message
    puts("bind failed. Error");
    return 1;
  }
  puts("bind done");

  // Listen
  listen(socket_desc, 3);

  // Accept and incoming connection
  puts("Waiting for incoming connections...");
  c = sizeof(struct sockaddr_in);

  char response[] = {"Chunk recieved\r\n"};
  while (1) {
    client_sock =
        accept(socket_desc, (struct sockaddr *)&client, (socklen_t *)&c);
    if (client_sock < 0) {
      puts("accept failed");
      continue;
    }
    puts("Connection accepted");

    FILE *file = open_write_file(client_sock);
    if (!file) {
      puts("Failed to open write file from connection");
      continue;
    }
    struct connection *conn = malloc(sizeof(struct connection));
    *conn = (struct connection){
        .socket = client_sock, .address = client, .outfile = file};
    pthread_t thread_id;
    pthread_create(&thread_id, NULL, conn_handler, (void *)conn);
  }
  return 0;
}

void *conn_handler(void *args) {
  puts("Connection handler started...\r\n");

  struct connection *conn = (struct connection *)args;

  int rv = decompress_from_socket(conn->socket, conn->outfile);
  if (rv < 0) {
    perror("Inflate function error: ");
    return NULL;
  }
  printf("Inflate ended, recieved %d bytes in total\r\n", rv);

  fclose(conn->outfile);

  return args;
}

FILE *open_write_file(int client_sock) {
  char message[255];
  size_t read_size;
  read_size = recv(client_sock, message, 255, 0);
  if (read_size < 0) {
    puts("Error recieving the client name");
  }
  char *fname = malloc(read_size + 5);
  memcpy(fname, "recv_", 5);
  memcpy(fname + 5, message, read_size - 2);

  return fopen(fname, "w");
};
