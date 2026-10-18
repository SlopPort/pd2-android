#pragma once
#include <cstdint>
// MSVC bit-scan intrinsics for any 32-bit index type (clang's builtins want unsigned long*).
template<class T> inline unsigned char d2_BitScanReverse(T* index, uint32_t mask) { if (!mask) return 0; *index = (T)(31 - __builtin_clz(mask)); return 1; }
template<class T> inline unsigned char d2_BitScanForward(T* index, uint32_t mask) { if (!mask) return 0; *index = (T)__builtin_ctz(mask); return 1; }
#define _BitScanReverse d2_BitScanReverse
#define _BitScanForward d2_BitScanForward
