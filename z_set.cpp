#include "z_set.h"

#include <cstring>

ZNode *znode_new(const char *name, size_t len, double score) {
  ZNode *node = (ZNode *)malloc(sizeof(ZNode) + len);
  avl_init(&node->tree);
  node->hmap.next = nullptr;
  node->hmap.hcode = (uint64_t)std::hash<std::string>()(name);
  node->score = score;
  node->len = len;
  memcpy(&node->name[0], name, len);
  return node;
}
static size_t min(size_t lhs, size_t rhs) { return lhs < rhs ? lhs : rhs; }

static void znode_del(ZNode *node) { free(node); }

static bool zless(AVLNode *lhs, double score, const char *name, size_t len) {
  ZNode *zl = container_of(lhs, ZNode, tree);
  if (zl->score != score) {
    return zl->score < score;
  }
  int rv = memcmp(zl->name, name, std::min(zl->len, len));
  if (rv != 0) {
    return rv < 0;
  }
  return zl->len < len;
}
static bool zless(AVLNode *lhs, AVLNode *rhs) {
  ZNode *from = container_of(rhs, ZNode, tree);
  return zless(lhs, from->score, from->name, from->len);
}

void tree_insert(ZSet *zset, ZNode *node) {
  AVLNode *parent = nullptr;
  AVLNode **from = &zset->root;
  avl_init(&node->tree);
  while (*from) {
    parent = *from;
    from = zless(&node->tree, parent) ? &parent->left : &parent->right;
  }
  *from = &node->tree;
  node->tree.parent = parent;
  zset->root = avl_fix(&node->tree);
}
