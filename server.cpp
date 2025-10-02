#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "hmap.h"
int main() {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    perror("sockert()");
  }
  int val = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
  struct sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(LISTEN_PORT);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);

  int rec = bind(fd, (const sockaddr *)&addr, sizeof(addr));
  if (rec) {
    perror("bind()");
  }
  rec = listen(fd, SOMAXCONN);

  std::vector<Conn *> fd2conn;

  // 将监听的文件描述符设位非阻塞模式
  fd_set_nb(fd);

  int efd = epoll_create1(0);
  if (efd < 0) {
    perror("epoll_create1()");
    return 1;
  }
  struct epoll_event ev{};
  ev.events = EPOLLIN;
  ev.data.fd = fd;
  if (epoll_ctl(efd, EPOLL_CTL_ADD, fd, &ev) < 0) {
    perror("epoll_ctl()");
    return 1;
  }

  struct epoll_event events[1024];
  while (true) {
    // int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), 1000);
    int n = epoll_wait(efd, events, 1024, 1000);
    if (n < 0) {
      if (errno != EINTR) {
        perror("epoll_wait()");
      }
      continue;
    }
    for (int i = 0; i < n; i++) {
      int cur_fd = events[i].data.fd;
      if (cur_fd == fd) {
        // 有新连接
        int32_t connfd = accept_new_conn(fd2conn, fd);
        if (connfd < 0) {
          break;
        }
        // 新连接已经加入 fd2conn
        // 注册新连接到 epoll
        Conn *conn = fd2conn[connfd];  // 或 conn_put 返回的 conn
        struct epoll_event ev{};
        ev.events = EPOLLIN | EPOLLET;  // ET模式，可选
        ev.data.fd = conn->fd;
        epoll_ctl(efd, EPOLL_CTL_ADD, conn->fd, &ev);
      } else {
        Conn *conn = fd2conn[cur_fd];
        if (!conn) continue;

        connection_io(conn);

        if (conn->state == STATE_END) {
          epoll_ctl(efd, EPOLL_CTL_DEL, conn->fd, nullptr);
          fd2conn[conn->fd] = nullptr;
          close(conn->fd);
          delete (conn);
        } else {
          // 根据状态修改监听事件
          struct epoll_event ev{};
          ev.data.fd = conn->fd;
          ev.events = (conn->state == STATE_REQ ? EPOLLIN : EPOLLOUT) | EPOLLET;
          epoll_ctl(efd, EPOLL_CTL_MOD, conn->fd, &ev);
        }
      }
    }
  }
  return 0;
}
