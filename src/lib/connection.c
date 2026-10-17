#include<stdint.h>
#include<unistd.h>
#include<stdbool.h>
#include<arpa/inet.h>
#include<netinet/ip.h>
#include<stdio.h>
#include<fcntl.h>
#include<sys/socket.h>
#include<sys/poll.h>
#include<errno.h>

#include<debug.h>
#include<connection.h>

static const size_t INITIAL_BUF_SIZE = 4096;

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
  conn->fd = connfd;
  conn->want_read = true;
  conn->want_write = false;
  conn->want_close = false;
  buf_init(&conn->incoming);
  buf_init(&conn->outgoing);
  return conn;
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
}

void start_listen_nb(int fd) {
  int rv = listen(fd, SOMAXCONN);
  if (rv) {
    die("listen()");
  }

  size_t fd2conn_cap = 0;
  Conn **fd2conn = NULL;

  struct pollfd *poll_args = NULL;
  size_t poll_args_cap = 0;

  while (true) {
    size_t poll_args_size = 0;
    size_t required_poll_cap = 1;

    for (size_t i = 0; i < fd2conn_cap; i++) {
      if (fd2conn[i]) {
        required_poll_cap++;
      }
    }

    if (poll_args_cap < required_poll_cap) {
      poll_args_cap = required_poll_cap > 16 ? required_poll_cap * 2 : 16;
      struct pollfd *new_poll_args = realloc(poll_args, poll_args_cap * sizeof(struct pollfd));
      if (!new_poll_args) {
        die("realloc error");
      }
      poll_args = new_poll_args;
    }

    poll_args[poll_args_size++] = (struct pollfd){fd, POLLIN, 0};

    for (size_t i = 0; i < fd2conn_cap; ++i) {
      Conn *conn = fd2conn[i];

      if (!conn) {
        continue;
      }

      struct pollfd pfd = {conn->fd, POLLERR, 0};
      if (conn->want_read) {
        pfd.events |= POLLIN;
      }

      if (conn->want_write) {
        pfd.events |= POLLOUT;
      }

      poll_args[poll_args_size++] = pfd;
    }

    int rv = poll(poll_args, (nfds_t)poll_args_size, -1);
    if (rv < 0 && errno == EINTR) {
      continue;
    }
    if (rv < 0) {
      die("poll");
    }

    if (poll_args[0].revents) {
      Conn *conn = handle_accept(fd);
      if (conn) {
        if (fd2conn_cap <= (size_t)conn->fd) {
          size_t old_cap = fd2conn_cap;
          Conn **new_fd2conn = realloc(fd2conn, fd2conn_cap * sizeof(Conn *));
          if (!new_fd2conn) {
            die("realloc error");
          }
          fd2conn = new_fd2conn;
          for (size_t i = old_cap; i < fd2conn_cap; ++i) {
            fd2conn[i] = NULL;
          }
        }
        assert(!fd2conn[conn->fd]);
        fd2conn[conn->fd] = conn;
      }
    }

    for (size_t i = 1; i < poll_args_size; ++i) {
      uint32_t ready = poll_args[i].revents;
      if (ready == 0) {
        continue;
      }

      int fd_idx = poll_args[i].fd;
      if (fd_idx < 0 || (size_t)fd_idx >= fd2conn_cap) {
        continue;
      }

      Conn *conn = fd2conn[poll_args[i].fd];
      if (!conn) continue;

      if (ready & POLLIN) {
        assert(conn->want_read);
        handle_read(conn);
      }
      if (ready & POLLOUT) {
        assert(conn->want_write);
        handle_write(conn);
      }

      if ((ready & POLLERR) || conn->want_close) {
        (void)close(conn->fd);
        fd2conn[conn->fd] = NULL;
        buf_free(&conn->incoming);
        buf_free(&conn->outgoing);
        free(conn);
      }
    }
  }

  free(poll_args);
  free(fd2conn);
}