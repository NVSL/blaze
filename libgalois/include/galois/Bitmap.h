/*
Copyright (c) 2014-2015 Xiaowei Zhu, Tsinghua University

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#ifndef GALOIS_BITMAP_H
#define GALOIS_BITMAP_H

#include <string>
#include <iostream>

#define WORD_OFFSET(i) ((i) >> 6)
#define BIT_OFFSET(i) ((i) & 0x3f)

class Bitmap {
public:
  size_t size;
  unsigned long * data;
  Bitmap() {
    size = 0;
    data = NULL;
  }
  Bitmap(size_t size) {
    init(size);
  }
  ~Bitmap() {
    if (data) delete data;
  }
  void init(size_t size) {
    this->size = size;
    data = new unsigned long [WORD_OFFSET(size-1)+1];
  }
  void clear() {
    size_t bm_size = WORD_OFFSET(size-1);
    for (size_t i=0;i<=bm_size;i++) {
      data[i] = 0;
    }
  }
  void fill() {
    size_t bm_size = WORD_OFFSET(size-1);
    for (size_t i=0;i<bm_size;i++) {
      data[i] = 0xffffffffffffffff;
    }
    data[bm_size] = 0;
    for (size_t i=(bm_size<<6);i<size;i++) {
      data[bm_size] |= 1ul << BIT_OFFSET(i);
    }
  }
  size_t count() {
    size_t cnt = 0;
    for (size_t i=0; i<size; i++)
      if (get_bit(i)) cnt++;
    return cnt;
  }
  size_t count(size_t until) {
    size_t cnt = 0;
    for (size_t i=0; i<until; i++)
      if (get_bit(i)) cnt++;
    return cnt;
  }
  std::string to_str() {
    std::stringstream ss;
    for (size_t i=0; i<size; i++) {
      if (get_bit(i)) ss << "1"; 
      else            ss << "0"; 
    }
    return ss.str();
  }
  unsigned long get_bit(size_t i) {
    return data[WORD_OFFSET(i)] & (1ul<<BIT_OFFSET(i));
  }
  void set_bit(size_t i) {
    __sync_fetch_and_or(data+WORD_OFFSET(i), 1ul<<BIT_OFFSET(i));
  }
};

class BitmapArray {
public:
  size_t N;
  size_t B;
  size_t size;
  unsigned long * data;
  BitmapArray() {
    N = 0;
    B = 0;
    size = 0;
    data = NULL;
  }
  BitmapArray(size_t N, size_t size) {
    this->N    = N;
    this->size = size;
    this->B = WORD_OFFSET(size-1) + 1;
    this->data = new unsigned long [B * N];
  }
  ~BitmapArray() {
    if (this->data) delete data;
  }
  void clear() {
    for (size_t i = 0; i < N*B; i++) {
      data[i] = 0;
    }
  }
  void fill() {
    for (size_t i = 0; i < N*B; i++) {
      data[i] = 0xffffffffffffffff;
    }
  }
  size_t count(size_t idx) {
    unsigned long *item = &data[idx*B];
    size_t cnt = 0;
    for (size_t i=0; i<size; i++)
      if (_get_bit(item, i)) cnt++;
    return cnt;
  }
  std::string to_str(size_t idx) {
    std::stringstream ss;
    for (size_t i=0; i<size; i++) {
      if (get_bit(idx, i)) ss << "1";
      else                 ss << "0";
    }
    return ss.str();
  }
  unsigned long get_bit(size_t idx, size_t i) {
    unsigned long *item = &data[idx*B];
    return item[WORD_OFFSET(i)] & (1ul<<BIT_OFFSET(i));
  }
  void set_bit(size_t idx, size_t i) {
    unsigned long *item = &data[idx*B];
    __sync_fetch_and_or(item+WORD_OFFSET(i), 1ul<<BIT_OFFSET(i));
  }
  uint32_t bytesItem() const {
    return sizeof(unsigned long) * B;
  }
  size_t bytes() const {
    return bytesItem() * N;
  }
  void *ptr() const {
    return (void*)data;
  }

private:
  unsigned long _get_bit(unsigned long *item, size_t i) {
    return item[WORD_OFFSET(i)] & (1ul<<BIT_OFFSET(i));
  }
};

class MappedBitmapArray {
public:
  size_t N;
  size_t B;
  size_t size;
  unsigned long * data;
  MappedBitmapArray() {
    N = 0;
    B = 0;
    size = 0;
    data = NULL;
  }
  MappedBitmapArray(size_t N, size_t size, void *data) {
    init(N, size, data);
  }
  void init(size_t N, size_t size, void *data) {
    this->N    = N;
    this->size = size;
    this->B = WORD_OFFSET(size-1) + 1;
    this->data = (unsigned long *)data;
  }
  ~MappedBitmapArray() {}
  void clear() {
    for (size_t i = 0; i < N*B; i++) {
      data[i] = 0;
    }
  }
  void fill() {
    for (size_t i = 0; i < N*B; i++) {
      data[i] = 0xffffffffffffffff;
    }
  }
  size_t count(size_t idx) {
    unsigned long *item = &data[idx*B];
    size_t cnt = 0;
    for (size_t i=0; i<size; i++)
      if (_get_bit(item, i)) cnt++;
    return cnt;
  }
  std::string to_str(size_t idx) {
    std::stringstream ss;
    for (size_t i=0; i<size; i++) {
      if (get_bit(idx, i)) ss << "1";
      else                 ss << "0";
    }
    return ss.str();
  }
  unsigned long get_bit(size_t idx, size_t i) {
    unsigned long *item = &data[idx*B];
    return item[WORD_OFFSET(i)] & (1ul<<BIT_OFFSET(i));
  }
  void set_bit(size_t idx, size_t i) {
    unsigned long *item = &data[idx*B];
    __sync_fetch_and_or(item+WORD_OFFSET(i), 1ul<<BIT_OFFSET(i));
  }
  uint32_t bytesItem() const {
    return sizeof(unsigned long) * B;
  }
  size_t bytes() const {
    return bytesItem() * N;
  }
  void *ptr() const {
    return (void*)data;
  }

private:
  unsigned long _get_bit(unsigned long *item, size_t i) {
    return item[WORD_OFFSET(i)] & (1ul<<BIT_OFFSET(i));
  }
};

#endif
