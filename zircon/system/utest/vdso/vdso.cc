// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <elfload/elfload.h>
#include <fbl/array.h>
#include <lib/zx/process.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <limits.h>
#include <new>
#include <string.h>
#include <unittest/unittest.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

static const zx::vmo vdso_vmo{zx_take_startup_handle(PA_HND(PA_VMO_VDSO, 0))};

class ScratchPad {
 public:
  ScratchPad() = delete;
  ScratchPad(const char* name) {
    EXPECT_EQ(
        zx_process_create(zx_job_default(), name, static_cast<uint32_t>(strlen(name)), 0,
                          process_.reset_and_get_address(), root_vmar_.reset_and_get_address()),
        ZX_OK, "zx_process_create");
  }

  const zx::vmar& root_vmar() const { return root_vmar_; }
  uintptr_t vdso_base() const { return vdso_base_; }
  uintptr_t vdso_code_offset() const { return vdso_code_offset_; }
  uintptr_t vdso_code_size() const { return vdso_code_size_; }
  uintptr_t vdso_code_address() const { return vdso_base_ + vdso_code_offset_; }
  uintptr_t vdso_total_size() const { return vdso_code_offset_ + vdso_code_size_; }

  zx_status_t load_vdso(zx::vmar* segments_vmar = nullptr, bool really_load = true) {
    elf_load_header_t header;
    uintptr_t phoff;
    zx_status_t status = elf_load_prepare(vdso_vmo.get(), nullptr, 0, &header, &phoff);
    if (status == ZX_OK) {
      fbl::Array<elf_phdr_t> phdrs(new elf_phdr_t[header.e_phnum], header.e_phnum);
      status = elf_load_read_phdrs(vdso_vmo.get(), phdrs.get(), phoff, header.e_phnum);
      if (status == ZX_OK) {
        for (const auto& ph : phdrs) {
          if (ph.p_type == PT_LOAD && (ph.p_type & PF_X)) {
            vdso_code_offset_ = ph.p_vaddr;
            vdso_code_size_ = ph.p_memsz;
          }
        }
        if (really_load) {
          status = elf_load_map_segments(
              root_vmar_.get(), &header, phdrs.get(), vdso_vmo.get(),
              segments_vmar ? segments_vmar->reset_and_get_address() : nullptr, &vdso_base_,
              nullptr);
        }
      }
    }
    return status;
  }

  zx_status_t compute_vdso_sizes() { return load_vdso(nullptr, false); }

 private:
  zx::process process_;
  zx::vmar root_vmar_;
  uintptr_t vdso_base_ = 0;
  uintptr_t vdso_code_offset_ = 0;
  uintptr_t vdso_code_size_ = 0;
};

bool vdso_map_twice_test() {
  BEGIN_TEST;

  ScratchPad scratch(__func__);

  // Load the vDSO once.  That's on me.
  EXPECT_EQ(scratch.load_vdso(), ZX_OK, "load vDSO into empty process");

  // Load the vDSO twice.  Can't get loaded again.
  EXPECT_EQ(scratch.load_vdso(), ZX_ERR_ACCESS_DENIED, "load vDSO second time");

  END_TEST;
}

bool vdso_map_change_test() {
  BEGIN_TEST;

  ScratchPad scratch(__func__);

  // Load the vDSO and hold onto the sub-VMAR.
  zx::vmar vdso_vmar;
  EXPECT_EQ(scratch.load_vdso(&vdso_vmar), ZX_OK, "load vDSO");

  // Changing protections on the code pages is forbidden.
  EXPECT_EQ(
      vdso_vmar.protect(scratch.vdso_code_address(), scratch.vdso_code_size(), ZX_VM_PERM_READ),
      ZX_ERR_ACCESS_DENIED, "zx_vmar_protect on vDSO code");

  zx::vmo vmo;
  ASSERT_EQ(zx::vmo::create(scratch.vdso_total_size(), 0, &vmo), ZX_OK, "zx_vmo_create");

  // Implicit unmapping by overwriting the mapping is forbidden.
  uintptr_t addr = 0;
  EXPECT_EQ(vdso_vmar.map(0, vmo, 0, scratch.vdso_total_size(),
                          ZX_VM_PERM_READ | ZX_VM_SPECIFIC_OVERWRITE, &addr),
            ZX_ERR_ACCESS_DENIED, "zx_vmar_map to overmap vDSO");
  EXPECT_EQ(addr, 0, "zx_vmar_map to overmap vDSO");

  // Also forbidden if done from a parent VMAR.
  zx_info_vmar_t root_vmar_info;
  ASSERT_EQ(scratch.root_vmar().get_info(ZX_INFO_VMAR, &root_vmar_info, sizeof(root_vmar_info),
                                         nullptr, nullptr),
            ZX_OK, "zx_object_get_info on root VMAR");
  EXPECT_EQ(scratch.root_vmar().map(scratch.vdso_base() - root_vmar_info.base, vmo, 0,
                                    scratch.vdso_total_size(),
                                    ZX_VM_PERM_READ | ZX_VM_SPECIFIC_OVERWRITE, &addr),
            ZX_ERR_ACCESS_DENIED, "zx_vmar_map to overmap vDSO from root");
  EXPECT_EQ(addr, 0, "zx_vmar_map to overmap vDSO from root");

  // Explicit unmapping covering the vDSO code region is forbidden.
  EXPECT_EQ(scratch.root_vmar().unmap(scratch.vdso_base(), scratch.vdso_total_size()),
            ZX_ERR_ACCESS_DENIED, "zx_vmar_unmap to unmap vDSO");

  // Implicit unmapping by destroying a containing VMAR is forbidden.
  EXPECT_EQ(vdso_vmar.destroy(), ZX_ERR_ACCESS_DENIED, "zx_vmar_destroy to unmap vDSO");
  EXPECT_EQ(scratch.root_vmar().destroy(), ZX_ERR_ACCESS_DENIED,
            "zx_vmar_destroy on root to unmap vDSO");

  END_TEST;
}

bool vdso_map_code_wrong_test() {
  BEGIN_TEST;

  ScratchPad scratch(__func__);

  ASSERT_EQ(scratch.compute_vdso_sizes(), ZX_OK, "cannot read vDSO program headers");

  // Try to map the first page, which is not the code, as executable.
  uintptr_t addr;
  EXPECT_EQ(scratch.root_vmar().map(0, vdso_vmo, 0, PAGE_SIZE, ZX_VM_PERM_READ | ZX_VM_PERM_EXECUTE,
                                    &addr),
            ZX_ERR_ACCESS_DENIED, "executable mapping of wrong part of vDSO");

  // Try to map only part of the code, not the whole code segment.
  ASSERT_GE(scratch.vdso_code_size(), PAGE_SIZE, "vDSO code < page??");
  if (scratch.vdso_code_size() > PAGE_SIZE) {
    ASSERT_EQ(scratch.vdso_code_size() % PAGE_SIZE, 0);
    EXPECT_EQ(scratch.root_vmar().map(0, vdso_vmo, scratch.vdso_code_offset(), PAGE_SIZE,
                                      ZX_VM_PERM_READ | ZX_VM_PERM_EXECUTE, &addr),
              ZX_ERR_ACCESS_DENIED, "executable mapping of subset of vDSO code");
  }

  END_TEST;
}

BEGIN_TEST_CASE(vdso_tests)
RUN_TEST(vdso_map_twice_test);
RUN_TEST(vdso_map_code_wrong_test);
RUN_TEST(vdso_map_change_test);
END_TEST_CASE(vdso_tests)
