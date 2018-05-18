// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/suggestion_engine_helper.h"

#include "lib/fxl/random/uuid.h"

namespace modular {

SuggestionPrototype* CreateSuggestionPrototype(
    SuggestionPrototypeMap* owner,
    const std::string& source_url,
    const std::string& story_id,
    Proposal proposal) {
  auto prototype_pair = owner->emplace(std::make_pair(source_url, proposal.id),
                                       std::make_unique<SuggestionPrototype>());
  auto suggestion_prototype = prototype_pair.first->second.get();
  suggestion_prototype->suggestion_id = fxl::GenerateUUID();
  suggestion_prototype->source_url = source_url;
  // TODO(miguelfrde): remove when cleaning usage of proposal.story_id.
  if (story_id.empty()) {
    suggestion_prototype->story_id = proposal.story_id;
  } else {
    suggestion_prototype->story_id = story_id;
  }
  suggestion_prototype->timestamp = fxl::TimePoint::Now();
  suggestion_prototype->proposal = std::move(proposal);

  return suggestion_prototype;
}

}  // namespace modular
