// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "unbind-task.h"

#include "../shared/log.h"
#include "coordinator.h"

namespace devmgr {

UnbindTask::UnbindTask(fbl::RefPtr<Device> device, UnbindTaskOpts opts, Completion completion)
    : Task(device->coordinator->dispatcher(), std::move(completion), opts.post_on_create),
      device_(std::move(device)),
      do_unbind_(opts.do_unbind),
      devhost_requested_(opts.devhost_requested) {}

UnbindTask::~UnbindTask() = default;

fbl::RefPtr<UnbindTask> UnbindTask::Create(fbl::RefPtr<Device> device, UnbindTaskOpts opts,
                                           Completion completion) {
  return fbl::MakeRefCounted<UnbindTask>(std::move(device), opts, std::move(completion));
}

// Schedules the unbind tasks for the device's children.
void UnbindTask::ScheduleUnbindChildren() {
  fbl::RefPtr<UnbindTask> proxy_task = nullptr;
  if (device_->proxy() != nullptr) {
    switch (device_->proxy()->state()) {
      case Device::State::kDead:
      // We are already in the process of unbinding ourselves and our children,
      // no need to create a new one.
      case Device::State::kUnbinding:
        break;
      case Device::State::kSuspended:
      // The created unbind task will wait for the suspend to complete.
      case Device::State::kSuspending:
      case Device::State::kActive: {
        proxy_task = device_->proxy()->RequestUnbindTask(UnbindTaskOpts{
            .do_unbind = false, .post_on_create = false, .devhost_requested = false});
        proxy_task->AddDependency(fbl::WrapRefPtr(this));
      }
    }
    // A device may have both a proxy device and children devices,
    // so continue rather than returning early.
  }

  auto children = device_->children();
  // Though we try to schedule the unbind tasks for both a device's proxy and its children,
  // its possible for ScheduleRemove() to be called directly on a proxy unbind task, such as in the
  // case of a forced remove.
  // To handle this, we need to schedule unbind tasks for the proxy "children", which are actually
  // stored in our parent's children list.
  // This means we may end up adding the children as dependent on a proxy device twice,
  // but that is handled by the task logic.
  if (device_->flags & DEV_CTX_PROXY && device_->parent()) {
    children = device_->parent()->children();
  }

  for (auto& child : children) {
    // Use a switch statement here so that this gets reconsidered if we add
    // more states.
    switch (child.state()) {
      case Device::State::kDead:
      case Device::State::kUnbinding:
        continue;
      case Device::State::kSuspended:
      case Device::State::kSuspending:
      case Device::State::kActive:
        break;
    }
    auto child_task = child.RequestUnbindTask(
        UnbindTaskOpts{.do_unbind = true, .post_on_create = false, .devhost_requested = false});
    if (proxy_task) {
      child_task->AddDependency(proxy_task);
    } else {
      child_task->AddDependency(fbl::WrapRefPtr(this));
    }
  }
}

void UnbindTask::Run() {
  // The device is currently suspending, wait for it to complete.
  if (device_->state() == Device::State::kSuspending) {
    auto suspend_task = device_->GetActiveSuspend();
    ZX_ASSERT(suspend_task != nullptr);
    AddDependency(suspend_task);
    return;
  }

  // We need to schedule the child tasks before completing the unbind task runs,
  // as composite device disassociation may occur.
  ScheduleUnbindChildren();

  auto completion = [this](zx_status_t status) {
    // If this unbind task failed, force remove all devices from the devhost.
    bool failed_unbind = status != ZX_OK && status != ZX_ERR_UNAVAILABLE;
    if (failed_unbind && device_->state() != Device::State::kDead) {
      device_->coordinator->RemoveDevice(device_, true /* forced */);
    }
    // The forced removal will schedule new unbind tasks if needed (e.g. for proxy tasks),
    // so we should not propagate errors other than ZX_ERR_UNAVAILABLE.
    Complete(status == ZX_OK ? ZX_OK : ZX_ERR_UNAVAILABLE);
  };

  // Check if this device is not in a devhost.  This happens for the
  // top-level devices like /sys provided by devcoordinator, or if the device
  // has already been removed.
  if (device_->host() == nullptr) {
    return completion(ZX_OK);
  }

  zx_status_t status = ZX_OK;
  if (do_unbind_) {
    status = device_->SendUnbind(std::move(completion));
  } else {
    status = device_->SendCompleteRemoval(std::move(completion));
  }
  if (status != ZX_OK) {
    return completion(status);
  }
}

}  // namespace devmgr
