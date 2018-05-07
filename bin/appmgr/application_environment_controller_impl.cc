// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/application_environment_controller_impl.h"

#include <utility>

#include "garnet/bin/appmgr/realm.h"
#include "lib/fxl/functional/closure.h"

namespace component {

ApplicationEnvironmentControllerImpl::ApplicationEnvironmentControllerImpl(
    fidl::InterfaceRequest<ApplicationEnvironmentController> request,
    std::unique_ptr<Realm> realm)
    : binding_(this), realm_(std::move(realm)) {
  if (request.is_valid()) {
    binding_.Bind(std::move(request));
    binding_.set_error_handler([this] {
      realm_->parent()->ExtractChild(realm_.get());
      // The destructor of the temporary returned by ExtractChild destroys
      // |this| at the end of the previous statement.
    });
  }
}

ApplicationEnvironmentControllerImpl::~ApplicationEnvironmentControllerImpl() =
    default;

void ApplicationEnvironmentControllerImpl::Kill(KillCallback callback) {
  std::unique_ptr<ApplicationEnvironmentControllerImpl> self =
      realm_->parent()->ExtractChild(realm_.get());
  realm_ = nullptr;
  callback();
  // The |self| destructor destroys |this| when we unwind this stack frame.
}

void ApplicationEnvironmentControllerImpl::Detach() {
  binding_.set_error_handler(fxl::Closure());
}

}  // namespace component
