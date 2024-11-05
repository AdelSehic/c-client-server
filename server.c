#include "zlib.h"
#include <arpa/inet.h> //inet_addr
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>     //strlen
#include <sys/socket.h> //socets
#include <unistd.h>     //close

#define CHUNK 262144

FILE *open_write_file(int);
int inf(FILE *src, FILE *dst);

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
    // accept connection from an incoming client
    client_sock =
        accept(socket_desc, (struct sockaddr *)&client, (socklen_t *)&c);
    if (client_sock < 0) {
      puts("accept failed");
      return 1;
    }
    puts("Connection accepted");

    FILE *file = open_write_file(client_sock);
    if (!file) {
      puts("Failed to open write file from connection");
      return 1;
    }

    FILE *data = fopen("data", "w");
    if (!data) {
      perror("couldn't open data file");
      return 1;
    }

    // Receive a message from client
    int offset = 0;
    while ((read_size = recv(client_sock, client_message, 2000, 0)) > 0) {
      // Send the message back to client
      fwrite(client_message, sizeof(char), read_size, data);
      offset += read_size;

      send(client_sock, response, strlen(response), 0);
    }
    fclose(data);
    data = fopen("data", "r+");
    if (!data) {
      perror("Coudln't open data again");
      return 1;
    }

    if (read_size == 0) {
      puts("Client disconnected");
      close(client_sock);
      int expanded = inf(data, file);
      if (expanded < 0) {
        perror("Couldnt inflate the file");
        return 1;
      }
      printf("Expanded byte cont: %d\r\n", expanded);
    } else if (read_size == -1) {
      puts("recv failed");
    }

    fclose(data);
    fclose(file);
    remove("data");
  }
  return 0;
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

int inf(FILE *source, FILE *dest) {
  int ret;
  unsigned have;
  z_stream strm;
  unsigned char in[CHUNK];
  unsigned char out[CHUNK];

  /* allocate inflate state */
  strm.zalloc = Z_NULL;
  strm.zfree = Z_NULL;
  strm.opaque = Z_NULL;
  strm.avail_in = 0;
  strm.next_in = Z_NULL;
  ret = inflateInit(&strm);
  if (ret != Z_OK)
    return ret;

  /* decompress until deflate stream ends or end of file */
  size_t total = 0;
  do {
    strm.avail_in = fread(in, 1, CHUNK, source);
    if (ferror(source)) {
      (void)inflateEnd(&strm);
      return Z_ERRNO;
    }
    if (strm.avail_in == 0)
      break;
    strm.next_in = in;

    /* run inflate() on input until output buffer not full */
    do {
      strm.avail_out = CHUNK;
      strm.next_out = out;
      ret = inflate(&strm, Z_NO_FLUSH);
      assert(ret != Z_STREAM_ERROR); /* state not clobbered */
      switch (ret) {
      case Z_NEED_DICT:
        ret = Z_DATA_ERROR; /* and fall through */
      case Z_DATA_ERROR:
      case Z_MEM_ERROR:
        (void)inflateEnd(&strm);
        return ret;
      }
      have = CHUNK - strm.avail_out;
      if (fwrite(out, 1, have, dest) != have || ferror(dest)) {
        (void)inflateEnd(&strm);
        return Z_ERRNO;
      }
      total += have;
    } while (strm.avail_out == 0);

    /* done when inflate() says it's done */
  } while (ret != Z_STREAM_END);

  /* clean up and return */
  (void)inflateEnd(&strm);
  return ret == Z_STREAM_END ? total : Z_DATA_ERROR;
}
