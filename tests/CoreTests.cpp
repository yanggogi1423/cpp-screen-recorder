#include "core/RecordingSession.h"
#include "core/WaveWriter.h"
#include "core/AudioCheck.h"
#include "core/SessionTimeline.h"
#include <QCoreApplication>
#include <QDataStream>
#include <QFile>
#include <QTemporaryDir>
#include <QEventLoop>
#include <QTimer>
#include <iostream>

void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
class TestAudio : public luma::AudioCapture {
    std::thread thread_;
    std::atomic<bool> stopped_{false};

  public:
    ~TestAudio() override {
        stop();
    }
    void start(const luma::AudioSource &, luma::AudioCallback callback,
               luma::ErrorCallback) override {
        thread_ = std::thread([=, this] {
            while (!stopped_) {
                QByteArray pcm(480 * 4, 0);
                for (int i = 0; i < 960; i++)
                    qToLittleEndian<qint16>(4000, pcm.data() + i * 2);
                callback({pcm, luma::clockNs()});
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
    }
    void stop() override {
        stopped_ = true;
        if (thread_.joinable())
            thread_.join();
    }
};
class TestPlatform : public luma::Platform {
  public:
    QString name() const override {
        return "fixture";
    }
    QList<luma::Source> sources() override {
        return {};
    }
    QList<luma::AudioSource> audioSources() override {
        return {};
    }
    std::unique_ptr<luma::VideoCapture> createVideoCapture() override {
        return {};
    }
    std::unique_ptr<luma::AudioCapture> createAudioCapture() override {
        return std::make_unique<TestAudio>();
    }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        luma::Settings s;
        s.outputFolder = "test";
        s.validate();
        for (const auto fps : {0, 241}) {
            auto invalid = s;
            invalid.fps = fps;
            bool threw = false;
            try {
                invalid.validate();
            } catch (...) {
                threw = true;
            }
            require(threw, "invalid FPS accepted");
        }
        s.width = 1919;
        bool rejected = false;
        try {
            s.validate();
        } catch (...) {
            rejected = true;
        }
        require(rejected, "odd width accepted");
        s.width = 1920;
        const auto args =
            luma::RecordingSession::videoArguments(s, QSize(2560, 1440), "file with space.mkv");
        require(args.contains("2560x1440") && args.last() == "file with space.mkv",
                "video input/output argument mismatch");
        luma::AudioMix mix;
        luma::AudioPacket packet{QByteArray(8, 0), 0};
        qToLittleEndian<qint16>(10000, packet.pcm.data());
        mix.gain = 0.5f;
        luma::applyMix(packet, mix);
        require(qFromLittleEndian<qint16>(packet.pcm.constData()) == 5000, "live gain failed");
        mix.muted = true;
        luma::applyMix(packet, mix);
        require(packet.pcm == QByteArray(8, 0), "live mute failed");
        mix.muted = false;
        mix.gain = 2.0f;
        qToLittleEndian<qint16>(30000, packet.pcm.data());
        luma::applyMix(packet, mix);
        require(qFromLittleEndian<qint16>(packet.pcm.constData()) == 32767,
                "gain clipping overflow");
        QTemporaryDir dir;
        const auto file = dir.filePath("audio.wav");
        const auto origin = luma::clockNs();
        {
            luma::WaveWriter writer(file);
            QByteArray samples(480 * 4, '\x31');
            writer.append({samples, origin + 10000000}, origin);
            require(writer.frames() == 960, "silence gap or packet size incorrect");
            writer.append({samples, origin + 10000000}, origin);
            require(writer.frames() == 960, "overlap duplicated");
            writer.finish(30000000);
        }
        QFile wave(file);
        require(wave.open(QIODevice::ReadOnly), "WAV missing");
        auto bytes = wave.readAll();
        require(bytes.left(4) == "RF64" && bytes.size() == 80 + 1440 * 4,
                "RF64 header or tail duration incorrect");
        require(bytes.mid(80, 480 * 4) == QByteArray(480 * 4, 0), "leading silence incorrect");
        QDataStream header(bytes.mid(20, 24));
        header.setByteOrder(QDataStream::LittleEndian);
        quint64 riff, data, frames;
        header >> riff >> data >> frames;
        require(riff == quint64(bytes.size() - 8) && data == 1440 * 4 && frames == 1440,
                "RF64 ds64 lengths incorrect");
        luma::SessionTimeline timeline;
        const qint64 base = 1000000000;
        timeline.reset(base);
        timeline.setPaused(true, base + 10000000);
        require(timeline.elapsed(base + 30000000) == 10000000, "open pause counted as active time");
        require(timeline.project({QByteArray(480 * 4, 1), base + 20000000}).isEmpty(),
                "audio retained during pause");
        timeline.setPaused(false, base + 20000000);
        const auto segments = timeline.project({QByteArray(1440 * 4, 1), base});
        require(segments.size() == 2 && segments[0].pcm.size() == 480 * 4 &&
                    segments[1].pcm.size() == 480 * 4,
                "packet crossing pause was not split correctly");
        require(segments[1].timestampNs == base + 10000000,
                "resumed audio timestamp retained paused time");
        require(timeline.elapsed(base + 40000000) == 30000000,
                "video timeline did not remove pause");
        timeline.setPaused(true, base + 35000000);
        require(timeline.elapsed(base + 45000000) == 25000000, "second/open final pause broken");
        const auto probe = luma::RecordingSession::probeArguments(s);
        require(probe.contains("lavfi") && probe.contains("null") && probe.contains("libx264"),
                "encoder check is not an actual encode");
        TestPlatform platform;
        luma::AudioCheck check;
        QEventLoop loop;
        QString checkFile, checkError;
        double peak = 0;
        QObject::connect(&check, &luma::AudioCheck::completed, &loop,
                         [&](QString path, QString error, double value) {
                             checkFile = path;
                             checkError = error;
                             peak = value;
                             loop.quit();
                         });
        QTimer watchdog;
        watchdog.setSingleShot(true);
        QObject::connect(&watchdog, &QTimer::timeout, &loop, &QEventLoop::quit);
        watchdog.start(3000);
        const auto sample = dir.filePath("check.wav");
        check.start(&platform, {{"test", "Test", "fixture", luma::AudioKind::Microphone}}, sample,
                    200);
        loop.exec();
        check.stopAndWait();
        require(checkError.isEmpty() && checkFile == sample && peak > 0.1,
                "pre-record audio check did not capture sample/levels");
        QFile audition(sample);
        require(audition.open(QIODevice::ReadOnly) && audition.size() > 80 + 9000 * 4,
                "audio sample is missing/too short");
        std::cout
            << "PASS: settings, encoder arguments, RF64, timestamp gaps, overlap, silent tail\n";
        return 0;
    } catch (const std::exception &ex) {
        std::cerr << ex.what() << "\n";
        return 1;
    }
}
