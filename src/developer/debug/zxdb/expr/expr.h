// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EXPR_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EXPR_H_

#include "lib/fit/function.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"

namespace zxdb {

class Err;
class EvalContext;

// Main entrypoint to evaluate an expression. This will parse the input, execute the result with the
// given context, and call the callback when complete.
//
// If follow_references is set, expressions that result in a reference will have the value of the
// referenced data computed. This is useful when the caller wants the result value of an expression
// but doesn't care about the exact type.
//
// The callback may get issued asynchronously in the future or it may get called synchronously in a
// reentrant fashion from this function.
void EvalExpression(const std::string& input, fxl::RefPtr<EvalContext> context,
                    bool follow_references,
                    fit::callback<void(const Err& err, ExprValue value)> cb);

// Like EvalExpressions but evaluates a sequence of expressions, issuing the callback when they're
// all complete. The size order of the results in the callback vector will correspond to the inputs.
void EvalExpressions(const std::vector<std::string>& inputs, fxl::RefPtr<EvalContext> context,
                     bool follow_references,
                     fit::callback<void(std::vector<std::pair<Err, ExprValue>>)> cb);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EXPR_H_
