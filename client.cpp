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
#include <string>
#include <vector>
const size_t k_max_msg = 4096;
static int32_t read_full(int fd, char *buf, size_t n);
static int32_t write_all(int fd, const char *buf, size_t n);
static int32_t query(int fd, const char *text);
static int32_t send_req(int fd, const std::vector<std::string> &cmd);
static int32_t read_req(int fd);
int main(int argc, char *argv[]) {
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
  std::vector<std::string> cmd;
  for (int i = 1; i < argc; i++) {
    cmd.push_back(argv[i]);
  }
  printf("=======send req========\n");
  int32_t err = send_req(fd, cmd);
  if (err) {
    close(fd);
    return 0;
  }
  printf("=======read res========\n");
  int32_t err2 = read_req(fd);
  if (err) {
    close(fd);
    return 0;
  }
  close(fd);
  return 0;
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
static int32_t send_req(int fd, const std::vector<std::string> &cmd) {
  uint32_t len = 0;
  for (const std::string &s : cmd) {
    len += s.size() + 4;
  }
  if (len > k_max_msg) {
    return -1;
  }
  len += 4;  // 参数个数
  char wbuf[k_max_msg + 4];
  memcpy(&wbuf[0], &len, 4);
  uint32_t cmd_size = (uint32_t)cmd.size();
  memcpy(&wbuf[4], &cmd_size, 4);  // 参数个数
  size_t cur = 8;
  printf("cmd size:%u\n", cmd_size);
  int i = 1;
  for (const std::string &s : cmd) {
    uint32_t slen = (uint32_t)s.size();
    memcpy(&wbuf[cur], &slen, 4);
    printf("arg %d: len=%u, str=%s\n", i++, slen, s.c_str());
    cur += 4;
    memcpy(&wbuf[cur], s.data(), slen);
    cur += slen;
  }

  return write_all(fd, wbuf, 4 + len);
}

static int32_t read_req(int fd) {
  char rbuf[4 + k_max_msg + 1];
  errno = 0;
  int32_t err = read_full(fd, rbuf, 4);
  uint32_t len = 0;
  uint32_t res_code = 0;
  memcpy(&len, &rbuf[0], 4);
  if (len > k_max_msg) {
    perror("too long");
    return -1;
  }
  err = read_full(fd, &rbuf[4], 4);
  if (err) {
    perror("read() error");
    return err;
  }
  rbuf[4 + len] = '\0';
  memcpy(&res_code, &rbuf[4], 4);
  read_full(fd, &rbuf[8], len);
  printf("[%u] %.*s\n", res_code, len, &rbuf[8]);
  return 0;
}