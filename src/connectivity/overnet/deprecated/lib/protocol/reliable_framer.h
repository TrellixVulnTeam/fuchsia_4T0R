// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "src/connectivity/overnet/deprecated/lib/protocol/stream_framer.h"

namespace overnet {

// Framer that transports packets on a reliable stream of bytes
class ReliableFramer final : public StreamFramer {
 public:
  ReliableFramer();
  ~ReliableFramer();

  void Push(Slice data) override;
  StatusOr<Optional<Slice>> Pop() override;
  bool InputEmpty() const override;
  Optional<Slice> SkipNoise() override;

  Slice Frame(Slice data) override;

 private:
  Slice buffered_input_;
};

}  // namespace overnet
