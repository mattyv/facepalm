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
#include <QElapsedTimer>
#include <QEventLoop>
#include <QProcess>
#include <QString>
#include <QStringList>
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
  bool replay_status = false;
  bool camera_status = false;
};

void ScanMenu(const QDBusArgument& argument, MenuScan& scan) {
  argument.beginStructure();
  int id = 0;
  QVariantMap properties;
  argument >> id >> properties;
  QString label = properties.value("label").toString();
  label.remove(QLatin1Char('&'));
  if (label.compare("Quit", Qt::CaseInsensitive) == 0) scan.quit = id;
  if (label == "Gesture replay is available offline." &&
      !properties.value("enabled", true).toBool()) {
    scan.replay_status = true;
  }
  if (label == "Linux camera service is not provided." &&
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
      ScanMenu(qvariant_cast<QDBusArgument>(child), scan);
    }
  }
  argument.endArray();
  argument.endStructure();
}

bool RunWithWatcher(const QString& executable) {
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
  ScanMenu(qvariant_cast<QDBusArgument>(layout.arguments().at(1)), scan);
  if (scan.quit < 0 || !scan.replay_status || !scan.camera_status ||
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

bool RunWithoutWatcher(const QString& executable, bool close_window) {
  QProcess app;
  app.setProcessChannelMode(QProcess::ForwardedChannels);
  app.start(executable);
  if (!app.waitForStarted(kTimeoutMs)) return false;
  QString window;
  const bool shown = WaitUntil([&] {
    QProcess search;
    search.start("xdotool",
                 {"search", "--onlyvisible", "--name", "[Ff]acepalm"});
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
      close.start("xdotool",
                  {"mousemove", "--window", window, QString::number(width / 2),
                   QString::number(height - 20), "click", "1"});
    }
    close.waitForFinished(kTimeoutMs);
  }
  const bool exited =
      shown && width > 0 && height > 20 &&
      (close_window || close.exitCode() == 0) &&
      WaitUntil([&] { return app.state() == QProcess::NotRunning; }) &&
      app.exitStatus() == QProcess::NormalExit && app.exitCode() == 0;
  if (!exited) {
    std::cerr << "No-tray fallback failed: shown=" << shown
              << " window=" << window.toStdString()
              << " close=" << close.exitCode() << " state=" << app.state()
              << " exit=" << app.exitCode() << " status=" << app.exitStatus()
              << " xdotool=" << close.readAllStandardError().toStdString()
              << "\n";
    app.kill();
    app.waitForFinished(kTimeoutMs);
  }
  return exited;
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  if (app.arguments().size() != 2) {
    std::cerr << "Expected the path to facepalm-menu\n";
    return 2;
  }
  const QString executable = app.arguments().at(1);
  return RunWithWatcher(executable) && RunWithoutWatcher(executable, false) &&
                 RunWithoutWatcher(executable, true)
             ? 0
             : 1;
}

#include "linux_tray_integration.moc"
