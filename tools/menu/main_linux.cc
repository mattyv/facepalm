#include <QAction>
#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLocalSocket>
#include <QMenu>
#include <QPushButton>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QVBoxLayout>

namespace {

constexpr auto kReplayStatus = "Gesture replay is available offline.";
constexpr auto kCameraReady = "Camera service is ready.";
constexpr auto kCameraOffline = "Camera service is offline.";

QIcon FacepalmIcon() {
  QIcon icon = QIcon::fromTheme(QStringLiteral("facepalm"));
  if (!icon.isNull()) {
    return icon;
  }
  const QDir executable(QCoreApplication::applicationDirPath());
  return QIcon(executable.filePath(
      QStringLiteral("../share/icons/hicolor/128x128/apps/facepalm.png")));
}

QString SocketPath() {
  const QString override_path = qEnvironmentVariable("FACEPALM_SOCKET");
  if (!override_path.isEmpty()) {
    return override_path;
  }
  const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
  const QString state = qEnvironmentVariable("XDG_STATE_HOME");
  const QString base =
      !runtime.isEmpty()
          ? runtime
          : (state.isEmpty() ? QDir::homePath() + "/.local/state" : state);
  return QDir(base).filePath(QStringLiteral("facepalm/service.sock"));
}

bool ServiceReady(const QString& path) {
  QLocalSocket socket;
  socket.connectToServer(path);
  return socket.state() == QLocalSocket::ConnectedState ||
         socket.waitForConnected(50);
}

void Emphasize(QLabel& label, int point_size = 0) {
  auto font = label.font();
  font.setBold(true);
  if (point_size > 0) {
    font.setPointSize(point_size);
  }
  label.setFont(font);
}

}  // namespace

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  QCoreApplication::setApplicationName(QStringLiteral("facepalm-menu"));
  QCoreApplication::setApplicationVersion(QStringLiteral(FACEPALM_VERSION));
  QCommandLineParser parser;
  parser.setApplicationDescription(
      QStringLiteral("Facepalm camera and replay status"));
  parser.addHelpOption();
  parser.addVersionOption();
  parser.process(app);

  const QIcon icon = FacepalmIcon();
  app.setWindowIcon(icon);
  QDialog window;
  window.setWindowTitle(QStringLiteral("Facepalm"));
  window.setFixedWidth(364);
  auto* layout = new QVBoxLayout(&window);
  layout->setContentsMargins(18, 16, 18, 16);
  layout->setSpacing(12);

  auto* heading = new QLabel(QStringLiteral("Facepalm"), &window);
  Emphasize(*heading, 17);
  layout->addWidget(heading);

  auto* card = new QFrame(&window);
  card->setObjectName(QStringLiteral("cameraCard"));
  card->setStyleSheet(
      QStringLiteral("QFrame#cameraCard { background: palette(base); "
                     "border: 1px solid palette(mid); border-radius: 8px; }"));
  auto* card_layout = new QVBoxLayout(card);
  card_layout->setContentsMargins(12, 10, 12, 11);
  card_layout->setSpacing(4);
  auto* camera_row = new QHBoxLayout;
  auto* camera_title = new QLabel(QStringLiteral("Camera service"), card);
  Emphasize(*camera_title);
  camera_row->addWidget(camera_title);
  camera_row->addStretch();
  auto* camera_status = new QLabel(card);
  Emphasize(*camera_status);
  camera_row->addWidget(camera_status);
  card_layout->addLayout(camera_row);
  auto* camera_detail = new QLabel(card);
  camera_detail->setWordWrap(true);
  card_layout->addWidget(camera_detail);
  layout->addWidget(card);

  auto* replay = new QLabel(QString::fromUtf8(kReplayStatus), &window);
  layout->addWidget(replay);
  auto* quit = new QPushButton(QStringLiteral("Quit"), &window);
  quit->setMinimumWidth(70);
  layout->addWidget(quit, 0, Qt::AlignRight);
  QObject::connect(quit, &QPushButton::clicked, &app, &QApplication::quit);

  QMenu menu;
  QAction* services = menu.addAction(QString());
  services->setEnabled(false);
  QAction* tools = menu.addAction(QString::fromUtf8(kReplayStatus));
  tools->setEnabled(false);
  menu.addSeparator();
  QAction* show = menu.addAction(QStringLiteral("Show Facepalm"));
  const auto show_window = [&window] {
    window.show();
    window.raise();
    window.activateWindow();
  };
  QObject::connect(show, &QAction::triggered, &window, show_window);
  QAction* quit_action = menu.addAction(QStringLiteral("Quit"));
  QObject::connect(quit_action, &QAction::triggered, &app, &QApplication::quit);

  const QString socket_path = SocketPath();
  const auto refresh = [&] {
    const bool ready = ServiceReady(socket_path);
    camera_status->setText(ready ? QStringLiteral("Ready")
                                 : QStringLiteral("Offline"));
    camera_detail->setText(
        ready ? QStringLiteral("The webcam opens when an approved MCP client "
                               "requests reactions.")
              : QStringLiteral(
                    "Start the Facepalm daemon to use live webcam reactions."));
    services->setText(QString::fromUtf8(ready ? kCameraReady : kCameraOffline));
    window.adjustSize();
  };
  refresh();
  QTimer refresh_timer;
  QObject::connect(&refresh_timer, &QTimer::timeout, &window, refresh);
  refresh_timer.start(3000);

  if (!QSystemTrayIcon::isSystemTrayAvailable()) {
    window.show();
    return app.exec();
  }

  app.setQuitOnLastWindowClosed(false);
  QSystemTrayIcon tray(icon);
  tray.setToolTip(QStringLiteral("Facepalm"));
  tray.setContextMenu(&menu);
  QObject::connect(&tray, &QSystemTrayIcon::activated, &window,
                   [show_window](QSystemTrayIcon::ActivationReason reason) {
                     if (reason == QSystemTrayIcon::Trigger) {
                       show_window();
                     }
                   });
  tray.show();
  return app.exec();
}
