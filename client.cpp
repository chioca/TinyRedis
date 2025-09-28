#include <arpa/inet.h>   // inet_pton(), inet_ntop() 等
#include <netdb.h>       // gethostbyname(), getaddrinfo() 等
#include <netinet/in.h>  // sockaddr_in 结构体、AF_INET、htons 等
#include <stdio.h>
#include <sys/socket.h>  // socket(), bind(), connect(), listen(), accept() 等
#include <sys/types.h>   // 基本数据类型定义
#include <unistd.h>      // close()

#include <cassert>
#include <cerrno>   // errno
#include <cstring>  // memset(), memcpy() 等（或 <string.h>）
const size_t k_max_msg = 4096;
static int32_t read_full(int fd, char *buf, size_t n);
static int32_t write_all(int fd, const char *buf, size_t n);
static int32_t query(int fd, const char *text);
int main() {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    perror("socket()");
  }
  struct sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = ntohs(1234);
  addr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK);  // 127.0.0.1
  int rv = connect(fd, (const struct sockaddr *)&addr, sizeof(addr));
  //   char msg[]{"hello"};
  //   write(fd, msg, sizeof(msg));
  //   char rbuf[64]{};
  //   ssize_t n = read(fd, rbuf, sizeof(rbuf) - 1);
  //   printf("server says: %s\n", rbuf);
  int32_t err = query(fd, "hello1");
  if (err) {
    goto L_DONE;
  }
  err = query(fd, "hello2");
  if (err) {
    goto L_DONE;
  }
  err = query(fd, "hello3");
  if (err) {
    goto L_DONE;
  }

L_DONE:
  close(fd);
}
static int32_t write_all(int fd, const char *buf, size_t n) {
  while (n > 0) {
    ssize_t rv = write(fd, buf, n);
    if (rv <= 0) {
      return -1;
    }
    assert((size_t)rv <= n);
    n -= (size_t)rv;
    buf += rv;
  }
  return 0;
}
static int32_t read_full(int fd, char *buf, size_t n) {
  while (n > 0) {
    ssize_t rv = read(fd, buf, n);
    if (rv <= 0) {
      return -1;
    }
    assert((size_t)rv <= n);
    n -= (size_t)rv;
    buf += rv;
  }
  return 0;
}
static int32_t query(int fd, const char *text) {
  uint32_t len = (uint32_t)strlen(text);
  if (len > k_max_msg) {
    return -1;
  }
  char wbuf[k_max_msg + 4];
  memcpy(wbuf, &len, 4);
  memcpy(&wbuf[4], text, len);
  if (int32_t err = write_all(fd, wbuf, 4 + len)) {
    return err;
  }
  char rbuf[4 + k_max_msg + 1];
  errno = 0;
  int32_t err = read_full(fd, rbuf, 4);
  if (err) {
    if (errno == 0) {
      fprintf(stderr, "EOF\n");
    } else {
      perror("read() error");
    }
    return err;
  }
  memcpy(&len, rbuf, 4);
  if (len > k_max_msg) {
    perror("too long");
    return -1;
  }
  err = read_full(fd, &rbuf[4], len);
  if (err) {
    perror("read() error");
    return err;
  }
  rbuf[4 + len] = '\0';
  printf("server says: %s\n", &rbuf[4]);
  return 0;
}