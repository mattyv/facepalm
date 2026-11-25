#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <QApplication>
#include <QDialog>
#include <QEvent>
#include <QGuiApplication>
#include <QLabel>
#include <QMetaObject>
#include <QPushButton>
#include <QScreen>
#include <QWindow>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "gtest/gtest.h"
#include "tools/menu/calibration_window_linux.h"

namespace {
class Server {
 public:
  Server() : path_("/tmp/facepalm-window-" + std::to_string(getpid())) {
    listener_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listener_ < 0) {
      return;
    }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, path_.c_str(), sizeof(address.sun_path) - 1);
    if (bind(listener_, reinterpret_cast<sockaddr*>(&address),
             sizeof(address)) != 0) {
      return;
    }
    owns_path_ = true;
    if (listen(listener_, 1) != 0) {
      return;
    }
    ready_ = true;
    thread_ = std::thread([this] { Serve(); });
  }
  ~Server() { Stop(); }
  [[nodiscard]] bool Ready() const { return ready_; }
  [[nodiscard]] const std::string& Path() const { return path_; }
  void Stop() {
    stop_ = true;
    if (thread_.joinable()) {
      thread_.join();
    }
    if (listener_ >= 0) {
      close(listener_);
      listener_ = -1;
    }
    if (owns_path_) {
      unlink(path_.c_str());
      owns_path_ = false;
    }
  }
  [[nodiscard]] const std::vector<std::string>& Requests() const {
    return requests_;
  }

 private:
  bool Wait(int fd) {
    pollfd p{.fd = fd, .events = POLLIN, .revents = 0};
    while (!stop_) {
      if (poll(&p, 1, 100) > 0) {
        return true;
      }
    }
    return false;
  }
  void Serve() {
    if (!Wait(listener_)) {
      return;
    }
    const int client = accept(listener_, nullptr, nullptr);
    if (client < 0) {
      return;
    }
    while (!stop_) {
      std::string line;
      char c = 0;
      while (Wait(client) && read(client, &c, 1) == 1 && c != '\n') {
        line += c;
      }
      if (line.empty()) {
        break;
      }
      requests_.push_back(line);
      std::string reply = R"({"ok":true})";
      if (line.starts_with("calibrate start")) {
        reply = R"({"state":"running","screen":0,"point":"centre"})";
      } else if (line == "calibrate next" && requests_.size() == 3) {
        reply = R"({"state":"running","screen":1,"point":"top_left"})";
      } else if (line == "calibrate next") {
        reply =
            "{\"state\":\"running\",\"screen\":1,\"point\":\"top_left\","
            "\"refused\":\"retry\"}";
      }
      reply += '\n';
      const auto ignored =
          send(client, reply.data(), reply.size(), MSG_NOSIGNAL);
      (void)ignored;
      if (line == "calibrate cancel") {
        break;
      }
    }
    close(client);
  }
  std::string path_;
  int listener_ = -1;
  bool ready_ = false;
  bool owns_path_ = false;
  std::atomic<bool> stop_ = false;
  std::thread thread_;
  std::vector<std::string> requests_;
};

TEST(CalibrationWindowServer, StopsAndRemovesSocketWithoutAConnection) {
  std::string path;
  {
    Server const server;
    ASSERT_TRUE(server.Ready());
    path = server.Path();
    EXPECT_EQ(access(path.c_str(), F_OK), 0);
  }
  EXPECT_NE(access(path.c_str(), F_OK), 0);
}

class HideObserver : public QObject {
 public:
  int hides = 0;
  bool interrupt = false;
  bool eventFilter(QObject* object, QEvent* event) override {
    if (event->type() == QEvent::Hide) {
      ++hides;
      if (interrupt) {
        interrupt = false;
        QMetaObject::invokeMethod(
            qApp, "screenAdded", Qt::DirectConnection,
            Q_ARG(QScreen*, QGuiApplication::screens().front()));
      }
    }
    return QObject::eventFilter(object, event);
  }
};

class CalibrationWindowIntegration : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(server.Ready());
    wizard = std::make_unique<facepalm_menu::CalibrationWindow>(
        &parent, server.Path(), [] { return std::string(); });
    wizard->Open("");
    dialog = parent.findChild<QDialog*>();
    ASSERT_NE(dialog, nullptr);
    dialog->installEventFilter(&observer);
    for (auto* button : dialog->findChildren<QPushButton*>()) {
      if (button->text() == "Start Calibration") {
        primary = button;
      }
    }
    ASSERT_NE(primary, nullptr);
    primary->click();
  }
  static void TriggerTopology() {
    EXPECT_TRUE(QMetaObject::invokeMethod(
        qApp, "screenAdded", Qt::DirectConnection,
        Q_ARG(QScreen*, QGuiApplication::screens().front())));
  }
  void VerifyCanceled() {
    auto* label = dialog->findChild<QLabel*>();
    ASSERT_NE(label, nullptr);
    EXPECT_TRUE(label->text().contains("restart", Qt::CaseInsensitive));
    EXPECT_TRUE(dialog->isVisible());
    server.Stop();
    ASSERT_FALSE(server.Requests().empty());
    EXPECT_EQ(server.Requests().back(), "calibrate cancel");
    EXPECT_EQ(std::count(server.Requests().begin(), server.Requests().end(),
                         "calibrate cancel"),
              1);
  }
  Server server;
  QWidget parent;
  std::unique_ptr<facepalm_menu::CalibrationWindow> wizard;
  QDialog* dialog = nullptr;
  QPushButton* primary = nullptr;
  HideObserver observer;
};

TEST_F(CalibrationWindowIntegration, TopologyChangeCancelsAndShowsRestart) {
  auto* label = dialog->findChild<QLabel*>();
  ASSERT_NE(label, nullptr);
  EXPECT_TRUE(label->text().contains("centre"));
  EXPECT_TRUE(label->text().contains("Enter"));
  TriggerTopology();
  QApplication::processEvents();
  VerifyCanceled();
}

TEST_F(CalibrationWindowIntegration, CloseInvalidatesQueuedRestartGuidance) {
  TriggerTopology();
  QPushButton* close_button = nullptr;
  for (auto* button : dialog->findChildren<QPushButton*>()) {
    if (button->text() == "Close") {
      close_button = button;
    }
  }
  ASSERT_NE(close_button, nullptr);
  close_button->click();
  QApplication::processEvents();
  EXPECT_FALSE(dialog->isVisible());
  server.Stop();
  ASSERT_FALSE(server.Requests().empty());
  EXPECT_EQ(server.Requests().back(), "calibrate cancel");
}

TEST_F(CalibrationWindowIntegration, WaylandRemapRetryAndInterruption) {
  if (QGuiApplication::platformName() != "wayland" ||
      QGuiApplication::screens().size() < 2) {
    GTEST_SKIP() << "requires two Wayland outputs";
  }
  EXPECT_TRUE(dialog->isFullScreen());
  EXPECT_EQ(dialog->windowHandle()->screen(), QGuiApplication::primaryScreen());
  observer.interrupt = true;
  primary->click();
  QApplication::processEvents();
  EXPECT_FALSE(dialog->isFullScreen());
  EXPECT_NE(dialog->windowHandle()->screen(), QGuiApplication::screens().at(1));
  VerifyCanceled();
}

TEST_F(CalibrationWindowIntegration, WaylandRetryDoesNotRemap) {
  if (QGuiApplication::platformName() != "wayland" ||
      QGuiApplication::screens().size() < 2) {
    GTEST_SKIP() << "requires two Wayland outputs";
  }
  primary->click();
  EXPECT_EQ(dialog->windowHandle()->screen(), QGuiApplication::screens().at(1));
  const int hides = observer.hides;
  primary->click();
  EXPECT_EQ(observer.hides, hides);
  TriggerTopology();
  QApplication::processEvents();
  VerifyCanceled();
}
}  // namespace

int main(int argc, char** argv) {
  QApplication const app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
