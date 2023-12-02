#pragma once

#include "prelude.h"

template <typename T> struct Chan {
  Mutex mtx = {};
  Cond cv = {};

  T *data = nullptr;
  u64 front = 0;
  u64 back = 0;
  u64 len = 0;
  u64 capacity = 0;

  void trash() { mem_free(data); }

  void reserve(u64 cap) {
    if (cap <= capacity) {
      return;
    }

    T *buf = (T *)mem_alloc(sizeof(T) * cap);

    u64 lhs = back;
    u64 rhs = (capacity - front);

    memcpy(buf, &data[front], sizeof(T) * rhs);
    memcpy(&buf[rhs], &data[0], sizeof(T) * lhs);

    mem_free(data);

    data = buf;
    front = 0;
    back = len;
    capacity = cap;
  }

  void send(T item) {
    mtx.lock();
    defer(mtx.unlock());

    if (len == capacity) {
      reserve(len > 0 ? len * 2 : 8);
    }

    data[back] = item;
    back = (back + 1) % capacity;
    len++;

    cv.signal();
  }

  T recv() {
    mtx.lock();
    defer(mtx.unlock());

    while (len == 0) {
      cv.wait(&mtx);
    }

    T item = data[front];
    front = (front + 1) % capacity;
    len--;

    return item;
  }
};
