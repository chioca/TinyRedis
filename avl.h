#pragma once
#include <cstdint>
struct AVLNode {
  uint32_t depth = 0;
  uint32_t cnt = 0;
  AVLNode *left = nullptr;
  AVLNode *right = nullptr;
  AVLNode *parent = nullptr;
};

void avl_init(AVLNode *node);
inline uint32_t avl_depth(AVLNode *node) { return node ? node->depth : 0; }
inline uint32_t avl_cnt(AVLNode *node) { return node ? node->cnt : 0; }
inline uint32_t max(uint32_t lhs, uint32_t rhs) {
  return lhs > rhs ? lhs : rhs;
}
inline void avl_update(AVLNode *node) {
  if (node) {
    node->depth = 1 + max(avl_depth(node->left), avl_depth(node->right));
    node->cnt = 1 + avl_cnt(node->left) + avl_cnt(node->right);
  }
}
AVLNode *rot_left(AVLNode *node);
AVLNode *rot_right(AVLNode *node);

AVLNode *avl_fix_left(AVLNode *root);
AVLNode *avl_fix_right(AVLNode *root);