#pragma once
#include <QImage>
#include <QList>
#include <QRect>
#include <QString>
#include <QtEndian>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <stdexcept>

namespace luma {
inline qint64 clockNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
enum class SourceKind { Display, Window, Region };
enum class AudioKind { Application, Microphone, System };
struct AudioMix {
    std::atomic<float> gain{1.0f};
    std::atomic<bool> muted{false};
    std::atomic<float> peak{0.0f};
};
struct Source {
    QString id, name;
    SourceKind kind = SourceKind::Display;
    QRect bounds;
    quint32 processId = 0;
};
struct AudioSource {
    QString id, name, detail;
    AudioKind kind = AudioKind::Application;
    quint32 processId = 0;
    std::shared_ptr<AudioMix> mix = std::make_shared<AudioMix>();
};
struct AudioPacket {
    QByteArray pcm; // 48 kHz, signed little-endian 16-bit stereo, interleaved
    qint64 timestampNs = 0;
};
inline void applyMix(AudioPacket &packet, AudioMix &mix) {
    const auto gain = mix.muted.load() ? 0.0f : std::clamp(mix.gain.load(), 0.0f, 2.0f);
    float peak = 0;
    for (qsizetype i = 0; i + 1 < packet.pcm.size(); i += 2) {
        const auto sample = qFromLittleEndian<qint16>(packet.pcm.constData() + i);
        const auto value = qint16(std::clamp<int>(int(sample * gain), -32768, 32767));
        qToLittleEndian(value, packet.pcm.data() + i);
        peak = std::max(peak, std::abs(float(value)) / 32768.0f);
    }
    mix.peak = peak;
}
struct Settings {
    int width = 1920, height = 1080, fps = 60, delaySeconds = 3, quality = 20;
    QString encoder = "libx264", container = "mkv", outputFolder;
    bool cursor = true;
    void validate() const {
        if (width < 64 || height < 64 || width > 16384 || height > 16384 || width % 2 || height % 2)
            throw std::runtime_error("해상도는 64~16384 사이의 짝수로 입력해 주세요.");
        if (fps < 1 || fps > 240)
            throw std::runtime_error("FPS는 1~240 사이로 입력해 주세요.");
        if (delaySeconds < 0 || delaySeconds > 60)
            throw std::runtime_error("시작 지연은 0~60초로 입력해 주세요.");
        if (container != "mkv" && container != "mp4")
            throw std::runtime_error("저장 형식을 확인해 주세요.");
        if (!QStringList{"libx264", "h264_nvenc", "h264_qsv", "h264_amf", "h264_videotoolbox"}
                 .contains(encoder))
            throw std::runtime_error("지원하지 않는 인코더입니다.");
        if (outputFolder.trimmed().isEmpty())
            throw std::runtime_error("저장 폴더를 선택해 주세요.");
    }
};
using AudioCallback = std::function<void(AudioPacket)>;
using ErrorCallback = std::function<void(QString)>;
} // namespace luma
