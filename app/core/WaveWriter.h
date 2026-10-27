#pragma once
#include <QFile>
#include <QMutex>
#include "Types.h"

namespace luma {
// RF64, rather than RIFF, allows separate tracks to exceed 4 GiB.
class WaveWriter {
public:
    explicit WaveWriter(const QString& path);
    ~WaveWriter();
    void append(const AudioPacket& packet,qint64 originNs);
    void finish(qint64 durationNs);
    qint64 frames() const { return frames_; }
private:
    void pad(qint64 frames);
    void write(const char* data,qint64 size);
    QFile file_;
    QMutex mutex_;
    qint64 frames_=0;
    bool finished_=false;
};
}
