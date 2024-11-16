#include "zlib.h"
#include <arpa/inet.h> //inet_addr
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>     //strlen
#include <sys/socket.h> //socets
#include <unistd.h>     //close

#define CHUNK 4096

int decompress_from_socket(int sock, FILE *dest) {
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
  unsigned char client_message[1024];
  unsigned char *data;
  do {
    data = malloc(CHUNK + 1024);
    int rcv_total = 0, rcv_len;
    do {
      rcv_len = recv(sock, client_message, 1024, 0);
      memcpy(data + rcv_total, client_message, rcv_len);
      rcv_total += rcv_len;
    } while (rcv_total < CHUNK && rcv_len);

    strm.avail_in = rcv_total;
    if (rcv_total <= 0) {
      (void)inflateEnd(&strm);
      return Z_ERRNO;
    }
    if (strm.avail_in == 0)
      break;
    strm.next_in = data;

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

    free(data);
    /* done when inflate() says it's done */
  } while (ret != Z_STREAM_END);

  /* clean up and return */
  (void)inflateEnd(&strm);
  return ret == Z_STREAM_END ? total : Z_DATA_ERROR;
}

int send_compressed(int, unsigned char *, size_t);

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
