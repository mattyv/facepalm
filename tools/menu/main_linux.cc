#include <QAction>
#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSystemTrayIcon>
#include <QVBoxLayout>

namespace {

constexpr auto kToolsStatus = "Gesture replay is available offline.";
constexpr auto kCameraStatus = "Linux camera service is not provided.";

QIcon FacepalmIcon() {
  QIcon icon = QIcon::fromTheme(QStringLiteral("facepalm"));
  if (!icon.isNull()) return icon;
  const QDir executable(QCoreApplication::applicationDirPath());
  return QIcon(executable.filePath(
      QStringLiteral("../share/icons/hicolor/128x128/apps/facepalm.png")));
}

}  // namespace

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  QCoreApplication::setApplicationName(QStringLiteral("facepalm-menu"));
  QCoreApplication::setApplicationVersion(QStringLiteral(FACEPALM_VERSION));
  QCommandLineParser parser;
  parser.setApplicationDescription(
      QStringLiteral("Facepalm portable tools status"));
  parser.addHelpOption();
  parser.addVersionOption();
  parser.process(app);

  const QIcon icon = FacepalmIcon();
  app.setWindowIcon(icon);
  QDialog window;
  window.setWindowTitle(QStringLiteral("Facepalm"));
  auto* layout = new QVBoxLayout(&window);
  auto* tools = new QLabel(QString::fromUtf8(kToolsStatus), &window);
  auto* camera = new QLabel(QString::fromUtf8(kCameraStatus), &window);
  tools->setWordWrap(true);
  camera->setWordWrap(true);
  layout->addWidget(tools);
  layout->addWidget(camera);
  auto* quit = new QPushButton(QStringLiteral("Quit"), &window);
  layout->addWidget(quit);
  QObject::connect(quit, &QPushButton::clicked, &app, &QApplication::quit);

  const bool tray_available = QSystemTrayIcon::isSystemTrayAvailable();
  if (!tray_available) {
    window.show();
    return app.exec();
  }

  // Follow Qt's system tray example: a context menu owns the quit action and
  // closing the status window leaves the tray application running.
  app.setQuitOnLastWindowClosed(false);
  QMenu menu;
  QAction* tools_status = menu.addAction(QString::fromUtf8(kToolsStatus));
  QAction* camera_status = menu.addAction(QString::fromUtf8(kCameraStatus));
  tools_status->setEnabled(false);
  camera_status->setEnabled(false);
  menu.addSeparator();
  QAction* quit_action = menu.addAction(QStringLiteral("Quit"));
  QObject::connect(quit_action, &QAction::triggered, &app, &QApplication::quit);
  QSystemTrayIcon tray(icon);
  tray.setToolTip(QStringLiteral("Facepalm"));
  tray.setContextMenu(&menu);
  QObject::connect(&tray, &QSystemTrayIcon::activated, &window,
                   [&window](QSystemTrayIcon::ActivationReason reason) {
                     if (reason == QSystemTrayIcon::Trigger) {
                       window.show();
                       window.raise();
                       window.activateWindow();
                     }
                   });
  tray.show();
  return app.exec();
}
