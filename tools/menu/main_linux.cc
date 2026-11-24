#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDevice>
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
#include <QPixmap>
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
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "src/calibrate_client_token.h"
#include "src/cameras_listing.h"
#include "src/client_approvals.h"
#include "src/client_token.h"
#include "src/daemon_status.h"
#include "src/menu_field_format.h"
#include "src/menu_state.h"
#include "src/settings_config_path.h"
#include "src/settings_document.h"
#include "src/settings_save_guard.h"
#include "src/single_instance.h"
#include "src/user_paths.h"
#include "src/user_service.h"
#include "tools/menu/calibration_window_linux.h"

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

// The camera preview's own paths (#37 item 8, #108): creating
// `preview_request` is the menu's own ask (specs/PreviewLease.tla's
// StartPreview), removing it asks the daemon to stop; `preview_frame`
// is the latest frame, rewritten atomically, so a half-written file is
// never read.
QString PreviewRequestPath() {
  const std::optional<facepalm::UserPaths> paths = facepalm::PathsFor(
      QDir::homePath().toStdString(), facepalm::Platform::kLinux);
  return paths ? QString::fromStdString(paths->preview_request) : QString();
}

QString PreviewFramePath() {
  const std::optional<facepalm::UserPaths> paths = facepalm::PathsFor(
      QDir::homePath().toStdString(), facepalm::Platform::kLinux);
  return paths ? QString::fromStdString(paths->preview_frame) : QString();
}

// `preview_readings`'s own text (#134): the readings as they happen and
// the latest beat's feeling, beside `preview_frame` under the same
// rules.
QString PreviewReadingsPath() {
  const std::optional<facepalm::UserPaths> paths = facepalm::PathsFor(
      QDir::homePath().toStdString(), facepalm::Platform::kLinux);
  return paths ? QString::fromStdString(paths->preview_readings) : QString();
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

// facepalm.conf's own path (src/settings_config_path.h, #64).
QString ConfigPath() {
  const QString config = qEnvironmentVariable("XDG_CONFIG_HOME");
  return QString::fromStdString(facepalm::SettingsConfigPath(
      config.toStdString(), QDir::homePath().toStdString()));
}

// A plain value, not a class with an invariant to protect.
struct ConfigSnapshot {
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes)
  QByteArray bytes;
  // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes)
  bool missing = false;
};

std::optional<ConfigSnapshot> ReadConfig(const QString& path, QString& error) {
  constexpr qint64 kMaxConfigBytes =
      static_cast<qint64>(facepalm::kMaxSettingsFileBytes);
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
  if (facepalm::SettingsFileTooLarge(static_cast<std::size_t>(bytes.size())) ||
      decoded.contains(QChar::Null) || decoder.hasError()) {
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

// Opens the calibration wizard (#118, item 12 of #61): approves the
// "calibrate" client the same way SwitchService approves its own,
// through ClientApprovals (which calibrate_cli already drives this way),
// fresh on each open rather than once at launch, then hands
// CalibrationWindow the token it resolved to open with.
void OpenCalibration(facepalm_menu::CalibrationWindow& calibration,
                     QWidget* parent) {
  const std::optional<facepalm::UserPaths> paths = facepalm::PathsFor(
      QDir::homePath().toStdString(), facepalm::Platform::kLinux);
  if (!paths) {
    QMessageBox::warning(
        parent, QStringLiteral("Calibration could not start"),
        QStringLiteral("Your home folder could not be found."));
    return;
  }
  const std::unique_ptr<facepalm::ApprovalsIo> approvals_io =
      facepalm::PosixApprovalsIo();
  facepalm::ClientApprovals approvals(
      paths->clients,
      [](const std::string& path) { return facepalm::LoadClientToken(path); },
      *approvals_io);
  const facepalm::CalibrateClientToken result = facepalm::ResolveCalibrateToken(
      approvals,
      [](const std::string& path) { return facepalm::LoadClientToken(path); });
  if (!result.token) {
    QMessageBox::warning(parent, QStringLiteral("Calibration could not start"),
                         QString::fromStdString(result.error));
    return;
  }
  calibration.Open(*result.token);
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
  return {};
}

// The cameras facepalm-daemon's own `cameras` command lists (item 10 of
// #61, #13), parsed by facepalm::ParseCamerasListing (#120), the pure
// entry point extracted from this function and tools/menu/main.mm's own
// ListCameras so each parses the daemon's tab-separated stdout the same
// way, unit-tested and fuzzed on its own. Run synchronously, since opening
// Settings already waits on reading the settings file; empty, with no
// error shown, when the daemon cannot be found or does not run, so the
// picker then offers only the default.
std::vector<facepalm::CamerasListingEntry> ListCameras() {
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
  const QByteArray text = process.readAllStandardOutput();
  return facepalm::ParseCamerasListing(std::string_view(
      text.constData(), static_cast<std::size_t>(text.size())));
}

// facepalm::FormatFloat (src/menu_field_format.h, #64), wrapped for a
// Settings field.
QString Number(float value) {
  return QString::fromStdString(facepalm::FormatFloat(value));
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
  // The first kAdvancedFieldCount entries are the expression tuning grid,
  // including the squint-guard's own two thresholds (#140, #150); the
  // rest are looking away's and screen context's own numeric fields
  // (#35, #61 item 11, #79).
  static constexpr std::size_t kAdvancedFieldCount = 9;
  static constexpr int kWindowWidth = 560;
  static constexpr int kWindowHeight = 610 + 64;
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
      "Pose settle (0–5000 ms)",    "Eye look-down threshold (0–1)",
      "Eye blink threshold (0–1)",  "Degrees (0–180)",
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
      &facepalm::MenuSettings::expression_eye_look_down_threshold,
      &facepalm::MenuSettings::expression_eye_blink_threshold,
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
    for (const facepalm::CamerasListingEntry& device : ListCameras()) {
      const QString id = QString::fromStdString(device.id);
      const QString name = QString::fromStdString(device.name);
      camera_->addItem(
          device.built_in ? name + QStringLiteral(" (built-in)") : name, id);
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
        Number(config.expressions_eye_look_down_threshold),
        Number(config.expressions_eye_blink_threshold),
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
    if (!facepalm::SettingsFileUnchanged(
            std::string_view(snapshot_->bytes.constData(),
                             snapshot_->bytes.size()),
            snapshot_->missing,
            std::string_view(current->bytes.constData(), current->bytes.size()),
            current->missing)) {
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

// The camera preview (#37 item 8, #108): user-started, local-only, with
// the open window itself the indicator while it runs. Implements
// specs/PreviewLease.tla's StartPreview, StopPreview and Poll from the
// menu's side; the daemon only watches for the request file.
class CameraPreview {
 public:
  explicit CameraPreview(QWidget* parent)
      : window_(parent),
        label_(new QLabel(&window_)),
        status_(new QLabel(&window_)) {
    constexpr int kSide = 480;
    window_.setWindowTitle(QStringLiteral("Camera Preview"));
    window_.resize(kSide, kSide);
    auto* layout = new QVBoxLayout(&window_);
    label_->setAlignment(Qt::AlignCenter);
    layout->addWidget(label_);
    // The readings as they happen, and the latest beat's feeling
    // (#134): a diagnostic only, so plain, word-wrapped text below the
    // image, not styled as a control.
    status_->setAlignment(Qt::AlignCenter);
    status_->setWordWrap(true);
    layout->addWidget(status_);
    // Closing by the window's own close button emits rejected, so both
    // ways of closing end the preview the same way.
    QObject::connect(&window_, &QDialog::rejected, &window_,
                     [this] { Stop(); });
    QObject::connect(&timer_, &QTimer::timeout, &window_,
                     [this] { Refresh(); });
  }

  // Starts the preview if it is hidden, and stops it if it is showing.
  void Toggle() {
    if (window_.isVisible()) {
      Stop();
    } else {
      Start();
    }
  }

  [[nodiscard]] bool IsOpen() const { return window_.isVisible(); }

  // The request file, if any -- the only signal the daemon needs to stop
  // publishing (#108, #114). Safe to call whether or not the preview is
  // running: at launch, and on QCoreApplication::aboutToQuit, nothing
  // else here may have run yet, or ever run, this time around.
  void RemoveRequest() { QFile::remove(request_path_); }

 private:
  // Creates or touches the request file's directory and modified time
  // (#114, specs/PreviewLease.tla's Fresh): the daemon reads the request
  // as asking only while it was touched recently, so this is also what
  // keeps a still-open window's ask from ever going stale on its own.
  // The directory is put at exactly 0700 regardless of umask, the same
  // way src/client_approvals.cc's MakePrivateDirectory does for the
  // daemon's own side of this directory.
  bool Touch() {
    const QString directory = QFileInfo(request_path_).absolutePath();
    if (!QDir().mkpath(directory) ||
        !QFile::setPermissions(directory, QFileDevice::ReadOwner |
                                              QFileDevice::WriteOwner |
                                              QFileDevice::ExeOwner)) {
      return false;
    }
    QFile request(request_path_);
    return request.open(QIODevice::WriteOnly);
  }

  // PreviewLease!StartPreview (#114, #176): creating the request file is
  // a camera demand of its own now (src/camera_demand.h), the same way
  // an approved client subscribing already is, so the daemon admits the
  // camera for this alone rather than refusing unless something else
  // already has it on.
  void Start() {
    if (!Touch()) {
      Warn(QStringLiteral("Could not create the preview request."));
      return;
    }
    misses_ = 0;
    Refresh();
    window_.show();
    window_.raise();
    constexpr int kRefreshMs = 500;
    timer_.start(kRefreshMs);
  }

  // PreviewLease!StopPreview: removing the request file is the only
  // signal the daemon needs; any frame it already wrote is removed in
  // turn, through PreviewLease's own streaming_changed hook (#129), by
  // the daemon's very next per-frame check, not left behind for the
  // next preview to overwrite.
  void Stop() {
    timer_.stop();
    RemoveRequest();
    window_.hide();
    status_->clear();
  }

  // PreviewLease!Poll: also stops the preview once the camera has
  // genuinely stayed off for kCameraGraceMisses refreshes in a row,
  // since the daemon stops publishing then but the window would
  // otherwise keep showing the last frame. The grace period (#176) is
  // what keeps a preview that just asked for the camera itself from
  // reading its own daemon's admission catching up as abandonment and
  // closing itself before it was ever shown anything: Start no longer
  // waits for the camera to already be on first. Also touches the
  // request file (#114) so the daemon keeps seeing it as fresh for as
  // long as this window stays open; a touch that fails is left for the
  // next one, same as a dropped frame, rather than treated as a reason
  // to stop.
  void Refresh() {
    Touch();
    if (!facepalm::ReadCameraStatus(StatusPath().toStdString())) {
      // How many refreshes in a row, at kRefreshMs apart, may find the
      // camera not yet in use before giving up: long enough for the
      // daemon's own camera-demand poll (tools/daemon/main.cc's
      // kPreviewDemandPollInterval, 0.5 s) and its admission and status
      // publish to catch up with a request this window itself just
      // made, short enough that a camera genuinely lost for an
      // unrelated reason still closes the window promptly.
      constexpr int kCameraGraceMisses = 4;
      if (++misses_ >= kCameraGraceMisses) {
        window_.reject();
      }
      return;
    }
    misses_ = 0;
    const QPixmap pixmap(frame_path_);
    if (!pixmap.isNull()) {
      label_->setPixmap(pixmap.scaled(label_->size(), Qt::KeepAspectRatio,
                                      Qt::SmoothTransformation));
    }
    RefreshReadings();
  }

  // The readings as they happen, and the latest beat's feeling (#134):
  // read and shown the same way as the frame, so it is blank once the
  // file is gone (not streaming, or nothing has happened yet) rather
  // than left showing whatever it said last. Its own method, apart from
  // Refresh, keeps that one's cognitive complexity down (#64).
  void RefreshReadings() {
    QFile file(readings_path_);
    status_->setText(file.open(QIODevice::ReadOnly | QIODevice::Text)
                         ? QString::fromUtf8(file.readAll())
                         : QString());
  }

  void Warn(const QString& text) {
    QMessageBox::warning(
        &window_, QStringLiteral("Camera preview could not start"), text);
  }

  QDialog window_;
  QLabel* label_;
  QLabel* status_;
  QTimer timer_;
  // How many Refresh calls in a row have found the camera not yet in
  // use since this window last asked for the preview (#176).
  int misses_ = 0;
  const QString request_path_ = PreviewRequestPath();
  const QString frame_path_ = PreviewFramePath();
  const QString readings_path_ = PreviewReadingsPath();
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
  menu.addSeparator();
  const QAction* show = menu.addAction(QStringLiteral("Settings…"));
  const auto show_window = [&window] { window.Open(); };
  QObject::connect(show, &QAction::triggered, &window, show_window);

  QAction* preview_action =
      menu.addAction(QStringLiteral("Show Camera Preview"));
  CameraPreview preview(&window);

  // The calibration wizard (#118, item 12 of #61): optional, so it is a
  // menu entry a person chooses, never run on their behalf.
  QAction* calibrate_action = menu.addAction(QStringLiteral("Calibrate…"));
  facepalm_menu::CalibrationWindow calibration(&window,
                                               SocketPath().toStdString());

  // Decides camera_status's, power's, preview_action's and
  // calibrate_action's title and enabled state from src/menu_state.h's
  // DecideMenuState, the one table both this tray and tools/menu/
  // main.mm's menu read (docs/local-app.md, #176), so Facepalm being
  // off is never the silent cause of a preview or a calibration wizard
  // that can never reach it.
  const QString status_path = StatusPath();
  const auto refresh_menu_state = [=, &preview] {
    const bool service_on = ServiceIsOn();
    const facepalm::MenuState state = facepalm::DecideMenuState(
        {.service_on = service_on,
         .camera_in_use = facepalm::ReadCameraStatus(status_path.toStdString()),
         .preview_open = preview.IsOpen()});
    power->setText(service_on ? QStringLiteral("Turn Off Facepalm")
                              : QStringLiteral("Turn On Facepalm…"));
    camera_status->setText(QString::fromStdString(state.camera_status_title));
    preview_action->setText(QString::fromStdString(state.preview_title));
    preview_action->setEnabled(state.preview_enabled);
    calibrate_action->setText(QString::fromStdString(state.calibrate_title));
    calibrate_action->setEnabled(state.calibrate_enabled);
  };
  refresh_menu_state();
  QObject::connect(&menu, &QMenu::aboutToShow, &window, refresh_menu_state);

  // Turning on is the person's consent, so it asks first.
  QObject::connect(
      power, &QAction::triggered, &window, [&window, refresh_menu_state] {
        const bool on = ServiceIsOn();
        if (!on &&
            QMessageBox::question(
                &window, QStringLiteral("Turn on Facepalm?"),
                QStringLiteral(
                    "Facepalm will use your camera while an app you "
                    "approved, such as Claude, asks how you react, and it "
                    "starts again each time you log in until you turn it "
                    "off. Frames stay on this computer.")) !=
                QMessageBox::Yes) {
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
        refresh_menu_state();
      });

  QObject::connect(preview_action, &QAction::triggered, &window,
                   [&preview, refresh_menu_state] {
                     preview.Toggle();
                     refresh_menu_state();
                   });
  // A request file an earlier launch left behind -- it crashed, or its
  // session logged out, while the preview was open (#114) -- is removed
  // outright: the daemon's own freshness check would stop streaming to
  // it soon anyway, but this launch owns no window for it to show
  // regardless, so there is nothing to wait for. The tray quitting --
  // by its own Quit action, or the session logging out -- removes it
  // the same way an explicit stop does, so streaming ends with this
  // tray rather than being left for that check to notice later; a crash
  // skips this connection entirely, which is exactly the case the check
  // exists for.
  preview.RemoveRequest();
  QObject::connect(&app, &QCoreApplication::aboutToQuit, &window,
                   [&preview] { preview.RemoveRequest(); });

  QObject::connect(
      calibrate_action, &QAction::triggered, &window,
      [&calibration, &window] { OpenCalibration(calibration, &window); });

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

  // The camera-in-use indicator (#37 item 8), and the preview and
  // calibrate items alongside it, refresh on their own faster timer so
  // they settle within the two seconds the indicator is asked to,
  // independent of the slower daemon-reachability check above.
  constexpr int kCameraStatusIntervalMs = 1000;
  QTimer camera_status_timer;
  QObject::connect(&camera_status_timer, &QTimer::timeout, &window,
                   refresh_menu_state);
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
