// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "launcher.h"

#include <fbl/string.h>
#include <fbl/vector.h>
#include <fuchsia/process/c/fidl.h>
#include <lib/fidl/cpp/message_buffer.h>
#include <lib/zx/channel.h>
#include <lib/zx/job.h>
#include <stdint.h>
#include <zircon/processargs.h>
#include <zircon/status.h>

#include <utility>

namespace launcher {
namespace {

fbl::String GetString(fidl_string_t string) { return fbl::String(string.data, string.size); }

fbl::String GetString(fidl_vector_t bytes) {
  return fbl::String(static_cast<char*>(bytes.data), bytes.count);
}

void PushStrings(fidl_vector_t* input, fbl::Vector<fbl::String>* target) {
  fidl_vector_t* data = static_cast<fidl_vector_t*>(input->data);
  for (size_t i = 0; i < input->count; ++i) {
    target->push_back(GetString(data[i]));
  }
}

void PushCStrs(const fbl::Vector<fbl::String>& source, fbl::Vector<const char*>* target) {
  target->reserve(target->size() + source.size());
  for (const auto& str : source) {
    target->push_back(str.c_str());
  }
}

}  // namespace

LauncherImpl::LauncherImpl(zx::channel channel)
    : channel_(std::move(channel)),
      wait_(this, channel_.get(), ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED) {}

LauncherImpl::~LauncherImpl() = default;

zx_status_t LauncherImpl::Begin(async_dispatcher_t* dispatcher) { return wait_.Begin(dispatcher); }

void LauncherImpl::OnHandleReady(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                                 zx_status_t status, const zx_packet_signal_t* signal) {
  if (status != ZX_OK) {
    NotifyError(status);
    return;
  }

  if (signal->observed & ZX_CHANNEL_READABLE) {
    fidl::MessageBuffer buffer;
    for (uint64_t i = 0; i < signal->count; i++) {
      status = ReadAndDispatchMessage(&buffer);
      if (status == ZX_ERR_SHOULD_WAIT)
        break;
      if (status != ZX_OK) {
        NotifyError(status);
        return;
      }
    }
    status = wait_.Begin(dispatcher);
    if (status != ZX_OK) {
      NotifyError(status);
    }
    return;
  }

  ZX_DEBUG_ASSERT(signal->observed & ZX_CHANNEL_PEER_CLOSED);
  // Notice that we don't notify an error until we've drained all the messages
  // out of the channel.
  NotifyError(ZX_ERR_PEER_CLOSED);
}

zx_status_t LauncherImpl::ReadAndDispatchMessage(fidl::MessageBuffer* buffer) {
  fidl::Message message = buffer->CreateEmptyMessage();
  zx_status_t status = message.Read(channel_.get(), 0);
  if (status != ZX_OK)
    return status;
  if (!message.has_header())
    return ZX_ERR_INVALID_ARGS;
  // This is an if statement because, depending on the state of the ordinal
  // migration, GenOrdinal and Ordinal may be the same value.  See FIDL-524.
  uint64_t ordinal = message.ordinal();
  if (ordinal == fuchsia_process_LauncherLaunchOrdinal ||
      ordinal == fuchsia_process_LauncherLaunchGenOrdinal) {
    return Launch(buffer, std::move(message));
  } else if (ordinal == fuchsia_process_LauncherCreateWithoutStartingOrdinal ||
             ordinal == fuchsia_process_LauncherCreateWithoutStartingGenOrdinal) {
    return CreateWithoutStarting(buffer, std::move(message));
  } else if (ordinal == fuchsia_process_LauncherAddArgsOrdinal ||
             ordinal == fuchsia_process_LauncherAddArgsGenOrdinal) {
    return AddArgs(std::move(message));
  } else if (ordinal == fuchsia_process_LauncherAddEnvironsOrdinal ||
             ordinal == fuchsia_process_LauncherAddEnvironsGenOrdinal) {
    return AddEnvirons(std::move(message));
  } else if (ordinal == fuchsia_process_LauncherAddNamesOrdinal ||
             ordinal == fuchsia_process_LauncherAddNamesGenOrdinal) {
    return AddNames(std::move(message));
  } else if (ordinal == fuchsia_process_LauncherAddHandlesOrdinal ||
             ordinal == fuchsia_process_LauncherAddHandlesGenOrdinal) {
    return AddHandles(std::move(message));
  } else {
    fprintf(stderr, "launcher: error: Unknown message ordinal: %lu\n", ordinal);
    return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t LauncherImpl::Launch(fidl::MessageBuffer* buffer, fidl::Message message) {
  const char* error_msg = nullptr;
  zx_status_t status = message.Decode(&fuchsia_process_LauncherLaunchRequestTable, &error_msg);
  if (status != ZX_OK) {
    fprintf(stderr, "launcher: error: Launch: %s\n", error_msg);
    return status;
  }

  zx_txid_t txid = message.txid();
  uint64_t ordinal = message.ordinal();

  launchpad_t* lp = nullptr;
  PrepareLaunchpad(message, &lp);

  fidl::Builder builder = buffer->CreateBuilder();
  auto* response = builder.New<fuchsia_process_LauncherLaunchResponse>();
  response->hdr.txid = txid;
  response->hdr.ordinal = ordinal;
  response->status = launchpad_go(lp, &response->process, &error_msg);

  if (response->status != ZX_OK && error_msg) {
    fprintf(stderr, "launcher: error: Launch: %s\n", error_msg);
  }

  message.set_bytes(builder.Finalize());
  Reset();

  status = message.Encode(&fuchsia_process_LauncherLaunchResponseTable, &error_msg);
  if (status != ZX_OK) {
    fprintf(stderr, "launcher: error: Launch: %s\n", error_msg);
    return status;
  }
  return message.Write(channel_.get(), 0);
}

zx_status_t LauncherImpl::CreateWithoutStarting(fidl::MessageBuffer* buffer,
                                                fidl::Message message) {
  const char* error_msg = nullptr;
  zx_status_t status =
      message.Decode(&fuchsia_process_LauncherCreateWithoutStartingRequestTable, &error_msg);
  if (status != ZX_OK) {
    fprintf(stderr, "launcher: error: CreateWithoutStarting: %s\n", error_msg);
    return status;
  }

  zx_txid_t txid = message.txid();
  uint64_t ordinal = message.ordinal();

  launchpad_t* lp = nullptr;
  PrepareLaunchpad(message, &lp);

  fidl::Builder builder = buffer->CreateBuilder();
  auto* response = builder.New<fuchsia_process_LauncherCreateWithoutStartingResponse>();
  response->hdr.txid = txid;
  response->hdr.ordinal = ordinal;

  launchpad_start_data_t data = {};
  response->status = launchpad_ready_set(lp, &data, &error_msg);

  if (response->status == ZX_OK) {
    response->data = builder.New<fuchsia_process_ProcessStartData>();
    response->data->process = data.process;
    response->data->root_vmar = data.root_vmar;
    response->data->thread = data.thread;
    response->data->entry = data.entry;
    response->data->stack = data.stack;
    response->data->bootstrap = data.bootstrap;
    response->data->vdso_base = data.vdso_base;
    response->data->base = data.base;
  } else if (error_msg) {
    fprintf(stderr, "launcher: error: CreateWithoutStarting: %s\n", error_msg);
  }

  message.set_bytes(builder.Finalize());
  Reset();

  status = message.Encode(&fuchsia_process_LauncherCreateWithoutStartingResponseTable, &error_msg);
  if (status != ZX_OK) {
    fprintf(stderr, "launcher: error: CreateWithoutStarting: %s\n", error_msg);
    return status;
  }
  return message.Write(channel_.get(), 0);
}

zx_status_t LauncherImpl::AddArgs(fidl::Message message) {
  const char* error_msg = nullptr;
  zx_status_t status = message.Decode(&fuchsia_process_LauncherAddArgsRequestTable, &error_msg);
  if (status != ZX_OK) {
    fprintf(stderr, "launcher: error: AddArgs: %s\n", error_msg);
    return status;
  }
  PushStrings(message.GetPayloadAs<fidl_vector_t>(), &args_);
  return ZX_OK;
}

zx_status_t LauncherImpl::AddEnvirons(fidl::Message message) {
  const char* error_msg = nullptr;
  zx_status_t status = message.Decode(&fuchsia_process_LauncherAddEnvironsRequestTable, &error_msg);
  if (status != ZX_OK) {
    fprintf(stderr, "launcher: error: AddEnvirons: %s\n", error_msg);
    return status;
  }
  PushStrings(message.GetPayloadAs<fidl_vector_t>(), &environs_);
  return ZX_OK;
}

zx_status_t LauncherImpl::AddNames(fidl::Message message) {
  const char* error_msg = nullptr;
  zx_status_t status = message.Decode(&fuchsia_process_LauncherAddNamesRequestTable, &error_msg);
  if (status != ZX_OK) {
    fprintf(stderr, "launcher: error: AddNames: %s\n", error_msg);
    return status;
  }
  fidl_vector_t* payload = message.GetPayloadAs<fidl_vector_t>();
  fuchsia_process_NameInfo* names = static_cast<fuchsia_process_NameInfo*>(payload->data);
  for (size_t i = 0; i < payload->count; ++i) {
    ids_.push_back(PA_HND(PA_NS_DIR, static_cast<uint32_t>(nametable_.size())));
    handles_.push_back(zx::handle(names[i].directory));
    nametable_.push_back(GetString(names[i].path));
  }
  return ZX_OK;
}

zx_status_t LauncherImpl::AddHandles(fidl::Message message) {
  const char* error_msg = nullptr;
  zx_status_t status = message.Decode(&fuchsia_process_LauncherAddHandlesRequestTable, &error_msg);
  if (status != ZX_OK) {
    fprintf(stderr, "launcher: error: AddHandles: %s\n", error_msg);
    return status;
  }
  fidl_vector_t* payload = message.GetPayloadAs<fidl_vector_t>();
  fuchsia_process_HandleInfo* handles = static_cast<fuchsia_process_HandleInfo*>(payload->data);
  for (size_t i = 0; i < payload->count; ++i) {
    if (handles[i].id == PA_LDSVC_LOADER) {
      // We need to feed PA_LDSVC_LOADER to launchpad through a different API.
      ldsvc_.reset(handles[i].handle);
    } else {
      ids_.push_back(handles[i].id);
      handles_.push_back(zx::handle(handles[i].handle));
    }
  }
  return ZX_OK;
}

void LauncherImpl::PrepareLaunchpad(const fidl::Message& message, launchpad_t** lp_out) {
  fuchsia_process_LaunchInfo* info = message.GetPayloadAs<fuchsia_process_LaunchInfo>();

  // Grab an owning reference to the job because launchpad does not take
  // ownership of the job. We need to close the handle ourselves.
  zx::job job(info->job);
  fbl::String name = GetString(info->name);

  fbl::Vector<const char*> args, environs, nametable;
  PushCStrs(args_, &args);
  PushCStrs(environs_, &environs);
  PushCStrs(nametable_, &nametable);
  environs.push_back(nullptr);

  launchpad_t* lp = nullptr;
  launchpad_create_with_jobs(job.get(), ZX_HANDLE_INVALID, name.c_str(), &lp);

  if (!ldsvc_) {
    launchpad_abort(lp, ZX_ERR_INVALID_ARGS, "need ldsvc to load PT_INTERP");
  }

  // There's a subtle issue at this point. The problem is that launchpad will
  // make a synchronous call into the loader service to read the PT_INTERP,
  // but this handle was provided by our client, which means our client can
  // hang the launcher.
  zx::channel old_ldsvc(launchpad_use_loader_service(lp, ldsvc_.release()));

  launchpad_load_from_vmo(lp, info->executable);
  launchpad_set_args(lp, static_cast<int>(args.size()), args.get());
  launchpad_set_environ(lp, environs.get());
  launchpad_set_nametable(lp, nametable.size(), nametable.get());
  launchpad_add_handles(lp, ids_.size(), reinterpret_cast<zx_handle_t*>(handles_.get()),
                        ids_.get());
  // launchpad_add_handles() took ownership of the handles in handles_.
  for (auto& handle : handles_) {
    __UNUSED zx_handle_t old_handle = handle.release();
  }

  *lp_out = lp;
}

void LauncherImpl::NotifyError(zx_status_t error) {
  Reset();
  channel_.reset();
  if (error_handler_)
    error_handler_(error);
  // We might be deleted now.
}

void LauncherImpl::Reset() {
  args_.reset();
  environs_.reset();
  nametable_.reset();
  ids_.reset();
  handles_.reset();
  ldsvc_.reset();
}

}  // namespace launcher
