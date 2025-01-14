// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_ARCH_EXCEPTION_H_
#define ZIRCON_KERNEL_INCLUDE_ARCH_EXCEPTION_H_

#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <fbl/ref_ptr.h>

struct thread;
class ExceptionPort;
typedef struct arch_exception_context arch_exception_context_t;
typedef struct zx_exception_report zx_exception_report_t;

// Called by arch code when it cannot handle an exception.
// |context| is architecture-specific, and can be dumped to the console
// using arch_dump_exception_context(). Implemented by non-arch code.
zx_status_t dispatch_user_exception(uint exception_type, const arch_exception_context_t* context);

// Dispatches a debug exception to |eport|.
// The returned value is the result of calling
// |ThreadDispatcher::ExceptionHandlerExchange()|.
zx_status_t dispatch_debug_exception(fbl::RefPtr<ExceptionPort> eport, uint exception_type,
                                     const arch_exception_context_t* context);

// Dispatches an exception that was raised by a syscall using
// thread_signal_policy_exception() (see <kernel/thread.h>), causing
// dispatch_user_exception() to be called with the current context.
// Implemented by arch code.
zx_status_t arch_dispatch_user_policy_exception(void);

// Dumps architecture-specific state to the console. |context| typically comes
// from a call to dispatch_user_exception(). Implemented by arch code.
void arch_dump_exception_context(const arch_exception_context_t* context);

// Sets |report| using architecture-specific information from |context|.
// Implemented by arch code.
void arch_fill_in_exception_context(const arch_exception_context_t* context,
                                    zx_exception_report_t* report);

// Record registers in |context| as being available to
// |zx_thread_read_state(),zx_thread_write_state()|.
// This is called prior to the thread stopping in an exception, and must be
// matched with a corresponding call to |arch_remove_context_regs()| prior to
// the thread resuming execution.
void arch_install_context_regs(struct thread* thread, const arch_exception_context_t* context);

// Undo a previous call to |arch_install_context_regs()|.
void arch_remove_context_regs(struct thread* thread);

#endif  // ZIRCON_KERNEL_INCLUDE_ARCH_EXCEPTION_H_
