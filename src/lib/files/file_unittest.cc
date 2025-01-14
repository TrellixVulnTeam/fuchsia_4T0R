// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/files/file.h"

#include <fcntl.h>

#include "gtest/gtest.h"
#include "src/lib/files/path.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/files/unique_fd.h"

namespace files {
namespace {

TEST(File, WriteFileInTwoPhases) {
  ScopedTempDir dir;
  std::string path = dir.path() + "/destination";

  std::string content = "Hello World";
  ASSERT_TRUE(WriteFileInTwoPhases(path, content, dir.path()));
  std::string read_content;
  ASSERT_TRUE(ReadFileToString(path, &read_content));
  EXPECT_EQ(read_content, content);
}

TEST(File, ReadWriteFileAt) {
  ScopedTempDir dir;
  std::string filename = "bar";
  std::string content = "content";
  fxl::UniqueFD dirfd(open(dir.path().c_str(), O_RDONLY));

  EXPECT_TRUE(files::WriteFileAt(dirfd.get(), filename, content.c_str(), content.size()));

  std::string read_content;
  EXPECT_TRUE(files::ReadFileToStringAt(dirfd.get(), filename, &read_content));
  EXPECT_EQ(content, read_content);
}

TEST(File, IsFileAt) {
  ScopedTempDir dir;
  std::string path;

  ASSERT_TRUE(dir.NewTempFile(&path));

  fxl::UniqueFD dirfd(open(dir.path().c_str(), O_RDONLY));
  ASSERT_TRUE(dirfd.get() != -1);
  EXPECT_TRUE(IsFileAt(dirfd.get(), GetBaseName(path)));
}

TEST(File, GetFileSize) {
  ScopedTempDir dir;
  std::string path;

  ASSERT_TRUE(dir.NewTempFile(&path));

  uint64_t size;
  EXPECT_TRUE(GetFileSize(path, &size));
  EXPECT_EQ(0u, size);

  std::string content = "Hello World";
  ASSERT_TRUE(WriteFile(path, content.data(), content.size()));
  EXPECT_TRUE(GetFileSize(path, &size));
  EXPECT_EQ(content.size(), size);
}

TEST(File, GetFileSizeAt) {
  ScopedTempDir dir;
  fxl::UniqueFD dirfd(open(dir.path().c_str(), O_RDONLY));
  ASSERT_TRUE(dirfd.get() != -1);

  std::string full_path;
  ASSERT_TRUE(dir.NewTempFile(&full_path));
  std::string path = GetBaseName(full_path);

  uint64_t size;
  EXPECT_TRUE(GetFileSizeAt(dirfd.get(), path, &size));
  EXPECT_EQ(0u, size);

  std::string content = "Hello World";
  ASSERT_TRUE(WriteFileAt(dirfd.get(), path, content.data(), content.size()));
  EXPECT_TRUE(GetFileSizeAt(dirfd.get(), path, &size));
  EXPECT_EQ(content.size(), size);
}

}  // namespace
}  // namespace files
