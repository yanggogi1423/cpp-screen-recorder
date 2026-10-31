#pragma once
#include <QByteArray>
#include <QProcess>
#include <QString>
#include <functional>

namespace luma {
// FFmpeg emits microseconds even for the historically named out_time_ms key.
class FinalizeProgress {
    QByteArray pending_;
    qint64 timestamp_ = 0;
  public:
    bool feed(const QByteArray &bytes);
    int percent(qint64 durationNs) const;
};
// Bounds inactivity, not total runtime: a long recording may take many minutes.
void finishMediaProcess(QProcess &process, const QString &output, const QString &log,
                        qint64 durationNs, const std::function<void(int)> &progress,
                        int inactivityMs = 300000);
} // namespace luma
