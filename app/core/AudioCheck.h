#pragma once
#include "Platform.h"
#include <QObject>
#include <atomic>
#include <thread>

namespace luma {
class AudioCheck : public QObject {
    Q_OBJECT
  public:
    explicit AudioCheck(QObject *parent = nullptr) : QObject(parent) {}
    ~AudioCheck() override;
    void start(Platform *, QList<AudioSource>, QString file, int milliseconds = 5000);
    void requestStop() {
        stop_ = true;
    }
    void stopAndWait() {
        requestStop();
        if (thread_.joinable())
            thread_.join();
    }
    bool active() const {
        return active_;
    }
  signals:
    void ready();
    void completed(QString file, QString error, double peak);

  private:
    std::atomic<bool> stop_{false}, active_{false};
    std::thread thread_;
};
} // namespace luma
