// Exercises the Linux menu against a real session bus and an isolated tray
// host.
#include <QByteArray>
#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>
#include <QVariant>
#include <QVariantMap>
#include <functional>
#include <iostream>

namespace {
constexpr int kTimeoutMs = 10000;
constexpr int kPollMs = 10;

bool WaitUntil(const std::function<bool()>& condition) {
  QElapsedTimer timer;
  timer.start();
  while (!condition() && timer.elapsed() < kTimeoutMs) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, kPollMs);
    QThread::msleep(kPollMs);
  }
  return condition();
}

class Watcher : public QObject, protected QDBusContext {
  Q_OBJECT
  Q_CLASSINFO("D-Bus Interface", "org.kde.StatusNotifierWatcher")
  Q_PROPERTY(bool IsStatusNotifierHostRegistered READ hostRegistered)
  Q_PROPERTY(int ProtocolVersion READ protocolVersion)
  Q_PROPERTY(QStringList RegisteredStatusNotifierItems READ registeredItems)

 public:
  bool hostRegistered() const { return true; }
  int protocolVersion() const { return 0; }
  QStringList registeredItems() const { return items_; }
  QString service() const { return service_; }

 public slots:
  void RegisterStatusNotifierItem(const QString& item) {
    service_ = item.startsWith(QLatin1Char('/')) ? message().service() : item;
    items_.append(item);
    emit StatusNotifierItemRegistered(item);
  }
  void RegisterStatusNotifierHost(const QString&) {}

 signals:
  void StatusNotifierItemRegistered(const QString& service);
  void StatusNotifierItemUnregistered(const QString& service);
  void StatusNotifierHostRegistered();
  void StatusNotifierHostUnregistered();

 private:
  QString service_;
  QStringList items_;
};

QDBusMessage Call(QDBusInterface& interface, const QString& method,
                  const QVariantList& args = {}) {
  QDBusPendingCallWatcher watcher(
      interface.asyncCallWithArgumentList(method, args));
  if (!WaitUntil([&] { return watcher.isFinished(); })) {
    std::cerr << "D-Bus call timed out: " << method.toStdString() << '\n';
    return QDBusMessage::createError("test.timeout", "call timed out");
  }
  return watcher.reply();
}

struct MenuScan {
  int quit = -1;
  bool settings = false;
  bool replay_status = false;
  bool camera_status = false;
  QString expected_camera;
  QString expected_version;
  bool version_first = false;
  int top_items = 0;
};

void ScanMenu(const QDBusArgument& argument, MenuScan& scan, int depth = 0) {
  argument.beginStructure();
  int id = 0;
  QVariantMap properties;
  argument >> id >> properties;
  QString label = properties.value("label").toString();
  label.remove(QLatin1Char('&'));
  if (depth == 1 && scan.top_items++ == 0) {
    scan.version_first = label == scan.expected_version &&
                         !properties.value("enabled", true).toBool();
  }
  if (label.compare("Quit", Qt::CaseInsensitive) == 0) scan.quit = id;
  if (label.startsWith("Settings") &&
      properties.value("enabled", true).toBool()) {
    scan.settings = true;
  }
  if (label == "Gesture replay is available offline." &&
      !properties.value("enabled", true).toBool()) {
    scan.replay_status = true;
  }
  if (label == scan.expected_camera &&
      !properties.value("enabled", true).toBool()) {
    scan.camera_status = true;
  }
  argument.beginArray();
  while (!argument.atEnd()) {
    QVariant child;
    argument >> child;
    if (child.metaType() == QMetaType::fromType<QDBusVariant>()) {
      child = qvariant_cast<QDBusVariant>(child).variant();
    }
    if (child.metaType() == QMetaType::fromType<QDBusArgument>()) {
      ScanMenu(qvariant_cast<QDBusArgument>(child), scan, depth + 1);
    }
  }
  argument.endArray();
  argument.endStructure();
}

bool RunWithWatcher(const QString& executable, bool service_available) {
  QProcess version;
  version.start(executable, {"--version"});
  if (!version.waitForFinished(kTimeoutMs) || version.exitCode() != 0)
    return false;
  const QString version_output =
      QString::fromUtf8(version.readAllStandardOutput()).trimmed();
  if (!version_output.startsWith("facepalm-menu ")) return false;
  const QString expected_version =
      "Facepalm " + version_output.mid(QString("facepalm-menu ").size());
  QTemporaryDir runtime;
  if (!runtime.isValid()) return false;
  const QString socket = runtime.filePath("service.sock");
  QLocalServer service;
  if (service_available && !service.listen(socket)) return false;
  auto bus = QDBusConnection::sessionBus();
  Watcher watcher;
  if (!bus.registerService("org.kde.StatusNotifierWatcher") ||
      !bus.registerObject("/StatusNotifierWatcher", &watcher,
                          QDBusConnection::ExportAllSlots |
                              QDBusConnection::ExportAllSignals |
                              QDBusConnection::ExportAllProperties)) {
    std::cerr << "Could not register test watcher\n";
    return false;
  }
  QProcess app;
  app.setProcessChannelMode(QProcess::ForwardedChannels);
  auto environment = QProcessEnvironment::systemEnvironment();
  environment.insert("FACEPALM_SOCKET", socket);
  app.setProcessEnvironment(environment);
  app.start(executable);
  if (!app.waitForStarted(kTimeoutMs) ||
      !WaitUntil([&] { return !watcher.service().isEmpty(); })) {
    std::cerr << "Menu did not register a StatusNotifierItem\n";
    app.kill();
    app.waitForFinished(kTimeoutMs);
    return false;
  }
  QDBusInterface properties(watcher.service(), "/StatusNotifierItem",
                            "org.freedesktop.DBus.Properties", bus);
  auto icon =
      Call(properties, "Get", {"org.kde.StatusNotifierItem", "IconPixmap"});
  auto menu = Call(properties, "Get", {"org.kde.StatusNotifierItem", "Menu"});
  if (icon.type() == QDBusMessage::ErrorMessage || icon.arguments().isEmpty() ||
      menu.type() == QDBusMessage::ErrorMessage || menu.arguments().isEmpty()) {
    std::cerr << "Tray did not export icon and menu properties\n";
    app.kill();
    app.waitForFinished(kTimeoutMs);
    return false;
  }
  const QVariant icon_value =
      qvariant_cast<QDBusVariant>(icon.arguments().first()).variant();
  const QVariant menu_value =
      qvariant_cast<QDBusVariant>(menu.arguments().first()).variant();
  const QString menu_path = qvariant_cast<QDBusObjectPath>(menu_value).path();
  const QDBusArgument pixmaps = qvariant_cast<QDBusArgument>(icon_value);
  pixmaps.beginArray();
  bool has_pixels = false;
  if (!pixmaps.atEnd()) {
    int width = 0;
    int height = 0;
    QByteArray bytes;
    pixmaps.beginStructure();
    pixmaps >> width >> height >> bytes;
    pixmaps.endStructure();
    has_pixels = width > 0 && height > 0 && !bytes.isEmpty();
  }
  pixmaps.endArray();
  if (!has_pixels || menu_path.isEmpty() || menu_path == "/NO_DBUSMENU") {
    std::cerr << "Tray icon or menu is empty\n";
    app.kill();
    app.waitForFinished(kTimeoutMs);
    return false;
  }
  QDBusInterface menu_interface(watcher.service(), menu_path,
                                "com.canonical.dbusmenu", bus);
  const auto layout = Call(menu_interface, "GetLayout", {0, -1, QStringList{}});
  if (layout.type() == QDBusMessage::ErrorMessage ||
      layout.arguments().size() < 2) {
    std::cerr << "Tray menu layout unavailable\n";
    app.kill();
    app.waitForFinished(kTimeoutMs);
    return false;
  }
  MenuScan scan;
  scan.expected_version = expected_version;
  scan.expected_camera = service_available ? "Camera service is ready."
                                           : "Camera service is offline.";
  ScanMenu(qvariant_cast<QDBusArgument>(layout.arguments().at(1)), scan);
  if (!scan.version_first)
    std::cerr << "Version is not the first disabled item\n";
  if (!scan.version_first || scan.quit < 0 || !scan.settings ||
      !scan.replay_status || !scan.camera_status ||
      Call(menu_interface, "Event",
           {scan.quit, "clicked", QVariant::fromValue(QDBusVariant(0)),
            QVariant::fromValue(uint{0})})
              .type() == QDBusMessage::ErrorMessage ||
      !WaitUntil([&] { return app.state() == QProcess::NotRunning; }) ||
      app.exitStatus() != QProcess::NormalExit || app.exitCode() != 0) {
    std::cerr << "Tray Quit did not exit cleanly\n";
    app.kill();
    app.waitForFinished(kTimeoutMs);
    return false;
  }
  bus.unregisterObject("/StatusNotifierWatcher");
  bus.unregisterService("org.kde.StatusNotifierWatcher");
  return true;
}

// A listening but unresponsive owner must not be reported as success.
bool RunUnresponsiveOwner(const QString& executable) {
  QTemporaryDir runtime;
  if (!runtime.isValid()) return false;
  QLockFile lock(runtime.filePath("menu.lock"));
  QLocalServer server;
  if (!lock.tryLock(0) || !server.listen(runtime.filePath("menu.sock")))
    return false;
  auto environment = QProcessEnvironment::systemEnvironment();
  environment.insert("FACEPALM_MENU_LOCK", runtime.filePath("menu.lock"));
  environment.insert("FACEPALM_MENU_SOCKET", runtime.filePath("menu.sock"));
  QProcess launch;
  launch.setProcessEnvironment(environment);
  QElapsedTimer timer;
  timer.start();
  launch.start(executable, {"--settings"});
  if (!launch.waitForStarted(kTimeoutMs) ||
      !WaitUntil([&] { return launch.state() == QProcess::NotRunning; })) {
    launch.kill();
    launch.waitForFinished(kTimeoutMs);
    return false;
  }
  return launch.exitStatus() == QProcess::NormalExit &&
         launch.exitCode() != 0 && timer.elapsed() < 3000 &&
         launch.readAllStandardError().contains(
             "facepalm-menu: settings request was not acknowledged by the "
             "running tray");
}

// Runs the real tray server with isolated paths and a headless Qt platform.
bool RunHeadlessIpc(const QString& executable) {
  QTemporaryDir runtime;
  QTemporaryDir config;
  if (!runtime.isValid() || !config.isValid()) return false;
  auto environment = QProcessEnvironment::systemEnvironment();
  environment.insert("QT_QPA_PLATFORM", "offscreen");
  environment.insert("FACEPALM_MENU_LOCK", runtime.filePath("menu.lock"));
  environment.insert("FACEPALM_MENU_SOCKET", runtime.filePath("menu.sock"));
  environment.insert("FACEPALM_SOCKET", runtime.filePath("service.sock"));
  environment.insert("XDG_CONFIG_HOME", config.path());
  QProcess first;
  first.setProcessEnvironment(environment);
  first.start(executable);
  const bool started = first.waitForStarted(kTimeoutMs) && WaitUntil([&] {
                         return QFile::exists(runtime.filePath("menu.sock"));
                       });
  bool ok = started;
  // Invalid input and incomplete requests must close without an ACK. The
  // invalid bytes can already be buffered when newConnection is dispatched.
  for (const QByteArray& input : {QByteArray("bad"), QByteArray("set")}) {
    if (!ok) break;
    QLocalSocket client;
    client.connectToServer(runtime.filePath("menu.sock"));
    ok = client.waitForConnected(kTimeoutMs) &&
         client.write(input) == input.size() &&
         client.waitForBytesWritten(kTimeoutMs);
    if (ok && client.state() != QLocalSocket::UnconnectedState) {
      ok = client.waitForDisconnected(2000);
    }
    ok = ok && client.readAll().isEmpty();
    if (!ok) std::cerr << "Invalid or stalled request did not close\n";
  }
  if (ok) {
    QLocalSocket client;
    client.connectToServer(runtime.filePath("menu.sock"));
    ok = client.waitForConnected(kTimeoutMs) && client.write("set") == 3 &&
         client.waitForBytesWritten(kTimeoutMs);
    if (ok) {
      QElapsedTimer delay;
      delay.start();
      while (delay.elapsed() < 50) {
        QCoreApplication::processEvents();
        QThread::msleep(1);
      }
      ok = client.bytesAvailable() == 0 && client.write("tings") == 5 &&
           client.waitForBytesWritten(kTimeoutMs) &&
           WaitUntil([&] { return client.bytesAvailable() >= 2; }) &&
           client.read(2) == "ok";
      if (ok && client.state() != QLocalSocket::UnconnectedState) {
        ok = client.waitForDisconnected(2000);
      }
      ok = ok && client.readAll().isEmpty();
    }
  }
  if (ok) {
    QProcess second;
    second.setProcessEnvironment(environment);
    second.start(executable, {"--settings"});
    ok = second.waitForStarted(kTimeoutMs) &&
         WaitUntil([&] { return second.state() == QProcess::NotRunning; }) &&
         second.exitStatus() == QProcess::NormalExit && second.exitCode() == 0;
    if (!ok) {
      second.kill();
      second.waitForFinished(kTimeoutMs);
    }
  }
  first.kill();
  first.waitForFinished(kTimeoutMs);
  return ok;
}

// #98: a second launch while the first still runs must not open a second
// tray icon, and its --settings should raise the first's Settings window
// instead (specs/SingleInstance.tla's TryLockHeld and Forward).
bool RunSecondLaunch(const QString& executable) {
  QTemporaryDir runtime;
  QTemporaryDir config_home;
  if (!runtime.isValid() || !config_home.isValid()) return false;
  auto bus = QDBusConnection::sessionBus();
  Watcher watcher;
  if (!bus.registerService("org.kde.StatusNotifierWatcher") ||
      !bus.registerObject("/StatusNotifierWatcher", &watcher,
                          QDBusConnection::ExportAllSlots |
                              QDBusConnection::ExportAllSignals |
                              QDBusConnection::ExportAllProperties)) {
    std::cerr << "Could not register test watcher\n";
    return false;
  }
  auto environment = QProcessEnvironment::systemEnvironment();
  // No server ever listens on this socket, so both launches see the
  // camera as offline, deterministically; each gets its own isolated
  // config so neither touches a real one.
  environment.insert("FACEPALM_SOCKET", runtime.filePath("service.sock"));
  environment.insert("FACEPALM_MENU_LOCK", runtime.filePath("menu.lock"));
  environment.insert("FACEPALM_MENU_SOCKET", runtime.filePath("menu.sock"));
  environment.insert("XDG_CONFIG_HOME", config_home.path());

  QProcess first;
  first.setProcessChannelMode(QProcess::ForwardedChannels);
  first.setProcessEnvironment(environment);
  first.start(executable);
  if (!first.waitForStarted(kTimeoutMs) ||
      !WaitUntil([&] { return !watcher.service().isEmpty(); })) {
    std::cerr << "First instance did not register a StatusNotifierItem\n";
    first.kill();
    first.waitForFinished(kTimeoutMs);
    return false;
  }

  // Send a request in two pieces to exercise buffering in the real tray.
  QLocalSocket fragmented;
  fragmented.connectToServer(runtime.filePath("menu.sock"));
  if (!fragmented.waitForConnected(kTimeoutMs) ||
      fragmented.write("set") != 3 ||
      !fragmented.waitForBytesWritten(kTimeoutMs)) {
    first.kill();
    first.waitForFinished(kTimeoutMs);
    return false;
  }
  QCoreApplication::processEvents();
  if (fragmented.bytesAvailable() != 0 || fragmented.write("tings") != 5 ||
      !fragmented.waitForBytesWritten(kTimeoutMs) ||
      !WaitUntil([&] { return fragmented.bytesAvailable() >= 2; }) ||
      fragmented.read(2) != "ok") {
    std::cerr << "Fragmented settings request was not acknowledged\n";
    first.kill();
    first.waitForFinished(kTimeoutMs);
    return false;
  }

  QProcess second;
  second.setProcessChannelMode(QProcess::ForwardedChannels);
  second.setProcessEnvironment(environment);
  second.start(executable, {"--settings"});
  const bool second_exited =
      second.waitForStarted(kTimeoutMs) &&
      WaitUntil([&] { return second.state() == QProcess::NotRunning; }) &&
      second.exitStatus() == QProcess::NormalExit && second.exitCode() == 0;
  if (!second_exited) {
    std::cerr << "Second launch did not hand off and exit cleanly\n";
    first.kill();
    first.waitForFinished(kTimeoutMs);
    second.kill();
    second.waitForFinished(kTimeoutMs);
    return false;
  }
  if (watcher.registeredItems().size() != 1) {
    std::cerr << "A second launch opened its own tray icon\n";
    first.kill();
    first.waitForFinished(kTimeoutMs);
    return false;
  }

  const bool settings_shown = WaitUntil([&] {
    QProcess search;
    search.start("xdotool",
                 {"search", "--onlyvisible", "--name", "^Facepalm Settings$"});
    return search.waitForFinished(kTimeoutMs) && search.exitCode() == 0;
  });
  if (!settings_shown) {
    std::cerr << "The second launch's --settings did not raise the first "
                 "instance's window\n";
    first.kill();
    first.waitForFinished(kTimeoutMs);
    return false;
  }

  QDBusInterface properties(watcher.service(), "/StatusNotifierItem",
                            "org.freedesktop.DBus.Properties", bus);
  const auto menu =
      Call(properties, "Get", {"org.kde.StatusNotifierItem", "Menu"});
  if (menu.type() == QDBusMessage::ErrorMessage || menu.arguments().isEmpty()) {
    std::cerr << "Tray menu unavailable while quitting the first instance\n";
    first.kill();
    first.waitForFinished(kTimeoutMs);
    return false;
  }
  const QString menu_path =
      qvariant_cast<QDBusObjectPath>(
          qvariant_cast<QDBusVariant>(menu.arguments().first()).variant())
          .path();
  QDBusInterface menu_interface(watcher.service(), menu_path,
                                "com.canonical.dbusmenu", bus);
  const auto layout = Call(menu_interface, "GetLayout", {0, -1, QStringList{}});
  MenuScan scan;
  scan.expected_camera = "Camera service is offline.";
  if (layout.type() != QDBusMessage::ErrorMessage &&
      layout.arguments().size() >= 2) {
    ScanMenu(qvariant_cast<QDBusArgument>(layout.arguments().at(1)), scan);
  }
  const bool quit_ok =
      scan.quit >= 0 &&
      Call(menu_interface, "Event",
           {scan.quit, "clicked", QVariant::fromValue(QDBusVariant(0)),
            QVariant::fromValue(uint{0})})
              .type() != QDBusMessage::ErrorMessage &&
      WaitUntil([&] { return first.state() == QProcess::NotRunning; }) &&
      first.exitStatus() == QProcess::NormalExit && first.exitCode() == 0;
  if (!quit_ok) {
    std::cerr << "First instance did not quit cleanly\n";
    first.kill();
    first.waitForFinished(kTimeoutMs);
  }
  bus.unregisterObject("/StatusNotifierWatcher");
  bus.unregisterService("org.kde.StatusNotifierWatcher");
  return quit_ok;
}

bool RunWithoutWatcher(const QString& executable, bool close_window) {
  QTemporaryDir config_home;
  if (!config_home.isValid() || !QDir(config_home.path()).mkpath("facepalm")) {
    return false;
  }
  const QString path = config_home.filePath("facepalm/facepalm.conf");
  QFile config(path);
  const QByteArray initial(
      "camera.frames_per_second = 15 # preserve me\n"
      "head_pose.enabled = false\n");
  if (!config.open(QIODevice::WriteOnly) ||
      config.write(initial) != initial.size()) {
    return false;
  }
  config.close();
  QProcess app;
  app.setProcessChannelMode(QProcess::ForwardedChannels);
  auto environment = QProcessEnvironment::systemEnvironment();
  environment.insert("XDG_CONFIG_HOME", config_home.path());
  app.setProcessEnvironment(environment);
  app.start(executable);
  if (!app.waitForStarted(kTimeoutMs)) return false;
  QString window;
  const bool shown = WaitUntil([&] {
    QProcess search;
    search.start("xdotool",
                 {"search", "--onlyvisible", "--name", "^Facepalm Settings$"});
    if (!search.waitForFinished(kTimeoutMs) || search.exitCode() != 0)
      return false;
    window = QString::fromUtf8(search.readAllStandardOutput())
                 .split('\n', Qt::SkipEmptyParts)
                 .value(0);
    return !window.isEmpty();
  });
  QProcess geometry;
  if (shown) {
    geometry.start("xdotool", {"getwindowgeometry", "--shell", window});
    geometry.waitForFinished(kTimeoutMs);
  }
  int width = 0;
  int height = 0;
  for (const QString& line :
       QString::fromUtf8(geometry.readAllStandardOutput()).split('\n')) {
    if (line.startsWith("WIDTH=")) width = line.mid(6).toInt();
    if (line.startsWith("HEIGHT=")) height = line.mid(7).toInt();
  }
  QProcess close;
  if (shown && width > 0 && height > 20) {
    if (close_window) {
      close.start("xdotool", {"key", "--window", window, "Escape"});
    } else {
      QProcess::execute("xdotool", {"windowfocus", window, "key", "ctrl+a"});
      QProcess::execute("xdotool", {"type", "24"});
      close.start("xdotool",
                  {"mousemove", "--window", window, QString::number(width - 76),
                   QString::number(height - 36), "click", "1"});
    }
    close.waitForFinished(kTimeoutMs);
  }
  // Looking away's and screen context's own rows (#35, #61 item 11, #79)
  // made the real dialog taller than the window's own initial resize()
  // hint; Qt's layout stretches it to fit everything shown, so the upper
  // bound here is generous rather than pinned to one exact height.
  const bool settings_size =
      width >= 510 && width <= 600 && height >= 430 && height <= 900;
  const bool exited =
      shown && settings_size && (close_window || close.exitCode() == 0) &&
      WaitUntil([&] { return app.state() == QProcess::NotRunning; }) &&
      app.exitStatus() == QProcess::NormalExit && app.exitCode() == 0;
  if (!exited) {
    std::cerr << "No-tray fallback failed: shown=" << shown
              << " window=" << window.toStdString() << " size=" << width << "x"
              << height << " close=" << close.exitCode()
              << " state=" << app.state() << " exit=" << app.exitCode()
              << " status=" << app.exitStatus()
              << " xdotool=" << close.readAllStandardError().toStdString()
              << "\n";
    app.kill();
    app.waitForFinished(kTimeoutMs);
  }
  if (!exited || !config.open(QIODevice::ReadOnly)) return false;
  const auto saved = config.readAll();
  if (close_window) return saved == initial;
  if (!saved.contains("camera.frames_per_second = 24 # preserve me") ||
      !saved.contains("head_pose.enabled = false")) {
    std::cerr
        << "Settings save did not preserve the comment and unrelated value\n";
    return false;
  }
  return true;
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  if (app.arguments().size() != 2) {
    std::cerr << "Expected the path to facepalm-menu\n";
    return 2;
  }
  const QString executable = app.arguments().at(1);
  if (qEnvironmentVariableIsSet("FACEPALM_TEST_IPC_ONLY")) {
    return RunHeadlessIpc(executable) && RunUnresponsiveOwner(executable) ? 0
                                                                          : 1;
  }
  return RunWithWatcher(executable, false) &&
                 RunWithWatcher(executable, true) &&
                 RunSecondLaunch(executable) &&
                 RunUnresponsiveOwner(executable) &&
                 RunWithoutWatcher(executable, false) &&
                 RunWithoutWatcher(executable, true)
             ? 0
             : 1;
}

#include "linux_tray_integration.moc"
