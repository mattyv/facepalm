#include "tools/menu/calibration_window_linux.h"

#include <QByteArray>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QList>
#include <QLocalSocket>
#include <QPoint>
#include <QRect>
#include <QScreen>
#include <QString>
#include <QVBoxLayout>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "src/calibration.h"

namespace facepalm_menu {
namespace {

constexpr int kConnectTimeoutMs = 200;
constexpr int kWriteTimeoutMs = 200;
constexpr int kReadTimeoutMs = 5000;
constexpr int kWindowSide = 420;

// A function, not a QString global: QString's constructor is not
// exception-free, which a static-duration global cannot safely rely on
// (bugprone-throwing-static-initialization).
QString IntroText() {
  return QStringLiteral(
      "Calibration is optional. It teaches Facepalm where you look on each "
      "of your screens, which makes the feelings and reactions it reports "
      "more accurate; everything else keeps working without it.");
}

// #137's fixate-and-turn step.
QString GainsStepText() {
  return QStringLiteral(
      "Look at the dot the way you normally would and slowly turn your "
      "head left, then right, keeping your eyes on it the whole time. "
      "Then click Next.");
}

// Every screen Qt reports, primary first then the rest in
// QGuiApplication::screens()'s own order -- the daemon's own screen
// index (CalibrationFlow::View::screen) is counted against this same
// order on this machine.
QList<const QScreen*> OrderedScreens() {
  const QList<QScreen*> screens = QGuiApplication::screens();
  const QScreen* primary = QGuiApplication::primaryScreen();
  QList<const QScreen*> ordered;
  if (primary != nullptr) {
    ordered.append(primary);
  }
  for (const QScreen* screen : screens) {
    if (screen != primary) {
      ordered.append(screen);
    }
  }
  return ordered;
}

// `screen`, or nullptr once `index` is past the last one Qt reports (the
// window simply stays where it was).
const QScreen* ScreenAt(int index) {
  const QList<const QScreen*> screens = OrderedScreens();
  if (index < 0 || index >= screens.size()) {
    return nullptr;
  }
  return screens.at(index);
}

// Where `screen` sits relative to the primary one (#135: "the screen the
// prompt names").
QString ScreenPosition(const QScreen* screen) {
  const QScreen* primary = QGuiApplication::primaryScreen();
  if (screen == primary || primary == nullptr) {
    return QStringLiteral("the main screen");
  }
  const int dx = screen->geometry().x() - primary->geometry().x();
  const int dy = screen->geometry().y() - primary->geometry().y();
  if (std::abs(dx) >= std::abs(dy)) {
    return dx < 0 ? QStringLiteral("the screen to the left")
                  : QStringLiteral("the screen to the right");
  }
  return dy < 0 ? QStringLiteral("the screen above")
                : QStringLiteral("the screen below");
}

// "the screen to the left (HDMI-1)", naming both the screen's position
// and Qt's own name for it (#135, #137) -- the closest a Linux display
// has to AppKit's localizedName -- or just the position for a screen
// with none.
QString ScreenDescription(const QScreen* screen) {
  QString position = ScreenPosition(screen);
  if (screen->name().isEmpty()) {
    return position;
  }
  return QStringLiteral("%1 (%2)").arg(position, screen->name());
}

// "Calibrated 1 screen." or "Calibrated 3 screens." (#135, #137): the
// wizard's own result line, rather than ending silently (#136's finding).
QString ResultText(int screens) {
  return QStringLiteral("Calibrated %1 screen%2.")
      .arg(screens)
      .arg(screens == 1 ? QStringLiteral("") : QStringLiteral("s"));
}

// The daemon's own hint on a refused step (#177), sentence-cased, with the
// retry it still invites: "Look further toward that corner, away from the
// centre. Click Next to try again." Shown in place of StepPrompt while
// view.hint is set, so the person sees why the step did not advance
// without the wizard ending over it.
QString RefusalText(const std::string& hint) {
  QString reason = QString::fromStdString(hint);
  if (!reason.isEmpty()) {
    reason = reason.left(1).toUpper() + reason.mid(1);
  }
  return QStringLiteral("%1. Click Next to try again.").arg(reason);
}

// A QLocalSocket behind src/calibrate_cli.h's CalibrateClientIo (#118),
// the tray's own equivalent of tools/posix_calibrate_client_io.h: plain
// Qt glue, untested by this repo's own convention, the same as
// QtSingleInstanceIo above main().
class QtCalibrateClientIo : public facepalm::CalibrateClientIo {
 public:
  bool Connect(const std::string& path) override {
    socket_.connectToServer(QString::fromStdString(path));
    return socket_.state() == QLocalSocket::ConnectedState ||
           socket_.waitForConnected(kConnectTimeoutMs);
  }

  void WriteLine(std::string_view line) override {
    const QByteArray text = QByteArray::fromStdString(std::string(line)) + '\n';
    socket_.write(text);
    socket_.waitForBytesWritten(kWriteTimeoutMs);
  }

  std::optional<std::string> ReadLine() override {
    for (;;) {
      const qsizetype newline = pending_.indexOf('\n');
      if (newline >= 0) {
        const QByteArray line = pending_.left(newline);
        pending_.remove(0, newline + 1);
        return line.toStdString();
      }
      if (socket_.bytesAvailable() == 0 &&
          !socket_.waitForReadyRead(kReadTimeoutMs)) {
        return std::nullopt;
      }
      pending_.append(socket_.readAll());
    }
  }

 private:
  QLocalSocket socket_;
  QByteArray pending_;
};

// The step `view` prompts, following specs/Calibration.tla's own targets
// (the resting pose, #137's gains step, then each screen's centre and
// four corners), naming the screen a point's own prompt is for (#135).
QString StepPrompt(const facepalm::CalibrationFlow::View& view) {
  if (view.resting_step) {
    return QStringLiteral(
        "Sit the way you normally would and look at the centre of the "
        "screen in front of you, then click Next.");
  }
  if (view.gains_step) {
    return GainsStepText();
  }
  QString where;
  switch (view.point) {
    case facepalm::CalibrationPoint::kCentre:
      where = QStringLiteral("the centre");
      break;
    case facepalm::CalibrationPoint::kTopLeft:
      where = QStringLiteral("the top-left corner");
      break;
    case facepalm::CalibrationPoint::kTopRight:
      where = QStringLiteral("the top-right corner");
      break;
    case facepalm::CalibrationPoint::kBottomLeft:
      where = QStringLiteral("the bottom-left corner");
      break;
    case facepalm::CalibrationPoint::kBottomRight:
      where = QStringLiteral("the bottom-right corner");
      break;
  }
  const QScreen* screen = ScreenAt(view.screen);
  const QString screen_name =
      screen != nullptr ? ScreenDescription(screen)
                        : QStringLiteral("screen %1").arg(view.screen + 1);
  return QStringLiteral(
             "Look at %1 of %2, the way you normally would, then "
             "click Next.")
      .arg(where, screen_name);
}

}  // namespace

CalibrationWindow::CalibrationWindow(QWidget* parent, std::string socket_path)
    : window_(parent),
      label_(new QLabel(&window_)),
      primary_(new QPushButton(&window_)),
      secondary_(new QPushButton(&window_)),
      socket_path_(std::move(socket_path)) {
  window_.setWindowTitle(QStringLiteral("Calibrate Facepalm"));
  window_.resize(kWindowSide, kWindowSide / 2);
  label_->setWordWrap(true);
  auto* buttons = new QHBoxLayout();
  buttons->addStretch();
  buttons->addWidget(secondary_);
  buttons->addWidget(primary_);
  auto* layout = new QVBoxLayout(&window_);
  layout->addWidget(label_);
  layout->addLayout(buttons);
  primary_->setDefault(true);
  QObject::connect(primary_, &QPushButton::clicked, &window_,
                   [this] { PrimaryClicked(); });
  QObject::connect(secondary_, &QPushButton::clicked, &window_,
                   [this] { SecondaryClicked(); });
  // Closing by the window's own close button, or Escape, emits rejected
  // (CalibrationMenu.tla's WindowClosed): the same handler as the Cancel
  // and Close buttons funnel into once they close the window themselves.
  QObject::connect(&window_, &QDialog::rejected, &window_,
                   [this] { WindowClosed(); });
}

void CalibrationWindow::Open(std::string token) {
  if (window_.isVisible()) {
    window_.raise();
    window_.activateWindow();
    return;
  }
  io_ = std::make_unique<QtCalibrateClientIo>();
  flow_ = std::make_unique<facepalm::CalibrationFlow>(socket_path_,
                                                      std::move(token), *io_);
  started_ = false;
  Render();
  window_.show();
  window_.raise();
}

void CalibrationWindow::MoveToScreenForStep(
    const facepalm::CalibrationFlow::View& view) {
  if (view.resting_step || view.gains_step) {
    return;
  }
  const QScreen* screen = ScreenAt(view.screen);
  if (screen == nullptr) {
    return;
  }
  const QRect visible = screen->availableGeometry();
  const QPoint origin(
      visible.x() + ((visible.width() - window_.width()) / 2),
      visible.y() + ((visible.height() - window_.height()) / 2));
  window_.move(origin);
}

void CalibrationWindow::Render() {
  if (!started_) {
    label_->setText(IntroText());
    primary_->setText(QStringLiteral("Start Calibration"));
    primary_->setVisible(true);
    secondary_->setText(QStringLiteral("Cancel"));
    return;
  }
  const facepalm::CalibrationFlow::View& view = flow_->CurrentView();
  switch (view.state) {
    case facepalm::CalibrationFlow::State::kPrompting:
      MoveToScreenForStep(view);
      label_->setText(view.hint.empty() ? StepPrompt(view)
                                        : RefusalText(view.hint));
      primary_->setText(QStringLiteral("Next"));
      primary_->setVisible(true);
      secondary_->setText(QStringLiteral("Cancel"));
      break;
    case facepalm::CalibrationFlow::State::kDone:
      label_->setText(QStringLiteral("%1 The new profile is saved.")
                          .arg(ResultText(view.screens)));
      primary_->setVisible(false);
      secondary_->setText(QStringLiteral("Close"));
      break;
    case facepalm::CalibrationFlow::State::kFailed:
      label_->setText(QStringLiteral("Calibration could not finish: %1")
                          .arg(QString::fromStdString(view.error)));
      primary_->setVisible(false);
      secondary_->setText(QStringLiteral("Close"));
      break;
    case facepalm::CalibrationFlow::State::kClosed:
      // A cancel the daemon confirmed: nothing left to show.
      window_.hide();
      break;
  }
}

void CalibrationWindow::PrimaryClicked() {
  if (!started_) {
    started_ = true;
    // The real screen count (#135), not a hardcoded one screen.
    flow_->Start(static_cast<int>(QGuiApplication::screens().size()));
  } else if (flow_->CurrentView().state ==
             facepalm::CalibrationFlow::State::kPrompting) {
    flow_->Next();
  }
  Render();
}

void CalibrationWindow::SecondaryClicked() {
  if (started_ && flow_->CurrentView().state ==
                      facepalm::CalibrationFlow::State::kPrompting) {
    flow_->Cancel();
    Render();
    return;
  }
  window_.hide();
}

// CalibrationMenu.tla's WindowClosed: fires however the window closed
// (window_.hide() above included, since QDialog::reject is not the only
// path here -- Qt's own close button emits rejected directly), and
// CalibrationFlow::WindowClosed is a no-op unless a step was showing, so
// calling it unconditionally is always safe.
void CalibrationWindow::WindowClosed() {
  if (flow_) {
    flow_->WindowClosed();
  }
  window_.hide();
}

}  // namespace facepalm_menu
