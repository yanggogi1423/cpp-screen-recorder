#pragma once
#include "Platform.h"
#include "SessionTimeline.h"
#include <QObject>
#include <atomic>
#include <thread>

namespace luma {
class RecordingSession : public QObject {
    Q_OBJECT
  public:
    explicit RecordingSession(QObject *parent = nullptr) : QObject(parent) {}
    ~RecordingSession() override;
    void start(Platform *platform, VideoCapture *video, const Settings &settings,
               const QList<AudioSource> &audio);
    void requestStop() {
        timeline_.finish();
        stop_ = true;
    }
    qint64 frames() const {
        return frames_.load();
    }
    qint64 estimatedBytes() const {
        return estimatedBytes_.load();
    }
    void setPaused(bool value) {
        timeline_.setPaused(value);
    }
    bool paused() const {
        return timeline_.paused();
    }
    static QStringList probeArguments(const Settings &);
    static QString ffmpegPath();
    static QStringList videoArguments(const Settings &, QSize, const QString &);
  signals:
    void started(QString folder);
    void finished(QString file, QString warning);
    void failed(QString message, QString recoveryFolder);
    void finalizing();
    void finalizationProgress(QString stage, int percent);

  private:
    void run(Platform *, VideoCapture *, Settings, QList<AudioSource>);
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<qint64> frames_{0};
    std::atomic<qint64> estimatedBytes_{0};
    SessionTimeline timeline_;
};
} // namespace luma
