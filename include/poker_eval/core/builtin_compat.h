/*
 * builtin_compat.h - Cross-compiler compatibility for GCC built-in functions
 *
 * Provides MSVC equivalents for commonly used GCC built-in functions:
 * - __builtin_popcount / __builtin_popcountll
 * - __builtin_clz / __builtin_clzll
 * - __builtin_ctz / __builtin_ctzll
 */

#ifndef POKER_EVAL_BUILTIN_COMPAT_H
#define POKER_EVAL_BUILTIN_COMPAT_H

#ifdef _MSC_VER
    #include <intrin.h>
    #include <stdint.h>

    /* Population count (number of 1 bits) */
    static __inline int __builtin_popcount(unsigned int x) {
        return (int)__popcnt(x);
    }

    static __inline int __builtin_popcountll(unsigned long long x) {
        #ifdef _WIN64
            return (int)__popcnt64(x);
        #else
            return __popcnt((unsigned int)x) + __popcnt((unsigned int)(x >> 32));
        #endif
    }

    /* Count leading zeros */
    static __inline int __builtin_clz(unsigned int x) {
        unsigned long index;
        if (_BitScanReverse(&index, x)) {
            return 31 - (int)index;
        }
        return 32;  /* Undefined behavior in GCC, but return 32 for safety */
    }

    static __inline int __builtin_clzll(unsigned long long x) {
        unsigned long index;
        #ifdef _WIN64
            if (_BitScanReverse64(&index, x)) {
                return 63 - (int)index;
            }
        #else
            if (_BitScanReverse(&index, (unsigned long)(x >> 32))) {
                return 31 - (int)index;
            }
            if (_BitScanReverse(&index, (unsigned long)x)) {
                return 63 - (int)index;
            }
        #endif
        return 64;
    }

    /* Count trailing zeros */
    static __inline int __builtin_ctz(unsigned int x) {
        unsigned long index;
        if (_BitScanForward(&index, x)) {
            return (int)index;
        }
        return 32;
    }

    static __inline int __builtin_ctzll(unsigned long long x) {
        unsigned long index;
        #ifdef _WIN64
            if (_BitScanForward64(&index, x)) {
                return (int)index;
            }
        #else
            if (_BitScanForward(&index, (unsigned long)x)) {
                return (int)index;
            }
            if (_BitScanForward(&index, (unsigned long)(x >> 32))) {
                return 32 + (int)index;
            }
        #endif
        return 64;
    }

    #ifndef __builtin_prefetch
    #define __builtin_prefetch(addr, rw, locality) ((void)(addr))
    #endif
#endif /* _MSC_VER */

#endif /* POKER_EVAL_BUILTIN_COMPAT_H */
