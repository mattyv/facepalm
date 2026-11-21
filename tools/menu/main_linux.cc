#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QStringDecoder>
#include <QStringList>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "src/client_approvals.h"
#include "src/client_token.h"
#include "src/daemon_status.h"
#include "src/settings_document.h"
#include "src/single_instance.h"
#include "src/user_paths.h"
#include "src/user_service.h"

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
  QString override_path = qEnvironmentVariable("FACEPALM_SOCKET");
  if (!override_path.isEmpty()) {
    return override_path;
  }
  // The daemon's fixed per-user socket (src/user_paths.h).
  return QDir::homePath() +
         QStringLiteral("/.local/state/facepalm/service.sock");
}

bool ServiceReady(const QString& path) {
  constexpr int kConnectTimeoutMs = 50;
  QLocalSocket socket;
  socket.connectToServer(path);
  return socket.state() == QLocalSocket::ConnectedState ||
         socket.waitForConnected(kConnectTimeoutMs);
}

// The camera-in-use file (#37 item 8), from src/user_paths.h: no new path
// or token of its own.
QString StatusPath() {
  const std::optional<facepalm::UserPaths> paths = facepalm::PathsFor(
      QDir::homePath().toStdString(), facepalm::Platform::kLinux);
  return paths ? QString::fromStdString(paths->status) : QString();
}

// The single-instance guard's own state, next to the daemon's socket
// (#98): a QLockFile for the exclusive claim, and a QLocalServer name for
// forwarding a Settings request to whichever launch holds it.
QString MenuLockPath() {
  QString override_path = qEnvironmentVariable("FACEPALM_MENU_LOCK");
  if (!override_path.isEmpty()) {
    return override_path;
  }
  return QDir::homePath() + QStringLiteral("/.local/state/facepalm/menu.lock");
}

QString MenuSocketPath() {
  QString override_path = qEnvironmentVariable("FACEPALM_MENU_SOCKET");
  if (!override_path.isEmpty()) {
    return override_path;
  }
  return QDir::homePath() + QStringLiteral("/.local/state/facepalm/menu.sock");
}

constexpr auto kSettingsRequest = "settings";

// The real primitives behind src/single_instance.h: QLockFile::tryLock()
// is the one atomic call TryLockAtomic needs (specs/SingleInstance.tla),
// and it already reclaims a lock left by a process that has since died
// (GuardStaleReclaimed), rather than wedging every launch after it.
// RequestSettings is a short-lived QLocalSocket connection to whichever
// instance is listening.
class QtSingleInstanceIo : public facepalm::SingleInstanceIo {
 public:
  // `lock_path` and `socket_path` are two different files this instance
  // owns outright, and the one call site below names its arguments the
  // same way, so a swap would be visible there.
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  QtSingleInstanceIo(const QString& lock_path, QString socket_path)
      : lock_(lock_path), socket_path_(std::move(socket_path)) {}

  bool TryLock() override { return lock_.tryLock(0); }

  bool RequestSettings() override {
    constexpr int kRequestTimeoutMs = 200;
    QLocalSocket socket;
    socket.connectToServer(socket_path_);
    if (!socket.waitForConnected(kRequestTimeoutMs)) {
      return false;
    }
    socket.write(kSettingsRequest);
    return socket.waitForBytesWritten(kRequestTimeoutMs);
  }

 private:
  QLockFile lock_;
  QString socket_path_;
};

QString ConfigPath() {
  const QString config = qEnvironmentVariable("XDG_CONFIG_HOME");
  return QDir(config.isEmpty() ? QDir::homePath() + "/.config" : config)
      .filePath(QStringLiteral("facepalm/facepalm.conf"));
}

// A plain comparable value, not a class with an invariant to protect.
struct ConfigSnapshot {
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes)
  QByteArray bytes;
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes)
  bool missing = false;

  bool operator==(const ConfigSnapshot&) const = default;
};

std::optional<ConfigSnapshot> ReadConfig(const QString& path, QString& error) {
  constexpr qint64 kMaxConfigBytes = qint64{1024} * 1024;
  const QFileInfo info(path);
  if (!info.exists() && !info.isSymLink()) {
    return ConfigSnapshot{.bytes = {}, .missing = true};
  }
  if (!info.isFile()) {
    error = QStringLiteral("The settings path must point to a regular file.");
    return std::nullopt;
  }
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    error = file.errorString();
    return std::nullopt;
  }
  const QByteArray bytes = file.read(kMaxConfigBytes + 1);
  if (file.error() != QFileDevice::NoError) {
    error = file.errorString();
    return std::nullopt;
  }
  QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
  const QString decoded = decoder(bytes);
  if (bytes.size() > kMaxConfigBytes || decoded.contains(QChar::Null) ||
      decoder.hasError()) {
    error =
        QStringLiteral("The settings file must be UTF-8 and at most 1 MiB.");
    return std::nullopt;
  }
  return ConfigSnapshot{.bytes = bytes};
}

// The user service (#37): src/user_service.h holds the logic, following
// specs/UserService.tla; this supplies the install's paths. `on` is what
// the person asked for; returns an error message, or an empty string.
QString SwitchService(bool on) {
  const std::string home = QDir::homePath().toStdString();
  const std::optional<facepalm::UserPaths> paths =
      facepalm::PathsFor(home, facepalm::Platform::kLinux);
  if (!paths) {
    return QStringLiteral("Your home folder could not be found.");
  }
  const std::unique_ptr<facepalm::ApprovalsIo> approvals_io =
      facepalm::PosixApprovalsIo();
  facepalm::ClientApprovals approvals(
      paths->clients,
      [](const std::string& path) { return facepalm::LoadClientToken(path); },
      *approvals_io);
  const std::unique_ptr<facepalm::ServiceIo> io = facepalm::PosixServiceIo();
  facepalm::UserService service(
      facepalm::Platform::kLinux, home,
      [&approvals](std::string_view name, std::string& error) {
        return approvals.Approve(name, error);
      },
      *io);
  std::string error;
  if (!on) {
    return service.TurnOff(error) ? QString() : QString::fromStdString(error);
  }
  // The tray's own install first, then the camera build's documented one.
  const std::optional<facepalm::ServiceProgram> program = facepalm::FindProgram(
      {QDir::cleanPath(QCoreApplication::applicationDirPath() + "/..")
           .toStdString(),
       home + "/.local/facepalm"},
      ConfigPath().toStdString(), *io);
  if (!program) {
    return QStringLiteral(
        "The Facepalm camera daemon is not installed; see the Linux daemon "
        "build in Facepalm's docs.");
  }
  return service.TurnOn(*program, error) ? QString()
                                         : QString::fromStdString(error);
}

bool ServiceIsOn() {
  const std::unique_ptr<facepalm::ServiceIo> io = facepalm::PosixServiceIo();
  facepalm::UserService service(
      facepalm::Platform::kLinux, QDir::homePath().toStdString(),
      [](std::string_view, std::string&) {
        return std::optional<std::string>();
      },
      *io);
  return service.IsOn();
}

// facepalm-daemon's own path: the tray's own install first, then the
// documented one, the same two prefixes SwitchService's FindProgram tries
// (#37); empty when neither has it. Finding it needs no face model, since
// listing cameras does not run one.
QString DaemonPath() {
  const QStringList prefixes{
      QDir::cleanPath(QCoreApplication::applicationDirPath() +
                      QStringLiteral("/..")),
      QDir::homePath() + QStringLiteral("/.local/facepalm")};
  for (const QString& prefix : prefixes) {
    const QString candidate =
        QDir(prefix).filePath(QStringLiteral("bin/facepalm-daemon"));
    if (QFileInfo(candidate).isExecutable()) {
      return candidate;
    }
  }
  return QString();
}

// The cameras facepalm-daemon's own `cameras` command lists (item 10 of
// #61, #13): id, name and whether built in, tab-separated, one camera a
// line. Run synchronously, since opening Settings already waits on
// reading the settings file; empty, with no error shown, when the daemon
// cannot be found or does not run, so the picker then offers only the
// default.
QList<QStringList> ListCameras() {
  const QString daemon = DaemonPath();
  if (daemon.isEmpty()) {
    return {};
  }
  QProcess process;
  process.start(daemon, {QStringLiteral("cameras")});
  constexpr int kListCamerasTimeoutMs = 2000;
  if (!process.waitForFinished(kListCamerasTimeoutMs)) {
    return {};
  }
  const QString text = QString::fromUtf8(process.readAllStandardOutput());
  QList<QStringList> devices;
  for (const QString& line :
       text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
    const QStringList fields = line.split(QLatin1Char('\t'));
    if (fields.size() == 3) {
      devices.append(fields);
    }
  }
  return devices;
}

QString Number(float value) {
  constexpr std::size_t kBufferSize = 64;
  std::array<char, kBufferSize> buffer{};
  const auto result =
      std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
  return QString::fromLatin1(buffer.data(), result.ptr - buffer.data());
}

class SettingsDialog : public QDialog {
 public:
  SettingsDialog() {
    setWindowTitle(QStringLiteral("Facepalm Settings"));
    resize(kWindowWidth, kWindowHeight);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(kFormMargin, kFormMargin, kFormMargin,
                               kFormMargin);
    layout->setSpacing(kFormSpacing);

    auto* note = new QLabel(
        QStringLiteral("Start the daemon with --config and this file to apply "
                       "these settings."),
        this);
    note->setWordWrap(true);
    layout->addWidget(note);
    layout->addSpacing(kHeadingSpacing);

    auto* basic = new QGridLayout;
    ConfigureForm(*basic);
    fps_ = new QSpinBox(this);
    fps_->setRange(1, kMaxFramesPerSecond);
    AddField(*basic, 0, QStringLiteral("Frames per second (1–60)"), *fps_);
    // The chosen camera (item 10 of #61, #13): QComboBox has no
    // setAlignment, unlike AddField's other fields, so this row is added
    // directly rather than through that helper.
    camera_ = new QComboBox(this);
    camera_->setAccessibleName(QStringLiteral("Camera"));
    auto* camera_label =
        new QLabel(QStringLiteral("Camera (next start)"), this);
    camera_label->setBuddy(camera_);
    basic->addWidget(camera_label, 1, 0);
    basic->addWidget(camera_, 1, 1);
    layout->addLayout(basic);
    gestures_ = new QCheckBox(QStringLiteral("Detect head gestures"), this);
    expressions_ =
        new QCheckBox(QStringLiteral("Detect facial expressions"), this);
    layout->addWidget(gestures_);
    layout->addWidget(expressions_);

    auto* heading =
        new QLabel(QStringLiteral("Advanced tuning (next start)"), this);
    auto font = heading->font();
    font.setBold(true);
    heading->setFont(font);
    layout->addWidget(heading);

    auto* advanced = new QGridLayout;
    ConfigureForm(*advanced);
    for (std::size_t i = 0; i < kAdvancedFieldCount; ++i) {
      fields_[i] = new QLineEdit(this);
      AddField(*advanced, static_cast<int>(i),
               QString::fromUtf8(kFieldLabels[i]), *fields_[i]);
    }
    layout->addLayout(advanced);

    // Looking away (#35, #79).
    auto* look_away_heading = new QLabel(QStringLiteral("Looking away"), this);
    look_away_heading->setFont(font);
    layout->addWidget(look_away_heading);
    looking_away_ = new QCheckBox(QStringLiteral("Report looking away"), this);
    layout->addWidget(looking_away_);
    auto* look_away_fields = new QGridLayout;
    ConfigureForm(*look_away_fields);
    for (std::size_t i = kAdvancedFieldCount; i < kAdvancedFieldCount + 2;
         ++i) {
      fields_[i] = new QLineEdit(this);
      AddField(*look_away_fields, static_cast<int>(i - kAdvancedFieldCount),
               QString::fromUtf8(kFieldLabels[i]), *fields_[i]);
    }
    layout->addLayout(look_away_fields);

    // Screen context (#61 item 11, #79).
    auto* screen_context_heading =
        new QLabel(QStringLiteral("Screen context"), this);
    screen_context_heading->setFont(font);
    layout->addWidget(screen_context_heading);
    screen_context_ =
        new QCheckBox(QStringLiteral("Report screen context"), this);
    layout->addWidget(screen_context_);
    screen_context_window_titles_ = new QCheckBox(
        QStringLiteral("Include window titles (needs Screen Recording access)"),
        this);
    layout->addWidget(screen_context_window_titles_);
    auto* screen_context_fields = new QGridLayout;
    ConfigureForm(*screen_context_fields);
    fields_[kAdvancedFieldCount + 2] = new QLineEdit(this);
    AddField(*screen_context_fields, 0,
             QString::fromUtf8(kFieldLabels[kAdvancedFieldCount + 2]),
             *fields_[kAdvancedFieldCount + 2]);
    layout->addLayout(screen_context_fields);

    layout->addStretch();

    auto* path_label = new QLabel(path_, this);
    path_label->setTextFormat(Qt::PlainText);
    path_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    path_label->setWordWrap(true);
    layout->addWidget(path_label);
    layout->addSpacing(kPathSpacing);
    save_ = new QPushButton(QStringLiteral("Save Settings"), this);
    save_->setDefault(true);
    save_->setMinimumSize(kSaveButtonWidth, kSaveButtonHeight);
    layout->addWidget(save_, 0, Qt::AlignRight);
    connect(save_, &QPushButton::clicked, this, [this] { Save(); });
  }

  // Not QDialog::open(): raises and reloads an already-open dialog instead
  // of opening a new one.
  // NOLINTNEXTLINE(bugprone-virtual-near-miss)
  void Open() {
    if (!isVisible()) {
      Reload();
    }
    show();
    raise();
    activateWindow();
  }

 private:
  // The first kAdvancedFieldCount entries are the expression tuning grid;
  // the rest are looking away's and screen context's own numeric fields
  // (#35, #61 item 11, #79).
  static constexpr std::size_t kAdvancedFieldCount = 7;
  static constexpr int kWindowWidth = 560;
  static constexpr int kWindowHeight = 610;
  static constexpr int kFormMargin = 24;
  static constexpr int kFormSpacing = 12;
  static constexpr int kHeadingSpacing = 14;
  static constexpr int kMaxFramesPerSecond = 60;
  static constexpr int kPathSpacing = 10;
  static constexpr int kSaveButtonWidth = 136;
  static constexpr int kSaveButtonHeight = 28;
  static constexpr int kFieldFormRightMargin = 26;
  static constexpr int kFieldFormVerticalSpacing = 8;
  static constexpr int kFieldWidth = 110;

  static constexpr std::array kFieldLabels{
      "Gesture confidence (0–1)",   "Expression smallest rise (0–1)",
      "Expression spreads (0–100)", "Expression hold (0–5000 ms)",
      "Pose degrees (0–180)",       "Pose range (0–180)",
      "Pose settle (0–5000 ms)",    "Degrees (0–180)",
      "Hold (0–10000 ms)",          "Other-screen degrees (0–180)",
  };

  static constexpr std::array kSettingMembers{
      &facepalm::MenuSettings::gesture_confidence,
      &facepalm::MenuSettings::expression_rise,
      &facepalm::MenuSettings::expression_spreads,
      &facepalm::MenuSettings::expression_hold_ms,
      &facepalm::MenuSettings::expression_pose_degrees,
      &facepalm::MenuSettings::expression_pose_range,
      &facepalm::MenuSettings::expression_settle_ms,
      &facepalm::MenuSettings::looking_away_degrees,
      &facepalm::MenuSettings::looking_away_hold_ms,
      &facepalm::MenuSettings::screen_context_other_screen_degrees,
  };

  static void ConfigureForm(QGridLayout& form) {
    form.setContentsMargins(0, 0, kFieldFormRightMargin, 0);
    form.setColumnStretch(0, 1);
    form.setVerticalSpacing(kFieldFormVerticalSpacing);
  }

  template <typename Field>
  void AddField(QGridLayout& form, int row, const QString& title,
                Field& field) {
    auto* label = new QLabel(title, this);
    label->setBuddy(&field);
    field.setAccessibleName(title);
    field.setFixedWidth(kFieldWidth);
    field.setAlignment(Qt::AlignRight);
    form.addWidget(label, row, 0);
    form.addWidget(&field, row, 1);
  }

  void ShowError(const QString& error) {
    QMessageBox box(QMessageBox::Warning, windowTitle(), error, QMessageBox::Ok,
                    this);
    box.setTextFormat(Qt::PlainText);
    box.exec();
  }

  void Reload() {
    load_error_.clear();
    snapshot_ = ReadConfig(path_, load_error_);
    facepalm::Config config;
    if (snapshot_) {
      std::string error;
      const auto parsed =
          facepalm::ParseConfig(std::string_view(snapshot_->bytes.constData(),
                                                 snapshot_->bytes.size()),
                                error);
      if (parsed) {
        config = *parsed;
      } else {
        load_error_ = QString::fromStdString(error);
        snapshot_.reset();
      }
    }
    fps_->setValue(config.frames_per_second);
    camera_->clear();
    camera_->addItem(QStringLiteral("Default (built-in camera)"), QString());
    const QString wanted = QString::fromStdString(config.camera_device_id);
    bool found_wanted = wanted.isEmpty();
    for (const QStringList& device : ListCameras()) {
      const QString& id = device[0];
      const QString& name = device[1];
      const bool built_in = device[2] == QStringLiteral("1");
      camera_->addItem(built_in ? name + QStringLiteral(" (built-in)") : name,
                       id);
      found_wanted = found_wanted || id == wanted;
    }
    if (!found_wanted) {
      // Kept as its own item so saving without touching the picker does
      // not silently fall back to the default while the chosen camera is
      // away (item 10 of #61, #13).
      camera_->addItem(wanted + QStringLiteral(" (not connected)"), wanted);
    }
    camera_->setCurrentIndex(std::max(0, camera_->findData(wanted)));
    gestures_->setChecked(config.gestures_enabled);
    expressions_->setChecked(config.expressions_enabled);
    looking_away_->setChecked(config.looking_away_enabled);
    screen_context_->setChecked(config.screen_context_enabled);
    screen_context_window_titles_->setChecked(
        config.screen_context_window_titles_enabled);
    const std::array values{
        Number(config.gesture_confidence_threshold),
        Number(config.expressions_smallest_rise),
        Number(config.expressions_spreads),
        QString::number(config.expressions_hold_ms),
        Number(config.expressions_pose_degrees),
        Number(config.expressions_pose_range),
        QString::number(config.expressions_settle_ms),
        Number(config.looking_away_degrees),
        QString::number(config.looking_away_hold_ms),
        Number(config.screen_context_other_screen_degrees),
    };
    for (std::size_t i = 0; i < fields_.size(); ++i) {
      fields_[i]->setText(values[i]);
    }
    fps_->setFocus();
  }

  void Save() {
    if (!snapshot_) {
      ShowError(load_error_);
      return;
    }
    facepalm::MenuSettings settings;
    settings.frames_per_second = fps_->value();
    settings.camera_device_id = camera_->currentData().toString().toStdString();
    settings.gestures_enabled = gestures_->isChecked();
    settings.expressions_enabled = expressions_->isChecked();
    settings.looking_away_enabled = looking_away_->isChecked();
    settings.screen_context_enabled = screen_context_->isChecked();
    settings.screen_context_window_titles_enabled =
        screen_context_window_titles_->isChecked();
    for (std::size_t i = 0; i < fields_.size(); ++i) {
      settings.*kSettingMembers[i] = fields_[i]->text().toStdString();
    }
    std::string error;
    const auto updated = facepalm::UpdateSettingsDocument(
        std::string_view(snapshot_->bytes.constData(), snapshot_->bytes.size()),
        settings, error);
    if (!updated) {
      ShowError(QString::fromStdString(error));
      return;
    }

    QString read_error;
    const auto current = ReadConfig(path_, read_error);
    if (!current) {
      ShowError(read_error);
      return;
    }
    if (*current != *snapshot_) {
      ShowError(
          QStringLiteral("The settings file changed. Reopen Facepalm Settings "
                         "and try again."));
      return;
    }
    if (!QDir().mkpath(QFileInfo(path_).absolutePath())) {
      ShowError(QStringLiteral("Could not create the settings folder."));
      return;
    }
    QSaveFile file(path_);
    const QByteArray bytes = QByteArray::fromStdString(*updated);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() ||
        !file.commit()) {
      ShowError(file.errorString());
      return;
    }
    accept();
  }

  const QString path_ = ConfigPath();
  std::optional<ConfigSnapshot> snapshot_;
  QString load_error_;
  QSpinBox* fps_;
  QComboBox* camera_;
  QCheckBox* gestures_;
  QCheckBox* expressions_;
  QCheckBox* looking_away_;
  QCheckBox* screen_context_;
  QCheckBox* screen_context_window_titles_;
  QPushButton* save_;
  std::array<QLineEdit*, kFieldLabels.size()> fields_{};
};

}  // namespace

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  QCoreApplication::setApplicationName(QStringLiteral("facepalm-menu"));
  QCoreApplication::setApplicationVersion(QStringLiteral(FACEPALM_VERSION));
  QCommandLineParser parser;
  parser.setApplicationDescription(
      QStringLiteral("Facepalm settings and camera status"));
  parser.addHelpOption();
  parser.addVersionOption();
  const QCommandLineOption settings(QStringLiteral("settings"),
                                    QStringLiteral("Open Facepalm Settings."));
  parser.addOption(settings);
  parser.process(app);

  // specs/SingleInstance.tla: TryLockAtomic and TryLockHeld (#98). A
  // second launch while one is already running must not open a second
  // tray icon: it is handed off to the one running instead, and exits
  // here without ever building a window or a tray.
  const QString lock_path = MenuLockPath();
  QDir().mkpath(QFileInfo(lock_path).absolutePath());
  const QString instance_socket = MenuSocketPath();
  QtSingleInstanceIo single_instance(lock_path, instance_socket);
  if (facepalm::ClaimSingleInstance(parser.isSet(settings), single_instance) ==
      facepalm::SingleInstanceRole::kForwarded) {
    return 0;
  }

  const QIcon icon = FacepalmIcon();
  QApplication::setWindowIcon(icon);
  SettingsDialog window;

  QMenu menu;
  QAction* camera_status = menu.addAction(QString());
  camera_status->setEnabled(false);
  QAction* services = menu.addAction(QString());
  services->setEnabled(false);
  QAction* tools = menu.addAction(QString::fromUtf8(kReplayStatus));
  tools->setEnabled(false);
  QAction* power = menu.addAction(QString());
  const auto show_power = [power] {
    power->setText(ServiceIsOn() ? QStringLiteral("Turn Off Facepalm")
                                 : QStringLiteral("Turn On Facepalm…"));
  };
  show_power();
  QObject::connect(&menu, &QMenu::aboutToShow, &window, show_power);
  // Turning on is the person's consent, so it asks first.
  QObject::connect(power, &QAction::triggered, &window, [&window, show_power] {
    const bool on = ServiceIsOn();
    if (!on &&
        QMessageBox::question(
            &window, QStringLiteral("Turn on Facepalm?"),
            QStringLiteral(
                "Facepalm will use your camera while an app you "
                "approved, such as Claude, asks how you react, and it "
                "starts again each time you log in until you turn it "
                "off. Frames stay on this computer.")) != QMessageBox::Yes) {
      return;
    }
    const QString error = SwitchService(!on);
    if (!error.isEmpty()) {
      QMessageBox::warning(
          &window,
          on ? QStringLiteral("Facepalm could not be turned off")
             : QStringLiteral("Facepalm could not be turned on"),
          error);
    }
    show_power();
  });
  menu.addSeparator();
  const QAction* show = menu.addAction(QStringLiteral("Settings…"));
  const auto show_window = [&window] { window.Open(); };
  QObject::connect(show, &QAction::triggered, &window, show_window);
  const QAction* quit_action = menu.addAction(QStringLiteral("Quit"));
  QObject::connect(quit_action, &QAction::triggered, &app, &QApplication::quit);

  const QString socket_path = SocketPath();
  const auto refresh = [&] {
    const bool ready = ServiceReady(socket_path);
    services->setText(QString::fromUtf8(ready ? kCameraReady : kCameraOffline));
  };
  refresh();
  constexpr int kRefreshIntervalMs = 3000;
  QTimer refresh_timer;
  QObject::connect(&refresh_timer, &QTimer::timeout, &window, refresh);
  refresh_timer.start(kRefreshIntervalMs);

  // The camera-in-use indicator (#37 item 8): its own, faster timer so the
  // tray settles within the two seconds it is asked to, independent of the
  // slower daemon-reachability check above.
  constexpr int kCameraStatusIntervalMs = 1000;
  const QString status_path = StatusPath();
  const auto refresh_camera_status = [=] {
    camera_status->setText(facepalm::ReadCameraStatus(status_path.toStdString())
                               ? QStringLiteral("Camera on")
                               : QStringLiteral("Camera off"));
  };
  refresh_camera_status();
  QTimer camera_status_timer;
  QObject::connect(&camera_status_timer, &QTimer::timeout, &window,
                   refresh_camera_status);
  camera_status_timer.start(kCameraStatusIntervalMs);

  // specs/SingleInstance.tla: Forward. This instance holds the lock, so
  // any socket a stale run left behind has no live owner; listen for a
  // later launch's Settings request and raise this window for it.
  QLocalServer::removeServer(instance_socket);
  QLocalServer tray_server;
  tray_server.listen(instance_socket);
  QObject::connect(
      &tray_server, &QLocalServer::newConnection, &window,
      [&tray_server, show_window] {
        while (QLocalSocket* client = tray_server.nextPendingConnection()) {
          QObject::connect(client, &QLocalSocket::readyRead, client,
                           [client, show_window] {
                             if (client->readAll().contains(kSettingsRequest)) {
                               show_window();
                             }
                             client->close();
                           });
          QObject::connect(client, &QLocalSocket::disconnected, client,
                           &QObject::deleteLater);
        }
      });

  if (!QSystemTrayIcon::isSystemTrayAvailable()) {
    show_window();
    return QApplication::exec();
  }

  QApplication::setQuitOnLastWindowClosed(false);
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
  if (parser.isSet(settings)) {
    show_window();
  }
  return QApplication::exec();
}
