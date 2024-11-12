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

int compress_to_socket(FILE *source, int sock, int level) {
  int ret, flush, transfered;
  unsigned have;
  z_stream strm;
  int read;
  unsigned char in[CHUNK];
  unsigned char *out;

  /* allocate deflate state */
  strm.zalloc = Z_NULL;
  strm.zfree = Z_NULL;
  strm.opaque = Z_NULL;
  ret = deflateInit(&strm, level);
  if (ret != Z_OK)
    return ret;

  /* compress until end of file */
  do {
    strm.avail_in = fread(in, 1, CHUNK, source);
    if (ferror(source)) {
      (void)deflateEnd(&strm);
      return Z_ERRNO;
    }
    flush = feof(source) ? Z_FINISH : Z_NO_FLUSH;
    strm.next_in = in;

    /* run deflate() on input until output buffer not full, finish
       compression if all of source has been read in */
    do {
      out = malloc(CHUNK); // sizeof can be omitted
      strm.avail_out = CHUNK;
      strm.next_out = out;
      ret = deflate(&strm, flush);   /* no bad return value */
      assert(ret != Z_STREAM_ERROR); /* state not clobbered */
      have = CHUNK - strm.avail_out;
      transfered += send_compressed(sock, out, have);
    } while (strm.avail_out == 0);
    assert(strm.avail_in == 0); /* all input will be used */

    /* done when last data in file processed */
  } while (flush != Z_FINISH);
  assert(ret == Z_STREAM_END); /* stream will be complete */

  /* clean up and return */
  (void)deflateEnd(&strm);
  return transfered;
}

int send_compressed(int sock, unsigned char *buffer, size_t to_send) {
  int read, transfered = 0;
  if (send(sock, buffer, to_send, 0) < 0) {
    puts("Send failed");
    return -1;
  }
  free(buffer);
  return to_send;
};
