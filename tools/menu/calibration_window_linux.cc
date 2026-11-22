#include "tools/menu/calibration_window_linux.h"

#include <QByteArray>
#include <QHBoxLayout>
#include <QLocalSocket>
#include <QString>
#include <QVBoxLayout>
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

const QString kIntroText = QStringLiteral(
    "Calibration is optional. It teaches Facepalm where you look on this "
    "screen, which makes the feelings and reactions it reports more "
    "accurate; everything else keeps working without it.");

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
    QByteArray text = QByteArray::fromStdString(std::string(line)) + '\n';
    socket_.write(text);
    socket_.waitForBytesWritten(kWriteTimeoutMs);
  }

  std::optional<std::string> ReadLine() override {
    for (;;) {
      const int newline = pending_.indexOf('\n');
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
// (the resting pose, then each screen's centre and four corners).
QString StepPrompt(const facepalm::CalibrationFlow::View& view) {
  if (view.resting_step) {
    return QStringLiteral(
        "Sit the way you normally would and look at the centre of this "
        "screen, then click Next.");
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
  return QStringLiteral("Look at %1 of this screen, then click Next.")
      .arg(where);
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

void CalibrationWindow::Render() {
  if (!started_) {
    label_->setText(kIntroText);
    primary_->setText(QStringLiteral("Start Calibration"));
    primary_->setVisible(true);
    secondary_->setText(QStringLiteral("Cancel"));
    return;
  }
  const facepalm::CalibrationFlow::View& view = flow_->CurrentView();
  switch (view.state) {
    case facepalm::CalibrationFlow::State::kPrompting:
      label_->setText(StepPrompt(view));
      primary_->setText(QStringLiteral("Next"));
      primary_->setVisible(true);
      secondary_->setText(QStringLiteral("Cancel"));
      break;
    case facepalm::CalibrationFlow::State::kDone:
      label_->setText(
          QStringLiteral("Calibration finished. The new profile is saved."));
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
    flow_->Start(1);
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
