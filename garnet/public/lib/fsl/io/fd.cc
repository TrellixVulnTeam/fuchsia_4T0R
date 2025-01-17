// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fsl/io/fd.h"

#include <lib/fdio/limits.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/directory.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

namespace fsl {

zx::channel CloneChannelFromFileDescriptor(int fd) {
  zx::handle handle;
  zx_status_t status = fdio_fd_clone(fd, handle.reset_and_get_address());
  if (status != ZX_OK)
    return zx::channel();

  zx_info_handle_basic_t info = {};
  status = handle.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), NULL, NULL);

  if (status != ZX_OK || info.type != ZX_OBJ_TYPE_CHANNEL)
    return zx::channel();

  return zx::channel(handle.release());
}

fxl::UniqueFD OpenChannelAsFileDescriptor(zx::channel channel) {
  int fd = -1;
  zx_status_t status = fdio_fd_create(channel.release(), &fd);
  if (status != ZX_OK) {
    return fxl::UniqueFD();
  }
  return fxl::UniqueFD(fd);
}

}  // namespace fsl
