// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_ENCRYPTION_IMPL_ENCRYPTION_SERVICE_IMPL_H_
#define PERIDOT_BIN_LEDGER_ENCRYPTION_IMPL_ENCRYPTION_SERVICE_IMPL_H_

#include <functional>
#include <string>

#include "lib/fxl/tasks/task_runner.h"
#include "peridot/bin/ledger/encryption/public/encryption_service.h"
#include "peridot/lib/callback/scoped_task_runner.h"
#include "peridot/lib/convert/convert.h"

namespace encryption {

class EncryptionServiceImpl : public EncryptionService {
 public:
  EncryptionServiceImpl(fxl::RefPtr<fxl::TaskRunner> task_runner,
                        std::string namespace_id);
  ~EncryptionServiceImpl() override;

  // EncryptionService:
  storage::ObjectIdentifier MakeObjectIdentifier(
      storage::ObjectDigest digest) override;
  void EncryptCommit(
      convert::ExtendedStringView commit_storage,
      std::function<void(Status, std::string)> callback) override;
  void DecryptCommit(
      convert::ExtendedStringView storage_bytes,
      std::function<void(Status, std::string)> callback) override;
  void GetObjectName(
      storage::ObjectIdentifier object_identifier,
      std::function<void(Status, std::string)> callback) override;
  void EncryptObject(
      storage::ObjectIdentifier object_identifier,
      fsl::SizedVmo content,
      std::function<void(Status, std::string)> callback) override;
  void DecryptObject(
      storage::ObjectIdentifier object_identifier,
      std::string encrypted_data,
      std::function<void(Status, std::string)> callback) override;

 private:
  uint32_t GetCurrentKeyIndex();

  callback::ScopedTaskRunner task_runner_;
  const std::string namespace_id_;
};

}  // namespace encryption

#endif  // PERIDOT_BIN_LEDGER_ENCRYPTION_IMPL_ENCRYPTION_SERVICE_IMPL_H_
