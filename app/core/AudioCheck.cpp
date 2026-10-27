#include "AudioCheck.h"
#include "WaveWriter.h"
#include <QMutex>
#include <QMutexLocker>
#include <vector>

namespace luma {
AudioCheck::~AudioCheck() {
    requestStop();
    if (thread_.joinable())
        thread_.join();
}
void AudioCheck::start(Platform *platform, QList<AudioSource> sources, QString path,
                       int milliseconds) {
    if (active_)
        return;
    if (thread_.joinable())
        thread_.join();
    stop_ = false;
    active_ = true;
    thread_ = std::thread([=, this]() mutable {
        std::vector<std::unique_ptr<AudioCapture>> captures;
        QMutex mutex;
        QString failure;
        std::atomic<qint64> origin{0};
        const qint64 maximumFrames = qint64(milliseconds) * 48;
        std::vector<qint32> mixed(size_t(maximumFrames) * 2, 0);
        qint64 usedFrames = 0;
        double maxPeak = 0;
        try {
            if (sources.isEmpty())
                throw std::runtime_error("테스트할 마이크나 앱 소리를 선택해 주세요.");
            for (const auto &source : sources) {
                captures.push_back(platform->createAudioCapture());
                captures.back()->start(
                    source,
                    [&, mix = source.mix](AudioPacket packet) {
                        applyMix(packet, *mix);
                        const auto base = origin.load();
                        if (!base)
                            return;
                        const auto position = (packet.timestampNs - base) * 48000 / 1000000000;
                        QMutexLocker lock(&mutex);
                        maxPeak = std::max(maxPeak, double(mix->peak.load()));
                        for (qint64 i = 0; i < packet.pcm.size() / 4; i++) {
                            const auto frame = position + i;
                            if (frame < 0 || frame >= maximumFrames)
                                continue;
                            for (int channel = 0; channel < 2; channel++)
                                mixed[size_t(frame) * 2 + channel] += qFromLittleEndian<qint16>(
                                    packet.pcm.constData() + i * 4 + channel * 2);
                            usedFrames = std::max(usedFrames, frame + 1);
                        }
                    },
                    [&](QString error) {
                        QMutexLocker lock(&mutex);
                        failure = error;
                    });
                if (stop_)
                    break;
            }
            origin = clockNs();
            emit ready();
            while (!stop_ && clockNs() - origin < qint64(milliseconds) * 1000000) {
                {
                    QMutexLocker lock(&mutex);
                    if (!failure.isEmpty())
                        break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            for (auto &capture : captures)
                capture->stop();
            if (!failure.isEmpty())
                throw std::runtime_error(failure.toStdString());
            const auto length =
                std::clamp<qint64>((clockNs() - origin) * 48000 / 1000000000, 0, maximumFrames);
            usedFrames = std::max(usedFrames, length);
            if (usedFrames > 0) {
                QByteArray pcm(usedFrames * 4, 0);
                for (qint64 i = 0; i < usedFrames * 2; i++)
                    qToLittleEndian<qint16>(
                        qint16(std::clamp<qint32>(mixed[size_t(i)], -32768, 32767)),
                        pcm.data() + i * 2);
                WaveWriter writer(path);
                writer.append({pcm, origin}, origin);
                writer.finish(usedFrames * 1000000000 / 48000);
            } else
                path.clear();
            active_ = false;
            emit completed(path, {}, maxPeak);
        } catch (const std::exception &error) {
            for (auto &capture : captures)
                capture->stop();
            active_ = false;
            emit completed({}, QString::fromUtf8(error.what()), 0);
        }
    });
}
} // namespace luma
