// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/resolve_ptr_ref.h"

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/common/test_with_loop.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/expr/mock_eval_context.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"

namespace zxdb {

namespace {

class ResolvePtrRefTest : public TestWithLoop {};

}  // namespace

TEST_F(ResolvePtrRefTest, NotPointer) {
  auto eval_context = fxl::MakeRefCounted<MockEvalContext>();

  auto int32_type = MakeInt32Type();
  ExprValue int32_value(int32_type, {0x00, 0x00, 0x00, 0x00});

  bool called = false;
  Err out_err;
  ExprValue out_value;
  ResolvePointer(eval_context, int32_value,
                 [&called, &out_err, &out_value](const Err& err, ExprValue value) {
                   called = true;
                   out_err = err;
                   out_value = value;
                 });

  // This should fail synchronously.
  EXPECT_TRUE(called);
  EXPECT_EQ("Attempting to dereference 'int32_t' which is not a pointer.", out_err.msg());

  // Pointer with incorrectly sized data.
  auto int32_ptr_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, int32_type);
  ExprValue int32_ptr_value(int32_ptr_type, {0x00, 0x00, 0x00, 0x00});

  called = false;
  ResolvePointer(eval_context, int32_ptr_value,
                 [&called, &out_err, &out_value](const Err& err, ExprValue value) {
                   called = true;
                   out_err = err;
                   out_value = value;
                 });

  // This should fail synchronously.
  EXPECT_TRUE(called);
  EXPECT_EQ(
      "The value of type 'int32_t*' is the incorrect size (expecting 8, got "
      "4). Please file a bug.",
      out_err.msg());
}

TEST_F(ResolvePtrRefTest, InvalidMemory) {
  auto eval_context = fxl::MakeRefCounted<MockEvalContext>();
  constexpr uint64_t kAddress = 0x10;

  auto int32_type = MakeInt32Type();

  // This read will return no data.
  bool called = false;
  Err out_err;
  ExprValue out_value;
  ResolvePointer(eval_context, kAddress, int32_type,
                 [&called, &out_err, &out_value](const Err& err, ExprValue value) {
                   called = true;
                   out_err = err;
                   out_value = value;
                   debug_ipc::MessageLoop::Current()->QuitNow();
                 });

  EXPECT_FALSE(called);
  loop().Run();
  EXPECT_TRUE(called);
  EXPECT_EQ("Invalid pointer 0x10", out_err.msg());

  // This read will return only 2 bytes (it requires 4).
  eval_context->data_provider()->AddMemory(kAddress, {0x00, 0x00});
  called = false;
  out_err = Err();
  ResolvePointer(eval_context, kAddress, int32_type,
                 [&called, &out_err, &out_value](const Err& err, ExprValue value) {
                   called = true;
                   out_err = err;
                   out_value = value;
                   debug_ipc::MessageLoop::Current()->QuitNow();
                 });

  EXPECT_FALSE(called);
  loop().Run();
  EXPECT_TRUE(called);
  EXPECT_EQ("Invalid pointer 0x10", out_err.msg());
}

// Tests EnsureResolveReference when the value is not a reference.
TEST_F(ResolvePtrRefTest, NotRef) {
  auto eval_context = fxl::MakeRefCounted<MockEvalContext>();
  auto int32_type = MakeInt32Type();

  ExprValue value(123);

  bool called = false;
  ExprValue out_value;
  EnsureResolveReference(eval_context, value,
                         [&called, &out_value](const Err& err, ExprValue result) {
                           EXPECT_FALSE(err.has_error());
                           called = true;
                           out_value = result;
                         });

  // Should have run synchronously.
  EXPECT_TRUE(called);
  EXPECT_EQ(value, out_value);
}

// Tests EnsureResolveReference when the value is a const ref. The const should get ignored, the ref
// should be stripped, and the pointed-to value should be the result.
TEST_F(ResolvePtrRefTest, ConstRef) {
  auto eval_context = fxl::MakeRefCounted<MockEvalContext>();

  // Add a 32-bit int at a given address.
  constexpr uint64_t kAddress = 0x300020;
  std::vector<uint8_t> int_value = {0x04, 0x03, 0x02, 0x01};
  eval_context->data_provider()->AddMemory(kAddress, int_value);

  // Make "volatile const int32_t&". This tests modifies on both sides of the reference (volatile on
  // the outside, const on the inside).
  auto int32_type = MakeInt32Type();
  auto const_int32_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kConstType, int32_type);
  auto const_int32_ref_type =
      fxl::MakeRefCounted<ModifiedType>(DwarfTag::kReferenceType, const_int32_type);
  auto volatile_const_int32_ref_type =
      fxl::MakeRefCounted<ModifiedType>(DwarfTag::kVolatileType, const_int32_ref_type);

  ExprValue value(volatile_const_int32_ref_type, {0x20, 0x00, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00});

  bool called = false;
  ExprValue out_value;
  EnsureResolveReference(eval_context, value,
                         [&called, &out_value](const Err& err, ExprValue result) {
                           EXPECT_FALSE(err.has_error());
                           called = true;
                           out_value = result;
                           debug_ipc::MessageLoop::Current()->QuitNow();
                         });

  // Should have run asynchronously.
  EXPECT_FALSE(called);
  loop().Run();
  EXPECT_TRUE(called);

  // The resolved type should be "const int32_t" and have the input value.
  EXPECT_EQ(const_int32_type.get(), out_value.type());
  EXPECT_EQ(int_value, out_value.data());

  // Check the location of the result.
  EXPECT_EQ(ExprValueSource::Type::kMemory, out_value.source().type());
  EXPECT_EQ(kAddress, out_value.source().address());
}

TEST_F(ResolvePtrRefTest, GetPointedToType_Null) {
  auto eval_context = fxl::MakeRefCounted<MockEvalContext>();

  fxl::RefPtr<Type> pointed_to;
  Err err = GetPointedToType(eval_context, nullptr, &pointed_to);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("No type information.", err.msg());
}

TEST_F(ResolvePtrRefTest, GetPointedToType_NotPointer) {
  auto eval_context = fxl::MakeRefCounted<MockEvalContext>();

  auto int32_type = fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 4, "int32_t");

  fxl::RefPtr<Type> pointed_to;
  Err err = GetPointedToType(eval_context, int32_type.get(), &pointed_to);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("Attempting to dereference 'int32_t' which is not a pointer.", err.msg());
}

TEST_F(ResolvePtrRefTest, GetPointedToType_NoPointedToType) {
  auto eval_context = fxl::MakeRefCounted<MockEvalContext>();

  // Pointer to nothing.
  auto ptr_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, LazySymbol());

  fxl::RefPtr<Type> pointed_to;
  Err err = GetPointedToType(eval_context, ptr_type.get(), &pointed_to);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("Missing pointer type info, please file a bug with a repro.", err.msg());
}

TEST_F(ResolvePtrRefTest, GetPointedToType_Good) {
  auto eval_context = fxl::MakeRefCounted<MockEvalContext>();

  auto int32_type = fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 4, "int32_t");
  auto ptr_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, int32_type);

  fxl::RefPtr<Type> pointed_to;
  Err err = GetPointedToType(eval_context, ptr_type.get(), &pointed_to);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(int32_type.get(), pointed_to.get());
}

}  // namespace zxdb
