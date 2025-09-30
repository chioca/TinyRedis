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

enum {
  SER_ERR = 1,
  SER_STR = 2,
  SER_INT = 3,
  SER_ARR = 4,
  SER_NIL = 5,
};
const size_t k_max_msg = 4096;
static int32_t read_full(int fd, char *buf, size_t n);
static int32_t write_all(int fd, const char *buf, size_t n);
static int32_t query(int fd, const char *text);
static int32_t send_req(int fd, const std::vector<std::string> &cmd);
static int32_t read_req(int fd);
static int32_t on_response(const uint8_t *data, size_t size);

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
  err = read_full(fd, &rbuf[4], len);
  if (err) {
    perror("read() error");
    return err;
  }
  on_response((const uint8_t *)&rbuf[4], len);

  return 0;
}

static int32_t on_response(const uint8_t *data, size_t size) {
  if (size < 1) {
    printf("Bad Response\n");
    return -1;
  }
  switch (data[0]) {
    case SER_NIL:
      /* code */
      printf("(nil)\n");
      return 1;
    case SER_ERR: {
      if (size < 1 + 8) {
        printf("Bad Response\n");
        return -1;
      }
      int32_t code = 0;
      memcpy(&code, &data[1], 4);
      uint32_t len = 0;
      memcpy(&len, &data[5], 4);
      if (size < 1 + 8 + len) {
        printf("Bad Response\n");
        return -1;
      }
      printf("Error %d: %.*s\n", code, len, (const char *)&data[9]);
      return 0;
    }
    case SER_STR: {
      if (size < 1 + 4) {
        printf("Bad Response\n");
        return -1;
      }
      uint32_t slen = 0;
      memcpy(&slen, &data[1], 4);
      if (size < 1 + 4 + slen) {
        printf("Bad Response\n");
        return -1;
      }
      printf("%.*s\n", slen, (const char *)&data[5]);
      return (int32_t)1 + 4 + slen;
    }
    case SER_INT: {
      if (size < 1 + 8) {
        printf("Bad Response\n");
        return -1;
      }
      int64_t val = 0;
      memcpy(&val, &data[1], 8);
      printf("%lld\n", (long long)val);
      return (int32_t)1 + 8;
    }
    case SER_ARR: {
      if (size < 1 + 4) {
        printf("Bad Response\n");
        return -1;
      }
      uint32_t n = 0;
      memcpy(&n, &data[1], 4);
      printf("(arr) len = %u\n", n);
      size_t arr_bytes = 1 + 4;
      for (uint32_t i = 0; i < n; i++) {
        int32_t rv = on_response(&data[arr_bytes], size - arr_bytes);
        if (rv < 0) {
          return -1;
        }
        arr_bytes += rv;
      }
      printf("(arr) end\n");
      return (int32_t)arr_bytes;
    }
    default:
      printf("Bad Response\n");
      return -1;
  }
}