/*-*- Mode: C; c-basic-offset: 8 -*-*/

#ifndef fookanberramacrohfoo
#define fookanberramacrohfoo

/***
  This file is part of libkanberra.

  Copyright 2008 Lennart Poettering

  libkanberra is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation, either version 2.1 of the
  License, or (at your option) any later version.

  libkanberra is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with libkanberra. If not, see
  <http://www.gnu.org/licenses/>.
***/

#include <stdio.h>
#include <stdlib.h>

#ifndef PACKAGE
#error "Please include config.h before including this file!"
#endif

#if defined (__GNUC__) && __GNUC__ >= 3
#define KA_LIKELY(x) (__builtin_expect(!!(x),1))
#define KA_UNLIKELY(x) (__builtin_expect((x),0))
#else
#define KA_LIKELY(x) (x)
#define KA_UNLIKELY(x) (x)
#endif

#ifdef __GNUC__
#define KA_PRETTY_FUNCTION __PRETTY_FUNCTION__
#else
#define KA_PRETTY_FUNCTION ""
#endif

#define ka_return_if_fail(expr)                                         \
        do {                                                            \
                if (KA_UNLIKELY(!(expr))) {                             \
                        if (ka_debug())                                 \
                                fprintf(stderr, "Assertion '%s' failed at %s:%u, function %s().\n", #expr , __FILE__, __LINE__, KA_PRETTY_FUNCTION); \
                        return;                                         \
                }                                                       \
        } while(FALSE)

#define ka_return_val_if_fail(expr, val)                                \
        do {                                                            \
                if (KA_UNLIKELY(!(expr))) {                             \
                        if (ka_debug())                                 \
                                fprintf(stderr, "Assertion '%s' failed at %s:%u, function %s().\n", #expr , __FILE__, __LINE__, KA_PRETTY_FUNCTION); \
                        return (val);                                   \
                }                                                       \
        } while(FALSE)

#define ka_return_null_if_fail(expr) ka_return_val_if_fail(expr, NULL)

#define ka_return_if_fail_unlock(expr, mutex)                           \
        do {                                                            \
                if (KA_UNLIKELY(!(expr))) {                             \
                        if (ka_debug())                                 \
                                fprintf(stderr, "Assertion '%s' failed at %s:%u, function %s().\n", #expr , __FILE__, __LINE__, KA_PRETTY_FUNCTION); \
                        ka_mutex_unlock(mutex);                         \
                        return;                                         \
                }                                                       \
        } while(FALSE)

#define ka_return_val_if_fail_unlock(expr, val, mutex)                  \
        do {                                                            \
                if (KA_UNLIKELY(!(expr))) {                             \
                        if (ka_debug())                                 \
                                fprintf(stderr, "Assertion '%s' failed at %s:%u, function %s().\n", #expr , __FILE__, __LINE__, KA_PRETTY_FUNCTION); \
                        ka_mutex_unlock(mutex);                         \
                        return (val);                                   \
                }                                                       \
        } while(FALSE)

#define ka_return_null_if_fail_unlock(expr, mutex) ka_return_val_if_fail_unlock(expr, NULL, mutex)

/* An assert which guarantees side effects of x, i.e. is never
 * optimized away */
#define ka_assert_se(expr)                                              \
        do {                                                            \
                if (KA_UNLIKELY(!(expr))) {                             \
                        fprintf(stderr, "Assertion '%s' failed at %s:%u, function %s(). Aborting.\n", #expr , __FILE__, __LINE__, KA_PRETTY_FUNCTION); \
                        abort();                                        \
                }                                                       \
        } while (FALSE)

/* An assert that may be optimized away by defining NDEBUG */
#ifdef NDEBUG
#define ka_assert(expr) do {} while (FALSE)
#else
#define ka_assert(expr) ka_assert_se(expr)
#endif

#define ka_assert_not_reached()                                         \
        do {                                                            \
                fprintf(stderr, "Code should not be reached at %s:%u, function %s(). Aborting.\n", __FILE__, __LINE__, KA_PRETTY_FUNCTION); \
                abort();                                                \
        } while (FALSE)

#define KA_ELEMENTSOF(x) (sizeof(x)/sizeof((x)[0]))

#ifdef __GNUC__
#define KA_MAX(a,b)                             \
        __extension__ ({ typeof(a) _a = (a);    \
                        typeof(b) _b = (b);     \
                        _a > _b ? _a : _b;      \
                })
#else
#define KA_MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#ifdef __GNUC__
#define KA_MIN(a,b)                             \
        __extension__ ({ typeof(a) _a = (a);    \
                        typeof(b) _b = (b);     \
                        _a < _b ? _a : _b;      \
                })
#else
#define KA_MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifdef __GNUC__
#define KA_CLAMP(x, low, high)                                          \
        __extension__ ({ typeof(x) _x = (x);                            \
                        typeof(low) _low = (low);                       \
                        typeof(high) _high = (high);                    \
                        ((_x > _high) ? _high : ((_x < _low) ? _low : _x)); \
                })
#else
#define KA_CLAMP(x, low, high) (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))
#endif

#ifndef FALSE
#define FALSE (0)
#endif

#ifndef TRUE
#define TRUE (!FALSE)
#endif

#define KA_PTR_TO_UINT(p) ((unsigned int) (unsigned long) (p))
#define KA_UINT_TO_PTR(u) ((void*) (unsigned long) (u))

#define KA_PTR_TO_UINT32(p) ((uint32_t) KA_PTR_TO_UINT(p))
#define KA_UINT32_TO_PTR(u) KA_UINT_TO_PTR((uint32_t) u)

#define KA_PTR_TO_INT(p) ((int) KA_PTR_TO_UINT(p))
#define KA_INT_TO_PTR(u) KA_UINT_TO_PTR((int) u)

#define KA_PTR_TO_INT32(p) ((int32_t) KA_PTR_TO_UINT(p))
#define KA_INT32_TO_PTR(u) KA_UINT_TO_PTR((int32_t) u)

typedef int ka_bool_t;

ka_bool_t ka_debug(void);

static inline size_t ka_align(size_t l) {
        return (((l + sizeof(void*) - 1) / sizeof(void*)) * sizeof(void*));
}

#define KA_ALIGN(x) (ka_align(x))

typedef void (*ka_free_cb_t)(void *);

#ifdef HAVE_BYTESWAP_H
#include <byteswap.h>
#endif

#ifdef HAVE_BYTESWAP_H
 #define KA_INT16_SWAP(x) ((int16_t) bswap_16((uint16_t) x))
 #define KA_UINT16_SWAP(x) ((uint16_t) bswap_16((uint16_t) x))
 #define KA_INT32_SWAP(x) ((int32_t) bswap_32((uint32_t) x))
 #define KA_UINT32_SWAP(x) ((uint32_t) bswap_32((uint32_t) x))
#else
 #define KA_INT16_SWAP(x) ( (int16_t) ( ((uint16_t) x >> 8) | ((uint16_t) x << 8) ) )
 #define KA_UINT16_SWAP(x) ( (uint16_t) ( ((uint16_t) x >> 8) | ((uint16_t) x << 8) ) )
 #define KA_INT32_SWAP(x) ( (int32_t) ( ((uint32_t) x >> 24) | ((uint32_t) x << 24) | (((uint32_t) x & 0xFF00) << 8) | ((((uint32_t) x) >> 8) & 0xFF00) ) )
 #define KA_UINT32_SWAP(x) ( (uint32_t) ( ((uint32_t) x >> 24) | ((uint32_t) x << 24) | (((uint32_t) x & 0xFF00) << 8) | ((((uint32_t) x) >> 8) & 0xFF00) ) )
#endif

#ifdef WORDS_BIGENDIAN
 #define KA_INT16_FROM_LE(x) KA_INT16_SWAP(x)
 #define KA_INT16_FROM_BE(x) ((int16_t)(x))

 #define KA_INT16_TO_LE(x) KA_INT16_SWAP(x)
 #define KA_INT16_TO_BE(x) ((int16_t)(x))

 #define KA_UINT16_FROM_LE(x) KA_UINT16_SWAP(x)
 #define KA_UINT16_FROM_BE(x) ((uint16_t)(x))

 #define KA_UINT16_TO_LE(x) KA_UINT16_SWAP(x)
 #define KA_UINT16_TO_BE(x) ((uint16_t)(x))

 #define KA_INT32_FROM_LE(x) KA_INT32_SWAP(x)
 #define KA_INT32_FROM_BE(x) ((int32_t)(x))

 #define KA_INT32_TO_LE(x) KA_INT32_SWAP(x)
 #define KA_INT32_TO_BE(x) ((int32_t)(x))

 #define KA_UINT32_FROM_LE(x) KA_UINT32_SWAP(x)
 #define KA_UINT32_FROM_BE(x) ((uint32_t)(x))

 #define KA_UINT32_TO_LE(x) KA_UINT32_SWAP(x)
 #define KA_UINT32_TO_BE(x) ((uint32_t)(x))
#else
 #define KA_INT16_FROM_LE(x) ((int16_t)(x))
 #define KA_INT16_FROM_BE(x) KA_INT16_SWAP(x)

 #define KA_INT16_TO_LE(x) ((int16_t)(x))
 #define KA_INT16_TO_BE(x) KA_INT16_SWAP(x)

 #define KA_UINT16_FROM_LE(x) ((uint16_t)(x))
 #define KA_UINT16_FROM_BE(x) KA_UINT16_SWAP(x)

 #define KA_UINT16_TO_LE(x) ((uint16_t)(x))
 #define KA_UINT16_TO_BE(x) KA_UINT16_SWAP(x)

 #define KA_INT32_FROM_LE(x) ((int32_t)(x))
 #define KA_INT32_FROM_BE(x) KA_INT32_SWAP(x)

 #define KA_INT32_TO_LE(x) ((int32_t)(x))
 #define KA_INT32_TO_BE(x) KA_INT32_SWAP(x)

 #define KA_UINT32_FROM_LE(x) ((uint32_t)(x))
 #define KA_UINT32_FROM_BE(x) KA_UINT32_SWAP(x)

 #define KA_UINT32_TO_LE(x) ((uint32_t)(x))
 #define KA_UINT32_TO_BE(x) KA_UINT32_SWAP(x)
#endif

#define ka_streq(a, b) (strcmp((a),(b)) == 0)

#ifdef __GNUC__
#define KA_GCC_DESTRUCTOR __attribute__ ((destructor))
#else
#undef KA_GCC_DESTRUCTOR
#endif

#endif
