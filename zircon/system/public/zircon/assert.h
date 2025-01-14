// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_ASSERT_
#define SYSROOT_ZIRCON_ASSERT_

#ifdef _KERNEL
#include <assert.h>
#define ZX_PANIC(args...) PANIC(args)
#define ZX_ASSERT(args...) ASSERT(args)
#define ZX_ASSERT_MSG(args...) ASSERT_MSG(args)
#define ZX_DEBUG_ASSERT(args...) DEBUG_ASSERT(args)
#define ZX_DEBUG_ASSERT_MSG(args...) DEBUG_ASSERT_MSG(args)
#define ZX_DEBUG_ASSERT_COND(args...) DEBUG_ASSERT_COND(args)
#define ZX_DEBUG_ASSERT_MSG_COND(args...) DEBUG_ASSERT_MSG_COND(args)
#define ZX_DEBUG_ASSERT_IMPLEMENTED DEBUG_ASSERT_IMPLEMENTED

#ifdef ZX_DEBUGLEVEL
#undef ZX_DEBUGLEVEL
#endif
#define ZX_DEBUGLEVEL LK_DEBUGLEVEL

#else  // #ifdef _KERNEL

// TODO(ZX-4798): (dustingreen or mcgrathr for context; dustingreen to fix,
// mcgrathr probably as reviewer) These are no longer locally needed, so can be
// removed in a separate CL that will need wide OWNERS approval since many files
// are implicitly depending on these.
#include <stdio.h>
#include <stdlib.h>

#include <zircon/compiler.h>

__BEGIN_CDECLS
void __zx_panic(const char* format, ...) __NO_RETURN __PRINTFLIKE(1, 2);
__END_CDECLS

#define ZX_PANIC(fmt, ...) __zx_panic((fmt), ##__VA_ARGS__)

#define ZX_ASSERT(x)                                                      \
  do {                                                                    \
    if (unlikely(!(x))) {                                                 \
      ZX_PANIC("ASSERT FAILED at (%s:%d): %s\n", __FILE__, __LINE__, #x); \
    }                                                                     \
  } while (0)

#define ZX_ASSERT_MSG(x, msg, msgargs...)                                                     \
  do {                                                                                        \
    if (unlikely(!(x))) {                                                                     \
      ZX_PANIC("ASSERT FAILED at (%s:%d): %s\n" msg "\n", __FILE__, __LINE__, #x, ##msgargs); \
    }                                                                                         \
  } while (0)

// conditionally implement DEBUG_ASSERT based on ZX_DEBUGLEVEL in kernel space
// user space does not currently implement DEBUG_ASSERT
#ifdef ZX_DEBUGLEVEL
#define ZX_DEBUG_ASSERT_IMPLEMENTED (ZX_DEBUGLEVEL > 1)
#else
#define ZX_DEBUG_ASSERT_IMPLEMENTED 0
#endif

#define ZX_DEBUG_ASSERT(x)                                                      \
  do {                                                                          \
    if (ZX_DEBUG_ASSERT_IMPLEMENTED && unlikely(!(x))) {                        \
      ZX_PANIC("DEBUG ASSERT FAILED at (%s:%d): %s\n", __FILE__, __LINE__, #x); \
    }                                                                           \
  } while (0)

#define ZX_DEBUG_ASSERT_MSG(x, msg, msgargs...)                                         \
  do {                                                                                  \
    if (ZX_DEBUG_ASSERT_IMPLEMENTED && unlikely(!(x))) {                                \
      ZX_PANIC("DEBUG ASSERT FAILED at (%s:%d): %s\n" msg "\n", __FILE__, __LINE__, #x, \
               ##msgargs);                                                              \
    }                                                                                   \
  } while (0)

// implement _COND versions of ZX_DEBUG_ASSERT which only emit the body if
// ZX_DEBUG_ASSERT_IMPLEMENTED is set
#if ZX_DEBUG_ASSERT_IMPLEMENTED
#define ZX_DEBUG_ASSERT_COND(x) ZX_DEBUG_ASSERT(x)
#define ZX_DEBUG_ASSERT_MSG_COND(x, msg, msgargs...) ZX_DEBUG_ASSERT_MSG(x, msg, msgargs)
#else
#define ZX_DEBUG_ASSERT_COND(x) \
  do {                          \
  } while (0)
#define ZX_DEBUG_ASSERT_MSG_COND(x, msg, msgargs...) \
  do {                                               \
  } while (0)
#endif
#endif  // #ifdef _KERNEL

#endif  // SYSROOT_ZIRCON_ASSERT_
