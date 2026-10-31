#include "FinalizeProcess.h"
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <algorithm>
#include <stdexcept>

namespace luma {
bool FinalizeProgress::feed(const QByteArray &bytes) {
    pending_ += bytes;
    bool advanced = false;
    qsizetype end;
    while ((end = pending_.indexOf('\n')) >= 0) {
        const auto line = pending_.left(end).trimmed();
        pending_.remove(0, end + 1);
        if (line.startsWith("out_time_us=") || line.startsWith("out_time_ms=")) {
            bool ok = false;
            const auto time = line.mid(line.indexOf('=') + 1).toLongLong(&ok);
            if (ok && time > timestamp_) {
                timestamp_ = time;
                advanced = true;
            }
        }
    }
    // Do not retain unbounded malformed child output.
    if (pending_.size() > 65536)
        pending_.clear();
    return advanced;
}
int FinalizeProgress::percent(qint64 durationNs) const {
    if (durationNs <= 0)
        return 0;
    // Reserve 100% for successful process exit, including MP4 faststart.
    return int(std::clamp(timestamp_ * 100.0L * 1000 / durationNs, 0.0L, 99.0L));
}
void finishMediaProcess(QProcess &process, const QString &output, const QString &logPath,
                        qint64 durationNs, const std::function<void(int)> &progress,
                        int inactivityMs) {
    QFile log(logPath);
    log.open(QIODevice::WriteOnly);
    FinalizeProgress parser;
    QElapsedTimer activity;
    activity.start();
    qint64 size = -1, modified = -1;
    int reported = -1;
    process.setReadChannel(QProcess::StandardOutput);
    auto drain = [&] {
        if (parser.feed(process.readAllStandardOutput()))
            activity.restart();
        const auto diagnostics = process.readAllStandardError();
        log.write(diagnostics);
        log.flush();
        // faststart moves the index after media timestamps have reached the end.
        QFileInfo file(output);
        const auto currentSize = file.exists() ? file.size() : -1;
        const auto currentModified = file.exists() ? file.lastModified().toMSecsSinceEpoch() : -1;
        if (currentSize != size || currentModified != modified) {
            size = currentSize;
            modified = currentModified;
            activity.restart();
        }
        const auto value = parser.percent(durationNs);
        if (value != reported) {
            reported = value;
            progress(value);
        }
    };
    while (process.state() != QProcess::NotRunning) {
        process.waitForReadyRead(200);
        drain();
        if (activity.elapsed() >= inactivityMs && process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(5000);
            drain();
            throw std::runtime_error("파일 정리 작업이 5분간 진행되지 않아 중단했습니다. 복구 원본은 보존했습니다.");
        }
    }
    drain();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        throw std::runtime_error("영상·소리 결합에 실패했습니다. 복구 원본과 mux.log를 보존했습니다.");
    progress(100);
}
} // namespace luma
