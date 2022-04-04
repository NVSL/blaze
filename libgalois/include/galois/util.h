/*
 * This file belongs to the Galois project, a C++ library for exploiting parallelism.
 * The code is being released under the terms of the 3-Clause BSD License (a
 * copy is located in LICENSE.txt at the top-level directory).
 *
 * Copyright (C) 2018, The University of Texas at Austin. All rights reserved.
 * UNIVERSITY EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES CONCERNING THIS
 * SOFTWARE AND DOCUMENTATION, INCLUDING ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR ANY PARTICULAR PURPOSE, NON-INFRINGEMENT AND WARRANTIES OF
 * PERFORMANCE, AND ANY WARRANTY THAT MIGHT OTHERWISE ARISE FROM COURSE OF
 * DEALING OR USAGE OF TRADE.  NO WARRANTY IS EITHER EXPRESS OR IMPLIED WITH
 * RESPECT TO THE USE OF THE SOFTWARE OR DOCUMENTATION. Under no circumstances
 * shall University be liable for incidental, special, indirect, direct or
 * consequential damages or loss of profits, interruption of business, or
 * related expenses which may arise from use of Software or Documentation,
 * including but not limited to those resulting from defects in Software and/or
 * Documentation, or loss or inaccuracy of data of any kind.
 */

#ifndef GALOIS_UTIL_H_
#define GALOIS_UTIL_H_

#include <string>
#include <sstream>
#include <immintrin.h>

namespace galois {

template <typename C>
void splitCSVstr(const std::string& inputStr, C& output,
                 const char delim = ',') {
  std::stringstream ss(inputStr);

  for (std::string item; std::getline(ss, item, delim);) {
    output.push_back(item);
  }
}

inline void doPrefetch(char* addr, size_t len) {
  char *end = addr + len;
  while (addr < end) {
    _mm_prefetch(addr, _MM_HINT_T0);
    addr += 64;
  }
}

inline void prefetch_range(void *base, long idx, size_t len)
{
    asm volatile (
        "xor    %%r8, %%r8 \n"      /* r8: start addr */
        "xor    %%r9, %%r9 \n"      /* r9:  */
        "lea    (%[base], %[idx], 4), %%r8 \n"
"LOOP_RANGE%=: \n"
        "prefetcht0  (%%r8) \n"
        "add    $0x40, %%r8 \n"
        "add    $0x40, %%r9 \n"
        "cmp    %[len], %%r9 \n"
        "jl     LOOP_RANGE%= \n"
        :
        : [base]"r"(base), [idx]"r"(idx), [len]"r"(len)
        : "r8", "r9");
}

//inline void prefetch_range_gather(void *base, long idx, size_t count, void *base2)
//{
//    asm volatile (
//        "xor    %%r8, %%r8 \n"      /* r8: start addr */
//        "xor    %%r9, %%r9 \n"      /* r9: value in base */
//        "xor    %%r10, %%r10 \n"    /* r10: addr in base2 */
//        "xor    %%r11, %%r11 \n"    /* r11: count */
//        "lea    (%[base], %[idx], 4), %%r8 \n"
//"LOOP_RANGE%=: \n"
//        "mov    (%%r8), %%r9 \n"
//        "lea    (%[base2], %%r9, 4), %%r10 \n"
//        "prefetcht0  (%%r10) \n"
//        "add    $0x4, %%r8 \n"
//        "inc    %%r11 \n"
//        "cmp    %[count], %%r11 \n"
//        "jl     LOOP_RANGE%= \n"
//        :
//        : [base]"r"(base), [idx]"r"(idx), [count]"r"(count), [base2]"r"(base2)
//        : "r8", "r9", "r10", "r11");
//}

inline void prefetch_range_gather(const void *base, long idx, size_t count, void *base2)
{
    for (size_t i = 0; i < count; i += 8) {
      volatile __m256i vidx = _mm256_loadu_si256((__m256i*)((char*)base + (idx + i) * sizeof(int)));
      _mm256_i32gather_ps((float *)base2, vidx, 4);
      //volatile __m256 av    = _mm256_i32gather_ps((float *)base2, vidx, 4);
    }
}

inline void prefetch_cacheline_gather1(void* mem_addr, void* base)
{
    __m512i idx512 = _mm512_loadu_si512(mem_addr);

    for (int i = 0; i < 4; i++) {
        __m128i idx128 = _mm512_extracti32x4_epi32(idx512, i);
        int idx0 = _mm_extract_epi32(idx128, 0);
        int idx1 = _mm_extract_epi32(idx128, 1);
        int idx2 = _mm_extract_epi32(idx128, 2);
        int idx3 = _mm_extract_epi32(idx128, 3);
        _mm_prefetch((char*)base + idx0*4, _MM_HINT_T0);
        _mm_prefetch((char*)base + idx1*4, _MM_HINT_T0);
        _mm_prefetch((char*)base + idx2*4, _MM_HINT_T0);
        _mm_prefetch((char*)base + idx3*4, _MM_HINT_T0);
    }
}

inline void prefetch_cacheline_gather2(void* mem_addr, void* base)
{
    __m512i idx512 = _mm512_loadu_si512(mem_addr);
    __m128i idx128;
    int idx;

    idx128 = _mm512_extracti32x4_epi32(idx512, 0);
    idx    = _mm_extract_epi32(idx128, 0);
    _mm_prefetch((char*)base + idx*4, _MM_HINT_T0);
    idx    = _mm_extract_epi32(idx128, 1);
    _mm_prefetch((char*)base + idx*4, _MM_HINT_T0);
    idx    = _mm_extract_epi32(idx128, 2);
    _mm_prefetch((char*)base + idx*4, _MM_HINT_T0);
    idx    = _mm_extract_epi32(idx128, 3);
    _mm_prefetch((char*)base + idx*4, _MM_HINT_T0);

    idx128 = _mm512_extracti32x4_epi32(idx512, 1);
    idx    = _mm_extract_epi32(idx128, 0);
    _mm_prefetch((char*)base + idx*4, _MM_HINT_T0);
    idx    = _mm_extract_epi32(idx128, 1);
    _mm_prefetch((char*)base + idx*4, _MM_HINT_T0);
    idx    = _mm_extract_epi32(idx128, 2);
    _mm_prefetch((char*)base + idx*4, _MM_HINT_T0);
    idx    = _mm_extract_epi32(idx128, 3);
    _mm_prefetch((char*)base + idx*4, _MM_HINT_T0);

    idx128 = _mm512_extracti32x4_epi32(idx512, 2);
    idx    = _mm_extract_epi32(idx128, 0);
    _mm_prefetch((char*)base + idx*4, _MM_HINT_T0);
    idx    = _mm_extract_epi32(idx128, 1);
    _mm_prefetch((char*)base + idx*4, _MM_HINT_T0);
    idx    = _mm_extract_epi32(idx128, 2);
    _mm_prefetch((char*)base + idx*4, _MM_HINT_T0);
    idx    = _mm_extract_epi32(idx128, 3);
    _mm_prefetch((char*)base + idx*4, _MM_HINT_T0);

    idx128 = _mm512_extracti32x4_epi32(idx512, 3);
    idx    = _mm_extract_epi32(idx128, 0);
    _mm_prefetch((char*)base + idx*4, _MM_HINT_T0);
    idx    = _mm_extract_epi32(idx128, 1);
    _mm_prefetch((char*)base + idx*4, _MM_HINT_T0);
    idx    = _mm_extract_epi32(idx128, 2);
    _mm_prefetch((char*)base + idx*4, _MM_HINT_T0);
    idx    = _mm_extract_epi32(idx128, 3);
    _mm_prefetch((char*)base + idx*4, _MM_HINT_T0);
}
inline void prefetch_range_all(void* base_edge, const size_t &start, const size_t &len, void* base_vertex)
{
    uint32_t *addr = (uint32_t*)base_edge + start;
    uint32_t *end = addr + len;
    //printf("AllPrefetcher: addr %p, len %lu\n", addr, len*sizeof(uint32_t));
    while (addr < end) {
        //prefetch_cacheline_gather2(addr, base_vertex);
        _mm_prefetch(addr, _MM_HINT_T0);
        addr += 16;
    }
}

inline void ntstore_64byte(void* dst, void* src) {
  asm volatile(
    "mov      %[src], %%rsi   \n"
    "vmovdqa  0*32(%%rsi), %%ymm0 \n"
    "vmovdqa  1*32(%%rsi), %%ymm1 \n"
    "mov      %[dst], %%rsi   \n"
    "vmovntpd %%ymm0, 0*32(%%rsi) \n"
    "vmovntpd %%ymm1, 1*32(%%rsi) \n"
    :
    : [dst] "r" (dst), [src] "r" (src)
    : "rsi", "ymm0", "ymm1");
}

inline void ntstore_256byte(void* dst, void* src) {
  char *d = (char*)dst;
  char *s = (char*)src;
  ntstore_64byte(d, s);
  d += 64; s += 64;
  ntstore_64byte(d, s);
  d += 64; s += 64;
  ntstore_64byte(d, s);
  d += 64; s += 64;
  ntstore_64byte(d, s);
}


} // namespace galois

#endif // GALOIS_UTIL_H_
