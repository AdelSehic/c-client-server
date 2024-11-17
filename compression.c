#include "zlib.h"
#include <arpa/inet.h> //inet_addr
#include <assert.h>
#include <bits/posix1_lim.h>
#include <fcntl.h>
#include <mqueue.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>     //strlen
#include <sys/socket.h> //socets
#include <sys/stat.h>
#include <unistd.h> //close

#define CHUNK 65536
#define MQ_NAME "/a_msg_queue"

typedef enum { false, true } bool;

size_t transfered;

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

typedef struct message {
  bool final;
  unsigned char *buffer;
  size_t size;
  struct message *next;
} message;

struct transfer_init {
  int sock;
  struct message *root;
};

void *send_compressed(void *);
void cleanup(struct message *);

int compress_to_socket(FILE *source, int sock, int level) {
  int ret, flush;
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

  // make thread to send data to server and message queue
  struct message *msg = malloc(sizeof(struct message));
  msg = &(struct message){
      .next = NULL, .final = false, .buffer = NULL, .size = 0};

  pthread_t socket_send_thread;
  struct transfer_init *senddata = malloc(sizeof(struct transfer_init));
  *senddata = (struct transfer_init){.sock = sock, .root = msg};
  pthread_create(&socket_send_thread, NULL, send_compressed, (void *)senddata);

  size_t uid = 0;
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

      struct message *new_msg =
          (struct message *)malloc(sizeof(struct message));

      new_msg->next = NULL;
      new_msg->size = have;
      new_msg->final = false;
      new_msg->buffer = out;
      msg = msg->next = new_msg;

    } while (strm.avail_out == 0);
    assert(strm.avail_in == 0); /* all input will be used */

    /* done when last data in file processed */
  } while (flush != Z_FINISH);
  assert(ret == Z_STREAM_END); /* stream will be complete */

  struct message *final_msg = (struct message *)malloc(sizeof(struct message));

  final_msg->next = NULL;
  final_msg->size = 0;
  final_msg->final = false;
  final_msg->buffer = NULL;
  msg->next = &(struct message){.next = NULL, .final = true, .buffer = NULL};

  pthread_join(socket_send_thread, NULL);

  puts("Transfer complete, cleaning up...");
  cleanup(msg);

  /* clean up and return */
  (void)deflateEnd(&strm);
  mq_unlink(MQ_NAME);
  return transfered;
}

void *send_compressed(void *args) {
  char msgbuf[CHUNK];
  struct transfer_init *conn = (struct transfer_init *)args;

  struct message *msg = conn->root;

  int rcv;
  while (1) {
    while (msg->next == NULL)
      ;
    msg = msg->next;
    if (msg->final) {
      /*puts("[DEBUG] Final message recieved");*/
      break;
    }
    if (msg->size == 0) {
      /*puts("[DEBUG] Message size is 0");*/
      continue;
    }
    /*printf("[DEBUG] New message: %s, uid: %zu\r\n", msg->buffer, msg->uid);*/
    int read = send(conn->sock, msg->buffer, msg->size, 0);
    if (read < 0) {
      perror("Send failed");
      return NULL;
    }
    transfered += msg->size;
    /*printf("[DEBUG] Waiting for the next message ...\r\n");*/
  };

  /*puts("[DEBUG] Thread exiting");*/
  return NULL;
};

void cleanup(struct message *msg) {
  struct message *next;
  while (msg->next != NULL) {
    next = msg->next;
    free(msg);
    msg = next;
  }
}
