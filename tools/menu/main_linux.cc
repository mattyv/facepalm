#include <QAction>
#include <QApplication>
#include <QCheckBox>
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
#include <QLocalSocket>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QStringDecoder>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QVBoxLayout>
#include <array>
#include <charconv>
#include <optional>
#include <string>

#include "src/settings_document.h"

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
  // The daemon's fixed per-user socket (src/user_paths.h).
  return QDir::homePath() +
         QStringLiteral("/.local/state/facepalm/service.sock");
}

bool ServiceReady(const QString& path) {
  QLocalSocket socket;
  socket.connectToServer(path);
  return socket.state() == QLocalSocket::ConnectedState ||
         socket.waitForConnected(50);
}

QString ConfigPath() {
  const QString config = qEnvironmentVariable("XDG_CONFIG_HOME");
  return QDir(config.isEmpty() ? QDir::homePath() + "/.config" : config)
      .filePath(QStringLiteral("facepalm/facepalm.conf"));
}

struct ConfigSnapshot {
  QByteArray bytes;
  bool missing = false;

  bool operator==(const ConfigSnapshot&) const = default;
};

std::optional<ConfigSnapshot> ReadConfig(const QString& path, QString& error) {
  constexpr qint64 kMaxConfigBytes = 1024 * 1024;
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

QString Number(float value) {
  char buffer[64];
  const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
  return QString::fromLatin1(buffer, result.ptr - buffer);
}

class SettingsDialog : public QDialog {
 public:
  SettingsDialog() {
    setWindowTitle(QStringLiteral("Facepalm Settings"));
    resize(560, 610);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(12);

    auto* note = new QLabel(
        QStringLiteral("Start the daemon with --config and this file to apply "
                       "these settings."),
        this);
    note->setWordWrap(true);
    layout->addWidget(note);
    layout->addSpacing(14);

    auto* basic = new QGridLayout;
    ConfigureForm(*basic);
    fps_ = new QSpinBox(this);
    fps_->setRange(1, 60);
    AddField(*basic, 0, QStringLiteral("Frames per second (1–60)"), *fps_);
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
    for (std::size_t i = 0; i < fields_.size(); ++i) {
      fields_[i] = new QLineEdit(this);
      AddField(*advanced, static_cast<int>(i),
               QString::fromUtf8(kFieldLabels[i]), *fields_[i]);
    }
    layout->addLayout(advanced);
    layout->addStretch();

    auto* path_label = new QLabel(path_, this);
    path_label->setTextFormat(Qt::PlainText);
    path_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    path_label->setWordWrap(true);
    layout->addWidget(path_label);
    layout->addSpacing(10);
    save_ = new QPushButton(QStringLiteral("Save Settings"), this);
    save_->setDefault(true);
    save_->setMinimumSize(136, 28);
    layout->addWidget(save_, 0, Qt::AlignRight);
    connect(save_, &QPushButton::clicked, this, [this] { Save(); });
  }

  void Open() {
    if (!isVisible()) {
      Reload();
    }
    show();
    raise();
    activateWindow();
  }

 private:
  static constexpr std::array kFieldLabels{
      "Gesture confidence (0–1)",   "Expression smallest rise (0–1)",
      "Expression spreads (0–100)", "Expression hold (0–5000 ms)",
      "Pose degrees (0–180)",       "Pose range (0–180)",
      "Pose settle (0–5000 ms)",
  };

  static constexpr std::array kSettingMembers{
      &facepalm::MenuSettings::gesture_confidence,
      &facepalm::MenuSettings::expression_rise,
      &facepalm::MenuSettings::expression_spreads,
      &facepalm::MenuSettings::expression_hold_ms,
      &facepalm::MenuSettings::expression_pose_degrees,
      &facepalm::MenuSettings::expression_pose_range,
      &facepalm::MenuSettings::expression_settle_ms,
  };

  static void ConfigureForm(QGridLayout& form) {
    form.setContentsMargins(0, 0, 26, 0);
    form.setColumnStretch(0, 1);
    form.setVerticalSpacing(8);
  }

  template <typename Field>
  void AddField(QGridLayout& form, int row, const QString& title,
                Field& field) {
    auto* label = new QLabel(title, this);
    label->setBuddy(&field);
    field.setAccessibleName(title);
    field.setFixedWidth(110);
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
    gestures_->setChecked(config.gestures_enabled);
    expressions_->setChecked(config.expressions_enabled);
    const std::array values{
        Number(config.gesture_confidence_threshold),
        Number(config.expressions_smallest_rise),
        Number(config.expressions_spreads),
        QString::number(config.expressions_hold_ms),
        Number(config.expressions_pose_degrees),
        Number(config.expressions_pose_range),
        QString::number(config.expressions_settle_ms),
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
    settings.gestures_enabled = gestures_->isChecked();
    settings.expressions_enabled = expressions_->isChecked();
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
  QCheckBox* gestures_;
  QCheckBox* expressions_;
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

  const QIcon icon = FacepalmIcon();
  app.setWindowIcon(icon);
  SettingsDialog window;

  QMenu menu;
  QAction* services = menu.addAction(QString());
  services->setEnabled(false);
  QAction* tools = menu.addAction(QString::fromUtf8(kReplayStatus));
  tools->setEnabled(false);
  menu.addSeparator();
  QAction* show = menu.addAction(QStringLiteral("Settings…"));
  const auto show_window = [&window] { window.Open(); };
  QObject::connect(show, &QAction::triggered, &window, show_window);
  QAction* quit_action = menu.addAction(QStringLiteral("Quit"));
  QObject::connect(quit_action, &QAction::triggered, &app, &QApplication::quit);

  const QString socket_path = SocketPath();
  const auto refresh = [&] {
    const bool ready = ServiceReady(socket_path);
    services->setText(QString::fromUtf8(ready ? kCameraReady : kCameraOffline));
  };
  refresh();
  QTimer refresh_timer;
  QObject::connect(&refresh_timer, &QTimer::timeout, &window, refresh);
  refresh_timer.start(3000);

  if (!QSystemTrayIcon::isSystemTrayAvailable()) {
    show_window();
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
  if (parser.isSet(settings)) {
    show_window();
  }
  return app.exec();
}
