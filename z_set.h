#include "avl.h"
#include "hmap.h"
struct ZSet {
  AVLNode *root = nullptr;
  Hmap hmap;
};

struct ZNode {
  AVLNode tree;
  Hnode hmap;
  double score = 0;
  size_t len = 0;
  char name[0];
};

ZNode *znode_new(const char *name, size_t len, double score);
void tree_add(ZSet *zset, ZNode *node);
