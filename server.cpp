#include <arpa/inet.h>  // inet_pton(), inet_ntop() 等
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>       // gethostbyname(), getaddrinfo() 等
#include <netinet/in.h>  // sockaddr_in 结构体、AF_INET、htons 等
#include <poll.h>
#include <stdio.h>
#include <sys/socket.h>  // socket(), bind(), connect(), listen(), accept() 等
#include <sys/types.h>   // 基本数据类型定义
#include <unistd.h>      // close()

#include <cassert>
#include <cerrno>   // errno
#include <cstring>  // memset(), memcpy() 等（或 <string.h>）
#include <iostream>
#include <vector>

const size_t k_max_msg = 4096;

enum {
  STATE_REQ = 0,
  STATE_RES = 1,
  STATE_END = 2,  // 标记这个连接，准备删除它
};

struct Conn {
  int fd = -1;
  uint32_t state = 0;  // 取值为STATE_REQ 或 STATE_RES
  // 读缓冲区
  size_t rbuf_size = 0;
  uint8_t rbuf[4 + k_max_msg];
  // 写缓冲区
  size_t wbuf_size = 0;
  size_t wbuf_sent = 0;
  uint8_t wbuf[4 + k_max_msg];
};

static void fd_set_nb(int fd);
static void connection_io(Conn *conn);
static void conn_put(std::vector<Conn *> &fd2conn, struct Conn *conn);
static int32_t accept_new_conn(std::vector<Conn *> &fd2conn, int fd);
static void state_req(Conn *conn);
static bool try_fill_buffer(Conn *conn);
static void connection_io(Conn *conn);
static bool try_one_request(Conn *conn);
static void state_res(Conn *conn);
static bool try_flush_buffer(Conn *conn);
int main() {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    perror("sockert()");
  }
  int val = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
  struct sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = ntohs(1234);
  addr.sin_addr.s_addr = ntohl(0);

  int rec = bind(fd, (const sockaddr *)&addr, sizeof(addr));
  if (rec) {
    perror("bind()");
  }
  rec = listen(fd, SOMAXCONN);

  std::vector<Conn *> fd2conn;

  // 将监听的文件描述符设位非阻塞模式
  fd_set_nb(fd);
  std::vector<struct pollfd> poll_args;
  while (true) {
    poll_args.clear();
    struct pollfd pfd = {fd, POLLIN, 0};
    poll_args.push_back(pfd);
    for (Conn *conn : fd2conn) {
      if (!conn) {
        continue;
      }
      struct pollfd pfd{};
      pfd.fd = conn->fd;
      pfd.events = (conn->state == STATE_REQ) ? POLLIN : POLLOUT;
      pfd.events = pfd.events | POLLERR;
      poll_args.push_back(pfd);
    }
    int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), 1000);
    if (rv < 0) {
      perror("poll()");
    }
    for (size_t i = 1; i < poll_args.size(); i++) {
      if (poll_args[i].events) {
        Conn *conn = fd2conn[poll_args[i].fd];
        connection_io(conn);
        if (conn->state == STATE_END) {
          fd2conn[conn->fd] == NULL;
          (void)close(conn->fd);
          free(conn);
        }
      }
    }

    if (poll_args[0].events) {
      (void)accept_new_conn(fd2conn, fd);
    }
  }
  return 0;
}
static void fd_set_nb(int fd) {
  errno = 0;
  int flags = fcntl(fd, F_GETFL, 0);
  if (errno) {
    perror("fcntl error");
    return;
  }

  flags |= O_NONBLOCK;

  errno = 0;
  (void)fcntl(fd, F_SETFL, flags);
  if (errno) {
    perror("fcntl error");
  }
}

static void conn_put(std::vector<Conn *> &fd2conn, struct Conn *conn) {
  if (fd2conn.size() <= (size_t)conn->fd) {
    fd2conn.resize(conn->fd + 1);
  }
  fd2conn[conn->fd] = conn;
}

static int32_t accept_new_conn(std::vector<Conn *> &fd2conn, int fd) {
  struct sockaddr_in client_addr{};
  socklen_t socklen = sizeof(client_addr);
  int connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen);
  if (connfd < 0) {
    perror("accept()");
    return -1;
  }
  fd_set_nb(connfd);
  struct Conn *conn = (struct Conn *)malloc(sizeof(struct Conn));
  if (!conn) {
    close(connfd);
    return -1;
  }
  conn->fd = connfd;
  conn->state = STATE_REQ;
  conn->rbuf_size = 0;
  conn->wbuf_sent = 0;
  conn->wbuf_sent = 0;
  conn_put(fd2conn, conn);
  return 0;
}

static void connection_io(Conn *conn) {
  if (conn->state == STATE_REQ) {
    state_req(conn);
  } else if (conn->state == STATE_RES) {
    state_res(conn);
  } else {
    assert(0);  // 不该出现这种情况
  }
}

static void state_req(Conn *conn) {
  while (try_fill_buffer(conn)) {
  }
}
static bool try_fill_buffer(Conn *conn) {
  assert(conn->rbuf_size < sizeof(conn->rbuf));
  ssize_t rv = 0;
  do {
    size_t cap = sizeof(conn->rbuf) - conn->rbuf_size;
    rv = read(conn->fd, &conn->rbuf[conn->rbuf_size], cap);
  } while (rv < 0 && errno == EINTR);  // 只重试被信号中断的情况

  if (rv < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      // 没有数据可读
      return true;
    } else {
      perror("read() error");
      conn->state = STATE_END;
      return false;
      return false;
    }
  }

  if (rv == 0) {
    if (conn->rbuf_size > 0) {
      perror("unexpected error");
    } else {
      printf("EOF");
    }
    conn->state = STATE_END;
    return false;
  }

  conn->rbuf_size += (size_t)rv;
  assert(conn->rbuf_size <= sizeof(conn->rbuf));
  while (try_one_request(conn)) {
  }
  return (conn->state == STATE_REQ);
}
static bool try_one_request(Conn *conn) {
  // 尝试从缓冲区解析出一个请求
  if (conn->rbuf_size < 4) {
    // 缓冲区数据不够，下次循环再试试
    return false;
  }
  uint32_t len = 0;
  memcpy(&len, &conn->rbuf[0], 4);
  if (len > k_max_msg) {
    perror("too long");
    conn->state = STATE_END;
    return false;
  }
  if (4 + len > conn->rbuf_size) {
    // 缓冲区数据不够，下次循环再试试
    return false;
  }

  // 拿到一个请求，处理一下
  printf("client says: %. *s\n", len, &conn->rbuf[4]);

  // 生成回显响应
  memcpy(&conn->wbuf[0], &len, 4);
  memcpy(&conn->wbuf[4], &conn->rbuf[4], len);
  conn->wbuf_size = 4 + len;

  // 从缓冲区移除这个请求
  // 注意：频繁调用memmove效率可不高
  // 注意：生产环境的代码得优化下这部分
  size_t remain = conn->rbuf_size - 4 - len;
  if (remain) {
    memmove(conn->rbuf, &conn->rbuf[4 + len], remain);
  }
  conn->rbuf_size = remain;

  // 切换状态
  conn->state = STATE_RES;
  state_res(conn);

  // 如果请求处理完了，就继续外层循环
  return (conn->state == STATE_REQ);
}
static void state_res(Conn *conn) {
  while (try_flush_buffer(conn)) {
  }
}

static bool try_flush_buffer(Conn *conn) {
  ssize_t rv = 0;
  size_t remain = conn->wbuf_size - conn->wbuf_sent;
  rv = write(conn->fd, &conn->wbuf[conn->wbuf_sent], remain);
  if (rv < 0 && errno == EAGAIN) {
    // 遇到EAGAIN，停止写入
    return false;
  }
  if (rv < 0) {
    perror("write() error");
    conn->state = STATE_END;
    return false;
  }
  conn->wbuf_sent += (size_t)rv;
  assert(conn->wbuf_sent <= conn->wbuf_size);
  if (conn->wbuf_sent == conn->wbuf_size) {
    // 响应全部发送完毕，切换回STATE_REQ状态
    conn->state = STATE_REQ;
    conn->wbuf_sent = 0;
    conn->wbuf_size = 0;
    return false;
  }
  // 写缓冲区还有数据，可以再试试写入
  return true;
}
