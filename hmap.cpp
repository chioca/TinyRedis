#include "hmap.h"
void fd_set_nb(int fd) {
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

void conn_put(std::vector<Conn *> &fd2conn, struct Conn *conn) {
  if (fd2conn.size() <= (size_t)conn->fd) {
    fd2conn.resize(conn->fd + 1);
  }
  fd2conn[conn->fd] = conn;
}

int32_t accept_new_conn(std::vector<Conn *> &fd2conn, int fd) {
  struct sockaddr_in client_addr{};
  socklen_t socklen = sizeof(client_addr);
  int connfd = accept(fd, (struct sockaddr *)&client_addr, &socklen);
  if (connfd < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      perror("accept()");
      return -1;
    }
    return -1;
  }
  fd_set_nb(connfd);
  struct Conn *conn = new Conn();
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
  return connfd;
}

void connection_io(Conn *conn) {
  if (conn->state == STATE_REQ) {
    state_req(conn);
  } else if (conn->state == STATE_RES) {
    state_res(conn);
  } else {
    assert(0);  // 不该出现这种情况
  }
}

void state_req(Conn *conn) {
  while (try_fill_buffer(conn)) {
  }
}
bool try_fill_buffer(Conn *conn) {
  assert(conn->rbuf_size < sizeof(conn->rbuf));
  ssize_t rv = 0;
  do {
    size_t cap = sizeof(conn->rbuf) - conn->rbuf_size;
    if (cap == 0) {
      // 读缓冲区满了，无法继续读
      errno = 0;
      return false;
    }
    rv = read(conn->fd, &conn->rbuf[conn->rbuf_size], cap);
  } while (rv < 0 && errno == EINTR);  // 只重试被信号中断的情况

  if (rv < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      // 没有数据可读
      return false;
    } else {
      perror("read() error");
      conn->state = STATE_END;
      return false;
    }
  }

  if (rv == 0) {
    if (conn->rbuf_size > 0) {
      perror("unexpected error");
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
bool try_one_request(Conn *conn) {
  // 尝试从缓冲区解析出一个请求
  if (conn->rbuf_size < 4) {
    // 缓冲区数据不够，下次循环再试试
    return false;
  }
  uint32_t len = 0;
  memcpy(&len, &conn->rbuf[0], 4);
  if (len > k_max_msg) {
    printf("too long");
    conn->state = STATE_END;
    return false;
  }
  if (4 + len > conn->rbuf_size) {
    // 缓冲区数据不够，下次循环再试试
    return false;
  }
  std::vector<std::string> cmd;
  if (0 != parse_req(&conn->rbuf[4], len, cmd)) {
    conn->state = STATE_END;
    return false;
  }

  // 拿到一个请求，处理一下
  std::string out;
  do_request(cmd, out);
  if (out.size() > k_max_msg) {
    conn->state = STATE_END;
    out.clear();
    out_err(out, ERR_2BIG, "response too long");
    return false;
  }
  // 生成回显响应
  uint32_t res_len = (uint32_t)out.size();
  memcpy(&conn->wbuf[0], &res_len, 4);
  memcpy(&conn->wbuf[4], out.data(), res_len);
  conn->wbuf_size = 4 + res_len;
  conn->wbuf_sent = 0;
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
void state_res(Conn *conn) {
  while (try_flush_buffer(conn)) {
  }
}

bool try_flush_buffer(Conn *conn) {
  ssize_t rv = 0;
  do {
    size_t remain = conn->wbuf_size - conn->wbuf_sent;
    if (remain == 0) {
      // 写缓冲区数据已经发送完毕
      conn->state = STATE_REQ;
      conn->wbuf_sent = 0;
      conn->wbuf_size = 0;
      return false;
    }
    rv = write(conn->fd, &conn->wbuf[conn->wbuf_sent], remain);
  } while (rv < 0 && errno == EINTR);

  if (rv < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return false;
    } else {
      perror("write() error");
      conn->state = STATE_END;
      return false;
    }
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
int32_t do_request(const uint8_t *req, uint32_t req_len, uint32_t *res_code,
                   uint8_t *res, uint32_t *res_len) {
  std::vector<std::string> cmd;
  int x = parse_req(req, req_len, cmd);
  if (0 != x) {
    perror("Bad Request");
    return -1;
  }

  if (cmd.size() == 2 && cmd_is(cmd[0], "get")) {
    *res_code = do_get(cmd, res, res_len);
  } else if (cmd.size() == 3 && cmd_is(cmd[0], "set")) {
    *res_code = do_set(cmd, res, res_len);
  } else if (cmd.size() == 2 && cmd_is(cmd[0], "del")) {
    *res_code = do_del(cmd, res, res_len);
  } else {
    // 不识别的命令
    *res_code = RES_ERR;
    const char *msg = "Unknown cmd";
    strcpy((char *)res, msg);
    *res_len = strlen(msg);
    return 0;
  }
  return 0;
}
int32_t parse_req(const uint8_t *req, uint32_t req_len,
                  std::vector<std::string> &cmd) {
  uint32_t cur = 0;
  uint32_t n = 0;  // 读取参数个数

  memcpy(&n, &req[cur], 4);
  cur += 4;
  for (uint32_t i = 0; i < n; ++i) {
    if (cur + 4 > req_len) {
      printf("error : 1");
      return -1;
    }
    uint32_t slen = 0;  // 读取参数长度
    memcpy(&slen, &req[cur], 4);
    cur += 4;
    if (cur + slen > req_len) {
      return -1;
    }
    cmd.emplace_back((const char *)&req[cur], slen);
    cur += slen;
  }
  if (cur != req_len) {
    perror("parese_req() error: length mismatch");
    return -1;
  }
  return 0;
}
bool entry_eq(Hnode *lhs, Hnode *rhs) {
  struct Entry *le = container_of(lhs, struct Entry, hnode);
  struct Entry *re = container_of(rhs, struct Entry, hnode);
  return lhs->hcode == rhs->hcode && le->key == re->key;
}
uint32_t do_get(const std::vector<std::string> &cmd, uint8_t *res,
                uint32_t *reslen) {
  Entry key_node;
  key_node.key = cmd[1];
  key_node.hnode.hcode = (uint64_t)std::hash<std::string>()(key_node.key);
  Hnode *node = hm_lookup(&g_data.db, &key_node.hnode, entry_eq);
  if (!node) {
    std::string data = std::string("no such key ") + key_node.key;
    memcpy(res, data.data(), data.size());
    *reslen = (uint32_t)data.size();
    return RES_NX;
  }
  std::string &val = container_of(node, Entry, hnode)->val;
  assert(val.size() <= k_max_msg);
  std::string data = std::string("res = ") + val;
  memcpy(res, data.data(), data.size());
  *reslen = (uint32_t)data.size();
  return RES_OK;
}

uint32_t do_set(const std::vector<std::string> &cmd, uint8_t *res,
                uint32_t *reslen) {
  (void)res;
  (void)reslen;
  // g_map[cmd[1]] = cmd[2];
  Entry key_node;
  key_node.key = cmd[1];
  key_node.hnode.hcode = (uint64_t)std::hash<std::string>()(key_node.key);
  Hnode *node = hm_lookup(&g_data.db, &key_node.hnode, entry_eq);
  std::ostringstream oss;
  if (node) {
    container_of(node, Entry, hnode)->val = cmd[2];
    oss << "update" << cmd[1] << "=" << cmd[2];
    std::string data = oss.str();
    memcpy(res, data.data(), data.size());
    *reslen = (uint32_t)data.size();
  } else {
    Entry *new_entry = new Entry();
    new_entry->key = cmd[1];
    new_entry->val = cmd[2];
    new_entry->hnode.hcode = key_node.hnode.hcode;
    hm_insert(&g_data.db, &new_entry->hnode);
    oss << "insert " << cmd[1] << "=" << cmd[2];
    std::string data = oss.str();
    memcpy(res, data.data(), data.size());
    *reslen = (uint32_t)data.size();
  }
  return RES_OK;
}

uint32_t do_del(const std::vector<std::string> &cmd, uint8_t *res,
                uint32_t *reslen) {
  (void)res;
  (void)reslen;
  // g_map.erase(cmd[1]);
  Entry key_node;
  key_node.key = cmd[1];
  key_node.hnode.hcode = (uint64_t)std::hash<std::string>()(key_node.key);
  Hnode *node = hm_pop(&g_data.db, &key_node.hnode, entry_eq);
  if (node) {
    std::string data = std::string("del ") + key_node.key;
    memcpy(res, data.data(), data.size());
    *reslen = (uint32_t)data.size();
    delete (container_of(node, Entry, hnode));
    return RES_OK;
  }
  std::string data = std::string("no such key ") + key_node.key;
  memcpy(res, data.data(), data.size());
  *reslen = (uint32_t)data.size();
  return RES_NX;
}

void h_init(Htable *htab, size_t hsize) {
  assert(hsize > 0 && (hsize & (hsize - 1)) == 0);  // hsize 必须是2的幂次
  htab->capacity = hsize;
  htab->mask = hsize - 1;
  htab->tab = new Hnode *[hsize];
  for (size_t i = 0; i < hsize; i++) {
    htab->tab[i] = NULL;
  }
}
void h_insert(Htable *htab, Hnode *hnode) {
  size_t idx = hnode->hcode & htab->mask;
  Hnode *ori = htab->tab[idx];
  hnode->next = ori;
  htab->tab[idx] = hnode;
  htab->count++;
}

Hnode **h_lookup(Htable *htab, Hnode *hnode, bool (*cmp)(Hnode *, Hnode *)) {
  if (!htab->tab) return NULL;
  size_t pos = hnode->hcode & htab->mask;
  Hnode **cur = &htab->tab[pos];
  while (*cur) {
    if (cmp(*cur, hnode)) {
      return cur;
    }
    cur = &(*cur)->next;
  }
  return NULL;
}
Hnode *h_detach(Htable *htab, Hnode **from) {
  if (!from || !*from) return NULL;
  Hnode *node = *from;
  *from = (*from)->next;
  if (htab->count > 0) htab->count--;
  node->next = NULL;
  return node;
}

Hnode *hm_lookup(Hmap *hmap, Hnode *hnode, bool (*cmp)(Hnode *, Hnode *)) {
  hm_help_resizing(hmap);
  Hnode **res = h_lookup(&hmap->htab1, hnode, cmp);
  if (!res) {
    res = h_lookup(&hmap->htab2, hnode, cmp);
  }
  return res ? *res : NULL;
}

void hm_help_resizing(Hmap *hmap) {
  if (!hmap) return;
  if (hmap->htab2.tab == NULL) {
    return;
  }
  size_t nwork = 0;  // 每次帮忙搬移一个桶
  while (nwork < k_resizing_work && hmap->htab2.tab &&
         hmap->htab2.capacity > 0) {
    /* code */
    if (hmap->resizing_pos >= hmap->htab2.capacity) {
      break;
    }
    Hnode **from = &hmap->htab2.tab[hmap->resizing_pos];
    if (!*from) {
      hmap->resizing_pos++;
      continue;
    }
    Hnode *moved = h_detach(&hmap->htab2, from);
    if (moved) {
      h_insert(&hmap->htab1, moved);
    }
    nwork++;
  }
  bool finished = true;
  if (hmap->htab2.tab) {
    for (size_t i = hmap->resizing_pos; i < hmap->htab2.capacity; i++) {
      if (hmap->htab2.tab[i]) {
        finished = false;
        break;
      }
    }
  }
  if (finished) {
    if (hmap->htab2.tab) {
      delete[] hmap->htab2.tab;
    }
    hmap->htab2 = Htable{};
    hmap->resizing_pos = 0;
  }
}

void hm_insert(Hmap *hmap, Hnode *node) {
  if (!hmap->htab1.tab) {
    h_init(&hmap->htab1, 4);
  }
  h_insert(&hmap->htab1, node);
  if (!hmap->htab2.tab) {
    // 检查是否需要调整大小
    size_t load_factor = hmap->htab1.count / hmap->htab1.capacity;
    if (load_factor > k_max_load_factor) {
      hm_start_resizing(hmap);
    }
  }
  hm_help_resizing(hmap);
}
void hm_start_resizing(Hmap *hmap) {
  assert(hmap != NULL);
  assert(hmap->htab2.tab == NULL);
  hmap->htab2 = hmap->htab1;
  size_t new_size = hmap->htab1.capacity == 0 ? 4 : hmap->htab1.capacity * 2;
  printf("start resizing to %zu\n", new_size);
  h_init(&hmap->htab1, new_size);
  hmap->resizing_pos = 0;
}
Hnode *hm_pop(Hmap *hmap, Hnode *key, bool (*cmp)(Hnode *, Hnode *)) {
  hm_help_resizing(hmap);
  Hnode **from = h_lookup(&hmap->htab1, key, cmp);
  if (from) {
    // from = h_lookup(&hmap->htab2, key, cmp);
    return h_detach(&hmap->htab1, from);
  } else {
    from = h_lookup(&hmap->htab2, key, cmp);
    return from ? h_detach(&hmap->htab2, from) : NULL;
  }
}
void do_request(std::vector<std::string> &cmd, std::string &out) {
  if (cmd.size() == 1 && cmd_is(cmd[0], "keys")) {
    do_keys(cmd, out);
  } else if (cmd.size() == 2 && cmd_is(cmd[0], "get")) {
    do_get(cmd, out);
  } else if (cmd.size() == 3 && cmd_is(cmd[0], "set")) {
    do_set(cmd, out);
  } else if (cmd.size() == 2 && cmd_is(cmd[0], "del")) {
    do_del(cmd, out);
  } else {
    // 无法识别的命令
    out_err(out, ERR_UNKNOWN, "Unknown cmd");
  }
}
void out_nil(std::string &out) { out.push_back(SER_NIL); }

void out_str(std::string &out, const std::string &val) {
  out.push_back(SER_STR);
  uint32_t len = (uint32_t)val.size();
  out.append((char *)&len, 4);
  out.append(val);
}

void out_int(std::string &out, int64_t val) {
  out.push_back(SER_INT);
  out.append((char *)&val, 8);
}

void out_err(std::string &out, int32_t code, const std::string &msg) {
  out.push_back(SER_ERR);
  out.append((char *)&code, 4);
  uint32_t len = (uint32_t)msg.size();
  out.append((char *)&len, 4);
  out.append(msg);
}

void out_arr(std::string &out, uint32_t n) {
  out.push_back(SER_ARR);
  out.append((char *)&n, 4);
}

void h_scan(Htable *tab, void (*f)(Hnode *, void *), void *arg) {
  if (tab->count == 0) {
    return;
  }
  for (size_t i = 0; i < tab->capacity; ++i) {
    Hnode *node = tab->tab[i];
    while (node) {
      f(node, arg);
      node = node->next;
    }
  }
}

void cb_scan(Hnode *node, void *arg) {
  std::string &out = *(std::string *)arg;
  out_str(out, container_of(node, Entry, hnode)->key);
}

void do_keys(std::vector<std::string> &cmd, std::string &out) {
  (void)cmd;
  out_arr(out, (uint32_t)hm_size(&g_data.db));
  h_scan(&g_data.db.htab1, &cb_scan, &out);
  h_scan(&g_data.db.htab2, &cb_scan, &out);
}
void do_get(std::vector<std::string> &cmd, std::string &out) {
  if (cmd.size() != 2) {
    out_err(out, ERR_ARGC, "argc != 2");
    return;
  }
  Entry key_node;
  key_node.key = cmd[1];
  key_node.hnode.hcode = (uint64_t)std::hash<std::string>()(key_node.key);
  Hnode *node = hm_lookup(&g_data.db, &key_node.hnode, entry_eq);
  if (!node) {
    out_err(out, ERR_NX, "no such key");
    return;
  }
  std::string &val = container_of(node, Entry, hnode)->val;
  out_str(out, val);
}
void do_set(std::vector<std::string> &cmd, std::string &out) {
  if (cmd.size() != 3) {
    out_err(out, ERR_ARGC, "argc != 3");
    return;
  }
  Entry key_node;
  key_node.key = cmd[1];
  key_node.hnode.hcode = (uint64_t)std::hash<std::string>()(key_node.key);
  Hnode *node = hm_lookup(&g_data.db, &key_node.hnode, entry_eq);
  if (node) {
    container_of(node, Entry, hnode)->val = cmd[2];
    out_str(out, std::string("update ") + cmd[1] + "=" + cmd[2]);
  } else {
    Entry *new_entry = new Entry();
    new_entry->key = cmd[1];
    new_entry->val = cmd[2];
    new_entry->hnode.hcode = key_node.hnode.hcode;
    hm_insert(&g_data.db, &new_entry->hnode);
    out_str(out, std::string("insert ") + cmd[1] + "=" + cmd[2]);
  }
}
void do_del(std::vector<std::string> &cmd, std::string &out) {
  if (cmd.size() != 2) {
    out_err(out, ERR_ARGC, "argc != 2");
    return;
  }
  Entry key_node;
  key_node.key = cmd[1];
  key_node.hnode.hcode = (uint64_t)std::hash<std::string>()(key_node.key);
  Hnode *node = hm_pop(&g_data.db, &key_node.hnode, entry_eq);
  if (node) {
    out_str(out, std::string("del ") + key_node.key);
    delete (container_of(node, Entry, hnode));
    return;
  }
  out_err(out, ERR_NX, "no such key");
}
size_t hm_size(Hmap *hmap) {
  if (!hmap) return 0;
  return hmap->htab1.count + hmap->htab2.count;
}
