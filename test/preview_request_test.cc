#include "src/preview_request.h"

#include <optional>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

// Each test stands for the specs/PreviewLease.tla Fresh formula or the
// #114 behavior it covers.

namespace facepalm {
namespace {

using IsFreshPreviewRequestTest = ::testing::Test;

TEST_F(IsFreshPreviewRequestTest, AMissingFileIsNotFresh) {
  EXPECT_FALSE(IsFreshPreviewRequest(
      "/does/not/matter", 1000,
      [](const std::string&) { return std::optional<std::int64_t>(); }));
}

TEST_F(IsFreshPreviewRequestTest, JustTouchedIsFresh) {
  EXPECT_TRUE(IsFreshPreviewRequest("/request", 1000, [](const std::string&) {
    return std::optional<std::int64_t>(1000);
  }));
}

TEST_F(IsFreshPreviewRequestTest, ExactlyAtTheLimitIsStillFresh) {
  EXPECT_TRUE(IsFreshPreviewRequest(
      "/request", kPreviewRequestStaleMs,
      [](const std::string&) { return std::optional<std::int64_t>(0); }));
}

TEST_F(IsFreshPreviewRequestTest, OneMillisecondPastTheLimitIsStale) {
  EXPECT_FALSE(IsFreshPreviewRequest(
      "/request", kPreviewRequestStaleMs + 1,
      [](const std::string&) { return std::optional<std::int64_t>(0); }));
}

TEST_F(IsFreshPreviewRequestTest, AbandonedLongAgoIsStale) {
  // The scenario #114 is about: the menu quit, crashed, or its session
  // logged out, and nothing has touched the request since.
  EXPECT_FALSE(IsFreshPreviewRequest(
      "/request", 1000000,
      [](const std::string&) { return std::optional<std::int64_t>(0); }));
}

TEST_F(IsFreshPreviewRequestTest, PassesThePathToModifiedMs) {
  std::string seen;
  IsFreshPreviewRequest("/the/path", 0, [&seen](const std::string& path) {
    seen = path;
    return std::optional<std::int64_t>(0);
  });
  EXPECT_EQ(seen, "/the/path");
}

}  // namespace
}  // namespace facepalm
