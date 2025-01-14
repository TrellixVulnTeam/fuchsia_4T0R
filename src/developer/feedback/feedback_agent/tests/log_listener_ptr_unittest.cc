// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/log_listener_ptr.h"

#include <fuchsia/mem/cpp/fidl.h>
#include <lib/async_promise/executor.h>
#include <lib/fit/single_threaded_executor.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/service_directory_provider.h>
#include <lib/syslog/cpp/logger.h>
#include <lib/syslog/logger.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>

#include <memory>
#include <ostream>
#include <vector>

#include "src/developer/feedback/feedback_agent/tests/stub_logger.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/test/test_settings.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace fuchsia {
namespace feedback {
namespace {

template <typename ResultListenerT>
bool DoStringBufferMatch(const fuchsia::mem::Buffer& actual, const std::string& expected,
                         ResultListenerT* result_listener) {
  std::string actual_value;
  if (!fsl::StringFromVmo(actual, &actual_value)) {
    *result_listener << "Cannot parse actual VMO to string";
    return false;
  }

  if (actual_value.compare(expected) != 0) {
    return false;
  }

  return true;
}

// Returns true if gMock str(|arg|) matches |expected|.
MATCHER_P(MatchesStringBuffer, expected, "'" + std::string(expected) + "'") {
  return DoStringBufferMatch(arg, expected, result_listener);
}

class CollectSystemLogTest : public gtest::TestLoopFixture {
 public:
  CollectSystemLogTest() : executor_(dispatcher()), service_directory_provider_(dispatcher()) {}

 protected:
  void ResetStubLogger(std::unique_ptr<StubLogger> stub_logger) {
    stub_logger_ = std::move(stub_logger);
    if (stub_logger_) {
      FXL_CHECK(service_directory_provider_.AddService(stub_logger_->GetHandler()) == ZX_OK);
    }
  }

  fit::result<fuchsia::mem::Buffer> CollectSystemLog(const zx::duration timeout = zx::sec(1)) {
    fit::result<fuchsia::mem::Buffer> result;
    executor_.schedule_task(
        fuchsia::feedback::CollectSystemLog(
            dispatcher(), service_directory_provider_.service_directory(), timeout)
            .then([&result](fit::result<fuchsia::mem::Buffer>& res) { result = std::move(res); }));
    RunLoopFor(timeout);
    return result;
  }

 private:
  async::Executor executor_;
  ::sys::testing::ServiceDirectoryProvider service_directory_provider_;

  std::unique_ptr<StubLogger> stub_logger_;
};

TEST_F(CollectSystemLogTest, Succeed_BasicCase) {
  std::unique_ptr<StubLogger> stub_logger = std::make_unique<StubLogger>();
  stub_logger->set_messages({
      BuildLogMessage(FX_LOG_INFO, "line 1"),
      BuildLogMessage(FX_LOG_WARNING, "line 2", zx::msec(1)),
      BuildLogMessage(FX_LOG_ERROR, "line 3", zx::msec(2)),
      BuildLogMessage(FX_LOG_FATAL, "line 4", zx::msec(3)),
      BuildLogMessage(-1 /*VLOG(1)*/, "line 5", zx::msec(4)),
      BuildLogMessage(-2 /*VLOG(2)*/, "line 6", zx::msec(5)),
      BuildLogMessage(FX_LOG_INFO, "line 7", zx::msec(6), /*tags=*/{"foo"}),
      BuildLogMessage(FX_LOG_INFO, "line 8", zx::msec(7), /*tags=*/{"bar"}),
      BuildLogMessage(FX_LOG_INFO, "line 9", zx::msec(8),
                      /*tags=*/{"foo", "bar"}),
  });
  ResetStubLogger(std::move(stub_logger));

  fit::result<fuchsia::mem::Buffer> result = CollectSystemLog();

  ASSERT_TRUE(result.is_ok());
  fuchsia::mem::Buffer logs = result.take_value();
  EXPECT_THAT(logs, MatchesStringBuffer(
                        R"([15604.000][07559][07687][] INFO: line 1
[15604.001][07559][07687][] WARN: line 2
[15604.002][07559][07687][] ERROR: line 3
[15604.003][07559][07687][] FATAL: line 4
[15604.004][07559][07687][] VLOG(1): line 5
[15604.005][07559][07687][] VLOG(2): line 6
[15604.006][07559][07687][foo] INFO: line 7
[15604.007][07559][07687][bar] INFO: line 8
[15604.008][07559][07687][foo, bar] INFO: line 9
)"));
}

TEST_F(CollectSystemLogTest, Succeed_LoggerUnbindsFromLogListenerAfterOneMessage) {
  std::unique_ptr<StubLogger> stub_logger =
      std::make_unique<StubLoggerUnbindsFromLogListenerAfterOneMessage>();
  stub_logger->set_messages({
      BuildLogMessage(FX_LOG_INFO, "this line should appear in the partial logs"),
      BuildLogMessage(FX_LOG_INFO, "this line should be missing from the partial logs"),
  });
  ResetStubLogger(std::move(stub_logger));

  fit::result<fuchsia::mem::Buffer> result = CollectSystemLog();

  ASSERT_TRUE(result.is_ok());
  fuchsia::mem::Buffer logs = result.take_value();
  EXPECT_THAT(logs, MatchesStringBuffer("[15604.000][07559][07687][] INFO: this line "
                                        "should appear in the partial logs\n"));
}

TEST_F(CollectSystemLogTest, Succeed_LogCollectionTimesOut) {
  // The logger will delay sending the rest of the messages after the first message.
  // The delay needs to be longer than the log collection timeout to get partial logs.
  // Since we are using a test loop with a fake clock, the actual durations don't matter so we can
  // set them arbitrary long.
  const zx::duration logger_delay = zx::sec(10);
  const zx::duration log_collection_timeout = zx::sec(1);

  std::unique_ptr<StubLogger> stub_logger =
      std::make_unique<StubLoggerDelaysAfterOneMessage>(dispatcher(), logger_delay);
  stub_logger->set_messages({
      BuildLogMessage(FX_LOG_INFO, "this line should appear in the partial logs"),
      BuildLogMessage(FX_LOG_INFO, "this line should be missing from the partial logs"),
  });
  ResetStubLogger(std::move(stub_logger));

  fit::result<fuchsia::mem::Buffer> result = CollectSystemLog(log_collection_timeout);

  // First, we check that the log collection terminated with partial logs after the timeout.
  ASSERT_TRUE(result.is_ok());
  fuchsia::mem::Buffer logs = result.take_value();
  EXPECT_THAT(
      logs, MatchesStringBuffer(
                "[15604.000][07559][07687][] INFO: this line should appear in the partial logs\n"));

  // Then, we check that nothing crashes when the server tries to send the rest of the messages
  // after the connection has been lost.
  ASSERT_TRUE(RunLoopFor(logger_delay));
}

TEST_F(CollectSystemLogTest, Fail_EmptyLog) {
  ResetStubLogger(std::make_unique<StubLogger>());

  fit::result<fuchsia::mem::Buffer> result = CollectSystemLog();

  ASSERT_TRUE(result.is_error());
}

TEST_F(CollectSystemLogTest, Fail_LoggerNotAvailable) {
  ResetStubLogger(nullptr);

  fit::result<fuchsia::mem::Buffer> result = CollectSystemLog();

  ASSERT_TRUE(result.is_error());
}

TEST_F(CollectSystemLogTest, Fail_LoggerClosesConnection) {
  ResetStubLogger(std::make_unique<StubLoggerClosesConnection>());

  fit::result<fuchsia::mem::Buffer> result = CollectSystemLog();

  ASSERT_TRUE(result.is_error());
}

TEST_F(CollectSystemLogTest, Fail_LoggerNeverBindsToLogListener) {
  ResetStubLogger(std::make_unique<StubLoggerNeverBindsToLogListener>());

  fit::result<fuchsia::mem::Buffer> result = CollectSystemLog();

  ASSERT_TRUE(result.is_error());
}

TEST_F(CollectSystemLogTest, Fail_LoggerNeverCallsLogManyBeforeDone) {
  ResetStubLogger(std::make_unique<StubLoggerNeverCallsLogManyBeforeDone>());

  fit::result<fuchsia::mem::Buffer> result = CollectSystemLog();

  ASSERT_TRUE(result.is_error());
}

TEST_F(CollectSystemLogTest, Fail_LogCollectionTimesOut) {
  ResetStubLogger(std::make_unique<StubLoggerBindsToLogListenerButNeverCalls>());

  fit::result<fuchsia::mem::Buffer> result = CollectSystemLog();

  ASSERT_TRUE(result.is_error());
}

class LogListenerTest : public gtest::TestLoopFixture {
 public:
  LogListenerTest() : executor_(dispatcher()), service_directory_provider_(dispatcher()) {}

 protected:
  async::Executor executor_;
  ::sys::testing::ServiceDirectoryProvider service_directory_provider_;
};

// DX-1602
TEST_F(LogListenerTest, Succeed_LoggerClosesConnectionAfterSuccessfulFlow) {
  std::unique_ptr<StubLogger> stub_logger = std::make_unique<StubLogger>();
  stub_logger->set_messages({
      BuildLogMessage(FX_LOG_INFO, "msg"),
  });
  FXL_CHECK(service_directory_provider_.AddService(stub_logger->GetHandler()) == ZX_OK);

  // Since we are using a test loop with a fake clock, the actual duration doesn't matter so we can
  // set it arbitrary long.
  const zx::duration timeout = zx::sec(1);
  fit::result<void> result;
  LogListener log_listener(dispatcher(), service_directory_provider_.service_directory());
  executor_.schedule_task(log_listener.CollectLogs(timeout).then(
      [&result](const fit::result<void>& res) { result = std::move(res); }));
  RunLoopFor(timeout);

  // First, we check we have had a successful flow.
  ASSERT_TRUE(result.is_ok());

  // Then, we check that if the logger closes the connection (and triggers the error handler on the
  // LogListener side), we don't crash (cf. DX-1602).
  stub_logger->CloseAllConnections();
}

TEST_F(LogListenerTest, Fail_CallCollectLogsTwice) {
  std::unique_ptr<StubLogger> stub_logger = std::make_unique<StubLogger>();
  stub_logger->set_messages({
      BuildLogMessage(FX_LOG_INFO, "msg"),
  });
  FXL_CHECK(service_directory_provider_.AddService(stub_logger->GetHandler()) == ZX_OK);

  const zx::duration unused_timeout = zx::sec(1);
  LogListener log_listener(dispatcher(), service_directory_provider_.service_directory());
  executor_.schedule_task(log_listener.CollectLogs(unused_timeout));
  ASSERT_DEATH(log_listener.CollectLogs(unused_timeout),
               testing::HasSubstr("CollectLogs() is not intended to be called twice"));
}

}  // namespace
}  // namespace feedback

namespace mem {

// Pretty-prints string VMOs in gTest matchers instead of the default byte string in case of failed
// expectations.
void PrintTo(const Buffer& vmo, std::ostream* os) {
  std::string value;
  FXL_CHECK(fsl::StringFromVmo(vmo, &value));
  *os << "'" << value << "'";
}

}  // namespace mem
}  // namespace fuchsia

int main(int argc, char** argv) {
  if (!fxl::SetTestSettings(argc, argv)) {
    return EXIT_FAILURE;
  }

  testing::InitGoogleTest(&argc, argv);
  syslog::InitLogger({"feedback", "test"});
  return RUN_ALL_TESTS();
}
