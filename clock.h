#pragma once

#include <time.h>

#include <cstdint>

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

inline uint64_t get_monotonic_usec() {
  timespec tv = {0, 0};
  clock_gettime(CLOCK_MONOTONIC, &tv);
  return uint64_t(tv.tv_sec) * 1000000 + tv.tv_nsec / 1000;
}