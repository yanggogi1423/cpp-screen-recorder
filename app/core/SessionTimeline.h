#pragma once
#include "Types.h"
#include <mutex>
#include <vector>

namespace luma {
// All video pacing and audio packet projection use this same monotonic timeline.
// A pause removes real time from both streams, rather than inserting frozen video/silence.
class SessionTimeline {
  public:
    void reset(qint64 origin) {
        std::lock_guard lock(mutex_);
        origin_ = origin;
        pauses_.clear();
    }
    void setPaused(bool paused, qint64 now = clockNs()) {
        std::lock_guard lock(mutex_);
        const bool current = !pauses_.empty() && pauses_.back().end < 0;
        if (paused == current)
            return;
        if (paused)
            pauses_.push_back({std::max(origin_, now), -1});
        else
            pauses_.back().end = std::max(pauses_.back().start, now);
    }
    bool paused() const {
        std::lock_guard lock(mutex_);
        return !pauses_.empty() && pauses_.back().end < 0;
    }
    qint64 elapsed(qint64 now = clockNs()) const {
        std::lock_guard lock(mutex_);
        return std::max<qint64>(0, now - origin_ - removedBefore(now));
    }
    qint64 pausedDuration(qint64 now = clockNs()) const {
        std::lock_guard lock(mutex_);
        return removedBefore(now);
    }
    QList<AudioPacket> project(const AudioPacket &packet) const {
        std::lock_guard lock(mutex_);
        QList<AudioPacket> result;
        const qint64 start = packet.timestampNs;
        const qint64 count = packet.pcm.size() / 4;
        const qint64 end = start + count * 1000000000 / 48000;
        auto append = [&](qint64 a, qint64 b) {
            if (b <= a)
                return;
            auto index = [&](qint64 t) {
                return std::clamp<qint64>(((t - start) * 48000 + 999999999) / 1000000000, 0, count);
            };
            const auto first = index(a), last = index(b);
            if (last <= first)
                return;
            const auto sampleTime = start + first * 1000000000 / 48000;
            result.append({packet.pcm.mid(first * 4, (last - first) * 4),
                           sampleTime - removedBefore(sampleTime)});
        };
        qint64 cursor = start;
        for (const auto &pause : pauses_) {
            const auto finish = pause.end < 0 ? end : pause.end;
            if (finish <= cursor)
                continue;
            if (pause.start >= end)
                break;
            append(cursor, std::min(end, pause.start));
            cursor = std::max(cursor, finish);
            if (cursor >= end)
                return result;
        }
        append(cursor, end);
        return result;
    }

  private:
    struct Pause {
        qint64 start, end;
    };
    qint64 removedBefore(qint64 when) const {
        qint64 total = 0;
        for (const auto &p : pauses_) {
            if (when <= p.start)
                break;
            total += std::max<qint64>(0, std::min(when, p.end < 0 ? when : p.end) - p.start);
        }
        return total;
    }
    mutable std::mutex mutex_;
    qint64 origin_ = 0;
    std::vector<Pause> pauses_;
};
} // namespace luma
