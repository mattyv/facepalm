#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"
#include "src/calibrate_cli.h"
#include "src/calibration_flow.h"
#include "src/protocol.h"

namespace facepalm {
namespace {

constexpr auto kSocketPath = "/tmp/service.sock";

// Answers a connection's hello with protocol::ErrorJson(message_), the
// way a daemon refusing a client does; good for one CalibrationFlow::Start
// call.
class RefusingIo : public CalibrateClientIo {
 public:
  void Close() override {}
  explicit RefusingIo(std::string message) : message_(std::move(message)) {}

  bool Connect(const std::string& /* path */) override { return true; }
  void WriteLine(std::string_view /* line */) override {}
  std::optional<std::string> ReadLine() override {
    return protocol::ErrorJson(message_);
  }

 private:
  std::string message_;
};

// Property test (#178): whatever bytes a daemon-side refusal or error
// message carries, CalibrationFlow reads it back exactly as
// protocol::Quoted (src/protocol.cc) wrote it onto the wire -- never
// truncated at an escaped quote or backslash, which src/calibration_flow.cc's
// own hand-rolled StringField failed to undo before #178's fix.
void ErrorMessageRoundTrips(const std::string& message) {
  RefusingIo io(message);
  CalibrationFlow flow(kSocketPath, /* token= */ "", io,
                       [](std::chrono::milliseconds /* duration */) {});

  flow.Start(/* screens= */ 1);

  EXPECT_EQ(flow.CurrentView().state, CalibrationFlow::State::kFailed);
  EXPECT_EQ(flow.CurrentView().error, message);
}
FUZZ_TEST(CalibrationFlowFuzz, ErrorMessageRoundTrips)
    .WithDomains(fuzztest::Arbitrary<std::string>());

}  // namespace
}  // namespace facepalm
