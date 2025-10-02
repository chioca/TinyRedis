#include "z_set.h"

#include <cstring>

#include "common.h"
struct HKey {
  Hnode node;
  const char *name = NULL;
  size_t len = 0;
};
ZNode *znode_new(const char *name, size_t len, double score) {
  ZNode *node = (ZNode *)malloc(sizeof(ZNode) + len);
  avl_init(&node->tree);
  node->hmap.next = nullptr;
  node->hmap.hcode = str_hash((uint8_t *)name, len);
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

// 更新已有节点的score
static void zset_update(ZSet *zset, ZNode *node, double score) {
  if (node->score == score) {
    return;
  }

  zset->root = avl_del(&node->tree);
  avl_init(&node->tree);
  node->score = score;
  tree_insert(zset, node);
}

ZNode *zset_lookup(ZSet *zset, const char *name, size_t len) {
  if (!zset->root) {
    return nullptr;
  }

  HKey key;
  key.node.hcode = str_hash((uint8_t *)name, len);
  key.name = name;
  key.len = len;
  Hnode *found = hm_lookup(&zset->hmap, &key.node, entry_eq);
  return found ? container_of(found, ZNode, hmap) : nullptr;
}

bool zset_insert(ZSet *zset, const char *name, size_t len, double socre) {
  ZNode *node = zset_lookup(zset, name, len);
  if (node) {
    zset_update(zset, node, socre);
    return false;
  } else {
    node = znode_new(name, len, socre);
    hm_insert(&zset->hmap, &node->hmap);
    tree_insert(zset, node);
    return true;
  }
}