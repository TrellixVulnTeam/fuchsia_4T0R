// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include "arch/x86/user_copy.h"

#include <assert.h>
#include <lib/code_patching.h>
#include <string.h>
#include <trace.h>
#include <zircon/types.h>

#include <arch/user_copy.h>
#include <arch/x86.h>
#include <arch/x86/feature.h>
#include <kernel/thread.h>
#include <vm/vm.h>

#define LOCAL_TRACE 0

CODE_TEMPLATE(kStacInstruction, "stac")
CODE_TEMPLATE(kClacInstruction, "clac")
static const uint8_t kNopInstruction = 0x90;

extern "C" {

void fill_out_stac_instruction(const CodePatchInfo* patch) {
  const size_t kSize = 3;
  DEBUG_ASSERT(patch->dest_size == kSize);
  DEBUG_ASSERT(kStacInstructionEnd - kStacInstruction == kSize);
  if (g_x86_feature_has_smap) {
    memcpy(patch->dest_addr, kStacInstruction, kSize);
  } else {
    memset(patch->dest_addr, kNopInstruction, kSize);
  }
}

void fill_out_clac_instruction(const CodePatchInfo* patch) {
  const size_t kSize = 3;
  DEBUG_ASSERT(patch->dest_size == kSize);
  DEBUG_ASSERT(kClacInstructionEnd - kClacInstruction == kSize);
  if (g_x86_feature_has_smap) {
    memcpy(patch->dest_addr, kClacInstruction, kSize);
  } else {
    memset(patch->dest_addr, kNopInstruction, kSize);
  }
}
}

static inline bool ac_flag(void) { return x86_save_flags() & X86_FLAGS_AC; }

static bool can_access(const void* base, size_t len) {
  LTRACEF("can_access: base %p, len %zu\n", base, len);

  // We don't care about whether pages are actually mapped or what their
  // permissions are, as long as they are in the user address space.  We
  // rely on a page fault occurring if an actual permissions error occurs.
  DEBUG_ASSERT(x86_get_cr0() & X86_CR0_WP);
  return is_user_address_range((vaddr_t)base, len);
}

zx_status_t arch_copy_from_user(void* dst, const void* src, size_t len) {
  // If we have the SMAP feature, then AC should only be set when running
  // _x86_copy_to_or_from_user. If we don't have the SMAP feature, then we don't care if AC is set
  // or not.
  DEBUG_ASSERT(!g_x86_feature_has_smap || !ac_flag());

  if (!can_access(src, len))
    return ZX_ERR_INVALID_ARGS;

  thread_t* thr = get_current_thread();
  zx_status_t status = _x86_copy_to_or_from_user(dst, src, len, &thr->arch.page_fault_resume);

  DEBUG_ASSERT(!g_x86_feature_has_smap || !ac_flag());
  return status;
}

zx_status_t arch_copy_to_user(void* dst, const void* src, size_t len) {
  // If we have the SMAP feature, then AC should only be set when running
  // _x86_copy_to_or_from_user. If we don't have the SMAP feature, then we don't care if AC is set
  // or not.
  DEBUG_ASSERT(!g_x86_feature_has_smap || !ac_flag());

  if (!can_access(dst, len))
    return ZX_ERR_INVALID_ARGS;

  thread_t* thr = get_current_thread();
  zx_status_t status = _x86_copy_to_or_from_user(dst, src, len, &thr->arch.page_fault_resume);

  DEBUG_ASSERT(!g_x86_feature_has_smap || !ac_flag());
  return status;
}
