#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>
#include <string>
#include <thread>

#include "gtest/gtest.h"
#include "src/calibration_flow.h"
#include "tools/posix_calibrate_client_io.h"

namespace {

bool ServeUntilEof(int listener) {
  const int client = accept(listener, nullptr, nullptr);
  if (client < 0) {
    return false;
  }
  char byte;
  for (int request = 0; request < 2; ++request) {
    while (read(client, &byte, 1) == 1 && byte != '\n') {
    }
    const char* reply =
        request == 0 ? "{\"ok\":true}\n" : "{\"error\":\"failed\"}\n";
    const auto ignored = write(client, reply, std::strlen(reply));
    (void)ignored;
  }
  pollfd ready{.fd = client, .events = POLLIN, .revents = 0};
  const bool saw_eof = poll(&ready, 1, 2000) > 0 && read(client, &byte, 1) == 0;
  close(client);
  return saw_eof;
}

}  // namespace

TEST(CalibrationTransportIntegration, FailedStartClosesRealSocket) {
  const std::string path =
      "/tmp/facepalm-calibration-" + std::to_string(getpid());
  const int listener = socket(AF_UNIX, SOCK_STREAM, 0);
  ASSERT_GE(listener, 0);
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);
  ASSERT_EQ(
      bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)),
      0);
  ASSERT_EQ(listen(listener, 1), 0);
  bool saw_eof = false;
  std::thread server([&] { saw_eof = ServeUntilEof(listener); });
  facepalm::PosixCalibrateClientIo io;
  facepalm::CalibrationFlow flow(path, "", io);
  flow.Start(1);
  EXPECT_EQ(flow.CurrentView().state,
            facepalm::CalibrationFlow::State::kFailed);
  server.join();
  EXPECT_TRUE(saw_eof);
  close(listener);
  unlink(path.c_str());
}
