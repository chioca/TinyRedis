struct DList {
  DList *prev;
  DList *next;
};

inline void dlist_init(DList *node) { node->prev = node->next = node; }

inline bool dlist_empty(DList *node) { return node->next == node; }

inline void dlist_detach(DList *node) {
  DList *prev = node->prev;
  DList *next = node->next;
  prev->next = next;
  next->prev = prev;
}

inline void dlist_insert_before(DList *target, DList *node) {
  DList *prev = target->prev;
  prev->next = node;
  node->prev = prev;
  node->next = target;
  target->prev = node;
}