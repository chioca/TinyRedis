#pragma once
#include <cstddef>  // size_t, offsetof
#include <cstdint>  // uint32_t, uint64_t
#include <cstring>
#include <string>
#include <vector>
#define container_of(ptr, type, member) \
  ((type *)((char *)(ptr) - offsetof(type, member)))
constexpr int LISTEN_PORT = 1234;
const size_t k_max_msg = 4096;
const size_t k_resizing_work = 128;
const size_t k_max_load_factor = 8;
// 标记连接状态
enum {
  STATE_REQ = 0,
  STATE_RES = 1,
  STATE_END = 2,  // 标记这个连接，准备删除它
};

// 错误码定义
enum {
  RES_OK = 0,
  RES_ERR = 1,
  RES_NX = 2,
};

// 序列化相关错误码
enum {
  SER_ERR = 1,
  SER_STR = 2,
  SER_INT = 3,
  SER_ARR = 4,
  SER_NIL = 5,
};

enum {
  ERR_2BIG = 1,
  ERR_INVAL = 2,
  ERR_ARGC = 3,
  ERR_NX = 4,
  ERR_UNKNOWN = 5,
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

struct Hnode {
  Hnode *next = NULL;
  uint64_t hcode = 0;
};

struct Htable {
  Hnode **tab = NULL;
  // size_t size = 0;
  size_t capacity = 0;
  size_t count = 0;
  size_t mask = 0;
};

struct Hmap {
  Htable htab1;
  Htable htab2;
  size_t resizing_pos = 0;
};

struct Entry {
  struct Hnode hnode;
  std::string key;
  std::string val;
};

struct {
  Hmap db;
} g_data;
void fd_set_nb(int fd);
void connection_io(Conn *conn);
void conn_put(std::vector<Conn *> &fd2conn, struct Conn *conn);
int32_t accept_new_conn(std::vector<Conn *> &fd2conn, int fd);
void state_req(Conn *conn);
bool try_fill_buffer(Conn *conn);
void connection_io(Conn *conn);
bool try_one_request(Conn *conn);
void state_res(Conn *conn);
bool try_flush_buffer(Conn *conn);
int32_t do_request(const uint8_t *req, uint32_t req_len, uint32_t *res_code,
                   uint8_t *res, uint32_t *res_len);
void do_request(std::vector<std::string> &cmd, std::string &out);
int32_t parse_req(const uint8_t *req, uint32_t req_len,
                  std::vector<std::string> &cmd);
inline bool cmd_is(const std::string &a, const char *b) {
  return a.size() == strlen(b) && memcmp(a.data(), b, a.size()) == 0;
}
bool entry_eq(Hnode *lhs, Hnode *rhs);
uint32_t do_get(const std::vector<std::string> &cmd, uint8_t *res,
                uint32_t *reslen);
uint32_t do_set(const std::vector<std::string> &cmd, uint8_t *res,
                uint32_t *reslen);
uint32_t do_del(const std::vector<std::string> &cmd, uint8_t *res,
                uint32_t *reslen);
// 哈希表相关函数
void h_init(Htable *htab, size_t hsize);
void h_insert(Htable *htab, Hnode *hnode);
Hnode *h_detach(Htable *htab, Hnode **from);
void hm_help_resizing(Hmap *hmap);
Hnode *hm_lookup(Hmap *hmap, Hnode *hnode, bool (*cmp)(Hnode *, Hnode *));
void hm_insert(Hmap *hmap, Hnode *node);
void hm_start_resizing(Hmap *hmap);
Hnode *hm_pop(Hmap *hmap, Hnode *key, bool (*cmp)(Hnode *, Hnode *));
size_t hm_size(Hmap *hmap);
// 序列化相关函数
void out_nil(std::string &out);
void out_str(std::string &out, const std::string &val);
void out_int(std::string &out, int64_t val);
void out_err(std::string &out, int32_t code, const std::string &msg);
void out_arr(std::string &out, uint32_t n);

// 序列化重构处理不同命令函数
void h_scan(Htable *tab, void (*f)(Hnode *, void *), void *arg);

void cb_scan(Hnode *node, void *arg);
void do_keys(std::vector<std::string> &cmd, std::string &out);
void do_get(std::vector<std::string> &cmd, std::string &out);
void do_set(std::vector<std::string> &cmd, std::string &out);
void do_del(std::vector<std::string> &cmd, std::string &out);