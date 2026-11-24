#ifndef FACEPALM_TOOLS_MENU_SERVICE_HEALTH_LINUX_H_
#define FACEPALM_TOOLS_MENU_SERVICE_HEALTH_LINUX_H_

#include <QCoreApplication>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QStringList>

#include "src/menu_state.h"

namespace facepalm_menu {

// A timed-out child remains owned by the application until it exits; a
// second query never starts while that child is still running.
class ServiceHealthQuery {
 public:
  [[nodiscard]] bool Pending() const {
    return pending_ && pending_->state() != QProcess::NotRunning;
  }

  facepalm::ServiceHealth Read(const QString& program, const QStringList& args,
                               int timeout_ms) {
    if (Pending()) return facepalm::ServiceHealth::kUnavailable;
    auto* process = new QProcess(QCoreApplication::instance());
    pending_ = process;
    QObject::connect(
        process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        process, &QObject::deleteLater);
    process->start(program, args);
    if (!process->waitForFinished(timeout_ms)) {
      if (process->state() == QProcess::NotRunning) {
        process->deleteLater();
      } else {
        process->kill();
      }
      return facepalm::ServiceHealth::kUnavailable;
    }
    if (process->exitStatus() != QProcess::NormalExit ||
        process->exitCode() != 0)
      return facepalm::ServiceHealth::kUnavailable;
    return facepalm::ParseServiceHealth(
        process->readAllStandardOutput().toStdString());
  }

 private:
  QPointer<QProcess> pending_;
};

}  // namespace facepalm_menu
#endif  // FACEPALM_TOOLS_MENU_SERVICE_HEALTH_LINUX_H_
