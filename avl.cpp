#include "avl.h"

void avl_init(AVLNode *node) {
  node->depth = 1;
  node->cnt = 1;
  node->left = nullptr;
  node->right = nullptr;
  node->parent = nullptr;
}

AVLNode *rot_left(AVLNode *node) {
  AVLNode *new_node = node->right;
  if (new_node->left) {
    new_node->left->parent = node;
  }
  node->right = new_node->left;
  new_node->left = node;
  new_node->parent = node->parent;
  node->parent = new_node;
  avl_update(node);
  avl_update(new_node);
  return new_node;
}

AVLNode *rot_right(AVLNode *node) {
  AVLNode *new_node = node->left;
  if (new_node->right) {
    new_node->right->parent = node;
  }
  node->left = new_node->right;
  new_node->parent = node->parent;
  new_node->right = node;
  node->parent = new_node;
  avl_update(node);
  avl_update(new_node);
  return new_node;
}
AVLNode *avl_fix_left(AVLNode *root) {
  if (avl_depth(root->left->left) < avl_depth(root->left->right)) {
    root->left = rot_left(root->left);
  }
  return rot_right(root);
}

AVLNode *avl_fix_right(AVLNode *root) {
  if (avl_depth(root->right->right) > avl_depth(root->right->left)) {
    root->right = rot_right(root->right);
  }
  return rot_left(root);
}