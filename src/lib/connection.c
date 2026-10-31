#include<stdint.h>
#include<unistd.h>
#include<stdbool.h>
#include<arpa/inet.h>
#include<netinet/ip.h>
#include<stdio.h>
#include<fcntl.h>
#include<sys/socket.h>
#include<errno.h>
#include<stdlib.h>
#include<string.h>
#include<assert.h>
#include<sys/epoll.h>

#include<debug.h>
#include<connection.h>
#include<wire.h>

static const size_t INITIAL_BUF_SIZE = 4096;
static const size_t k_max_msg = 32 << 20; // Update for MongoDB Wire

typedef struct {
  uint8_t *data;
  size_t size;
  size_t capacity;
} Buffer;

typedef struct {
  int fd;
  bool want_read;
  bool want_write;
  bool want_close;
  Buffer incoming;
  Buffer outgoing;
} Conn;

typedef struct {
  uint32_t len;
  uint8_t *data;
} Message;

static void fd_set_nb(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    die("fcntl error");
    return;
  }

  flags |= O_NONBLOCK;

  if (fcntl(fd, F_SETFL, flags) < 0) {
    die("fcntl error");
  }
}

static void buf_init(Buffer *buf) {
  buf->data = malloc(INITIAL_BUF_SIZE);
  if (!buf->data) {
    die("malloc error");
  }
  buf->size = 0;
  buf->capacity = INITIAL_BUF_SIZE;
}

static void buf_free(Buffer *buf) {
  free(buf->data);
  buf->data = NULL;
  buf->size = 0;
  buf->capacity = 0;
}

static void buf_append(Buffer *buf, const uint8_t *data, size_t len) {
  if (buf->size + len > buf->capacity) {
    size_t new_cap = buf->capacity;
    while (buf->size + len > new_cap) {
      new_cap *= 2;
    }
    uint8_t *new_data = realloc(buf->data, new_cap);
    if (!new_data) {
      die("realloc error");
    }
    buf->data = new_data;
    buf->capacity = new_cap;
  }
  memcpy(buf->data + buf->size, data, len);
  buf->size += len;
}

static void buf_consume(Buffer *buf, size_t n) {
  if (n >= buf->size) {
    buf->size = 0;
  } else {
    memmove(buf->data, buf->data + n, buf->size - n);
    buf->size -= n;
  }
}

static Conn *handle_accept(int fd) {
  struct sockaddr_in client_addr = {};
  socklen_t addrlen = sizeof(client_addr);
  int connfd = accept(fd, (struct sockaddr *)&client_addr, &addrlen);
  if (connfd < 0) {
    msg_errno("accept() error");
    return NULL;
  }

  uint32_t ip = client_addr.sin_addr.s_addr;
  fprintf(stderr, "new client from %u.%u.%u.%u:%u\n",
    ip & 255, (ip >> 8) & 255, (ip >> 16) & 255, ip >> 24,
    ntohs(client_addr.sin_port)
  );

  fd_set_nb(connfd);

  Conn *conn = malloc(sizeof(Conn));
  if (!conn) {
    die("malloc error");
  }
  conn->fd = connfd;
  conn->want_read = true;
  conn->want_write = false;
  conn->want_close = false;
  buf_init(&conn->incoming);
  buf_init(&conn->outgoing);
  return conn;
}

// static int get_response(const Message *request_msg, Message *response_msg) {
//   if (request_msg->len == 0) {
//     return -1; // TODO: Assign enumerated error code
//   }

//   response_msg->len = request_msg->len;

//   response_msg->data = malloc(sizeof(uint8_t) * response_msg->len);
//   if (!response_msg->data) {
//     return -1; // TODO: Assign enumerated error code
//   }
//   memcpy(response_msg->data, request_msg->data, response_msg->len);
//   return 0;
// }


static bool try_one_request(Conn *conn) {
  if (conn->incoming.size < 4) {
    return false;
  }
  uint32_t len = 0;
  memcpy(&len, conn->incoming.data, 4);
  if (len > k_max_msg) {
    msg("too long");
    conn->want_close = true;
    return false;
  }

  if (len > conn->incoming.size) {
    return false;
  }

  WireDeserializeError err;
  WireMessage *wire_msg = wire_message_new_from_buf(conn->incoming.data, len, &err);

  if (err) {
    printf("Error: %d\n", err);
    // What to do with buf?
    // return false;
  }

  char buffer[256];
  int rv = snprintf(
    buffer,
    sizeof(buffer), 
    "message_length: %d, request_id: %d, response_to: %d, op_code: %d, flag_bits: %d",
    wire_msg->header.message_length,
    wire_msg->header.request_id,
    wire_msg->header.response_to,
    wire_msg->header.op_code,
    wire_msg->payload.msg.flag_bits
  );
  
  if (rv < 0 || (size_t)rv >= sizeof(buffer)) {
    // handle error
  }

  uint32_t buffer_len = (uint32_t)rv;
  printf("%d", buffer_len);
  printf("%.s", buffer);

  buf_append(&conn->outgoing, (const uint8_t *)&buffer_len, 4);
  buf_append(&conn->outgoing, (const uint8_t *)buffer, buffer_len);
  
  buf_consume(&conn->incoming, 4 + len);
  return true;
}

static void conn_destroy(Conn *conn) {
  if (!conn) return;
  close(conn->fd);
  buf_free(&conn->incoming);
  buf_free(&conn->outgoing);
  free(conn);
}

static void update_epoll(int epoll_fd, Conn *conn) {
  struct epoll_event ev = {0};
  ev.data.ptr = conn;
  if (conn->want_read) ev.events |= EPOLLIN;
  if (conn->want_write) ev.events |= EPOLLOUT;
  ev.events |= EPOLLERR | EPOLLHUP;

  if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, conn->fd, &ev) < 0) {
    die("epoll_ctl mod error");
  }
}

static void handle_write(int epoll_fd, Conn *conn) {
  assert(conn->outgoing.size > 0);
  ssize_t rv = write(conn->fd, conn->outgoing.data, conn->outgoing.size);
  if (rv < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    return;
  }

  if (rv < 0) {
    msg_errno("write() error");
    conn->want_close = true;
    return;
  }

  buf_consume(&conn->outgoing, (size_t) rv);

  if (conn->outgoing.size == 0) {
    conn->want_read = true;
    conn->want_write = false;
    update_epoll(epoll_fd, conn);
  }
}

static void handle_read(int epoll_fd, Conn *conn) {
  uint8_t buf[64 * 1024];
  ssize_t rv = read(conn->fd, buf, sizeof(buf));
  if (rv < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    return;
  }

  if (rv < 0) {
    msg_errno("read() error");
    conn->want_close = true;
    return;
  }
  if (rv == 0) {
    conn->want_close = true;
    return;
  }
  
  buf_append(&conn->incoming, buf, (size_t)rv);

  while (try_one_request(conn)) {}

  if (conn->outgoing.size > 0) {
    conn->want_read = false;
    conn->want_write = true;
    update_epoll(epoll_fd, conn);
    handle_write(epoll_fd, conn);
  }
}

int create_socket(int port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    die("socket()");
  }

  int val = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = ntohs(port);
  addr.sin_addr.s_addr = ntohl(0);
  int rv = bind(fd, (const struct sockaddr *)&addr, sizeof(addr));

  if (rv) {
    die("bind()");
  }

  fd_set_nb(fd);
  return fd;
}

void start_listen_nb(int listen_fd) {
  int rv = listen(listen_fd, SOMAXCONN);
  if (rv) {
    die("listen()");
  }

  int epoll_fd = epoll_create1(0);
  if (epoll_fd < 0) {
    die("epoll_create1()");
  }

  struct epoll_event ev = {0};
  ev.events = EPOLLIN;
  ev.data.fd = listen_fd;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &ev) < 0) {
    die("epoll_ctl add listen_fd error");
  }

  const int MAX_EVENTS = 64;
  struct epoll_event events[MAX_EVENTS];

  while (true) {
    int nready = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
    if (nready < 0) {
      if (errno == EINTR) continue;
      die("epoll_wait()");
    }

    for (int i = 0; i < nready; ++i) {
      if (events[i].data.fd == listen_fd) {
        Conn *conn = handle_accept(listen_fd);
        if (conn) {
          struct epoll_event conn_ev = {0};
          conn_ev.events = EPOLLIN | EPOLLERR | EPOLLHUP;
          conn_ev.data.ptr = conn;
          if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn->fd, &conn_ev) < 0) {
            die("epoll_ctl add conn error");
          }
        }
      } else {
        Conn *conn = (Conn *)events[i].data.ptr;
        uint32_t revents = events[i].events;
        
        if (revents & (EPOLLERR | EPOLLHUP)) {
          conn->want_close = true;
        }

        if (!conn->want_close && (revents & EPOLLIN)) {
          assert(conn->want_read);
          handle_read(epoll_fd, conn);
        }

        if (!conn->want_close && (revents & EPOLLOUT)) {
          assert(conn->want_write);
          handle_write(epoll_fd, conn);
        }

        if (conn->want_close) {
          if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, conn->fd, NULL) < 0) {
            // Ignore if already closed
          }
          conn_destroy(conn);
        }
      }
    }
  }

  close(epoll_fd);
}