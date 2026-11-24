#include <QCoreApplication>
#include <QThread>
#include <cstdio>

#include "gtest/gtest.h"
#include "tools/menu/service_health_linux.h"

namespace {
QString executable;

TEST(ServiceHealthCheck, ReadsOnlySuccessfulCompleteOutput) {
  facepalm_menu::ServiceHealthQuery query;
  EXPECT_EQ(query.Read(executable, {"failed"}, 1000),
            facepalm::ServiceHealth::kFailed);
  EXPECT_EQ(query.Read(executable, {"starting"}, 1000),
            facepalm::ServiceHealth::kStarting);
  EXPECT_EQ(query.Read(executable, {"partial"}, 1000),
            facepalm::ServiceHealth::kUnavailable);
  EXPECT_EQ(query.Read(executable, {"error"}, 1000),
            facepalm::ServiceHealth::kUnavailable);
  EXPECT_EQ(query.Read("/no-such-facepalm-query", {}, 1000),
            facepalm::ServiceHealth::kUnavailable);
}

TEST(ServiceHealthCheck, TimesOutAndRetiresBeforeAnotherQuery) {
  facepalm_menu::ServiceHealthQuery query;
  EXPECT_EQ(query.Read(executable, {"hang"}, 50),
            facepalm::ServiceHealth::kUnavailable);
  EXPECT_TRUE(query.Pending());
  EXPECT_EQ(query.Read(executable, {"failed"}, 50),
            facepalm::ServiceHealth::kUnavailable);
  for (int i = 0; i < 100 && query.Pending(); ++i) {
    QCoreApplication::processEvents();
    QThread::msleep(10);
  }
  EXPECT_FALSE(query.Pending());
  EXPECT_EQ(query.Read(executable, {"failed"}, 1000),
            facepalm::ServiceHealth::kFailed);
}
}  // namespace

int main(int argc, char** argv) {
  const QCoreApplication app(argc, argv);
  executable = QCoreApplication::applicationFilePath();
  if (argc > 1) {
    const QString mode = QString::fromLocal8Bit(argv[1]);
    if (mode == "hang") {
      QThread::sleep(5);
      return 0;
    }
    if (mode == "failed") {
      fputs("failed\n", stdout);
      return 0;
    }
    if (mode == "starting") {
      fputs("activating\n", stdout);
      return 0;
    }
    if (mode == "partial") {
      fputs("fail", stdout);
      return 0;
    }
    if (mode == "error") {
      fputs("failed\n", stdout);
      return 1;
    }
  }
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
