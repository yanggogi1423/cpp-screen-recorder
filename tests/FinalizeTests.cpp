#include "core/FinalizeProcess.h"
#include "core/RecordingSession.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QThread>
#include <QEventLoop>
#include <QTimer>
#include <QFileInfo>
#include <iostream>

void check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
class Video final : public luma::VideoCapture {
    QImage frame_{160, 120, QImage::Format_ARGB32};
  public:
    Video() { frame_.fill(Qt::black); }
    void start(const luma::Source &, const QRect &, bool, luma::ErrorCallback) override {}
    QImage latestFrame() override { return frame_; }
    void setCursor(bool) override {}
    void stop() override {}
};
class Audio final : public luma::AudioCapture {
    std::thread thread_;
    std::atomic<bool> stopped_{false};
  public:
    ~Audio() override { stop(); }
    void start(const luma::AudioSource &, luma::AudioCallback callback, luma::ErrorCallback) override {
        stopped_ = false;
        thread_ = std::thread([this, callback] {
            while (!stopped_) {
                callback({QByteArray(480 * 4, 0), luma::clockNs()});
                QThread::msleep(10);
            }
        });
    }
    void stop() override { stopped_ = true; if (thread_.joinable()) thread_.join(); }
};
class Platform final : public luma::Platform {
  public:
    QString name() const override { return "synthetic"; }
    QList<luma::Source> sources() override { return {}; }
    QList<luma::AudioSource> audioSources() override { return {}; }
    std::unique_ptr<luma::VideoCapture> createVideoCapture() override { return std::make_unique<Video>(); }
    std::unique_ptr<luma::AudioCapture> createAudioCapture() override { return std::make_unique<Audio>(); }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.contains("--child")) {
        if (args.last() == "fail") return 7;
        if (args.last() == "stall") { QThread::msleep(2000); return 0; }
        for (int i = 1; i <= 10; ++i) {
            std::cout << "out_time_us=" << i * 100000 << "\nprogress=continue\n" << std::flush;
            QThread::msleep(80);
        }
        return 0;
    }
    try {
        luma::FinalizeProgress parser;
        check(!parser.feed("out_time_us=10"), "incomplete progress accepted");
        check(parser.feed("00000\r\n"), "split progress lost");
        check(parser.percent(2000000000LL) == 50, "microsecond units incorrect");
        check(!parser.feed("out_time_us=N/A\nout_time_us=-12\nout_time_us=500000\n"), "invalid/regressing time accepted");
        parser.feed("out_time_ms=999999999999\n");
        check(parser.percent(2000000000LL) == 99, "100 percent emitted before exit");
        QTemporaryDir dir;
        check(dir.isValid(), "temporary directory unavailable");
        for (const auto &mode : {"progress", "stall", "fail"}) {
            QProcess process;
            process.start(app.applicationFilePath(), {"--child", mode});
            check(process.waitForStarted(5000), "fixture process did not start");
            bool threw = false;
            int percent = -1, events = 0;
            try {
                luma::finishMediaProcess(process, dir.filePath("out"), dir.filePath("mux.log"),
                    1000000000LL, [&](int value) { percent = value; ++events; }, 350);
            } catch (...) { threw = true; }
            if (QString(mode) == "progress")
                check(!threw && percent == 100 && events >= 5,
                      "active job longer than inactivity limit did not complete/progress");
            else
                check(threw && percent != 100, "stalled/failed job reported success");
        }
        // Real encode/mix with generated silence only; never opens an OS device.
        if (!luma::RecordingSession::ffmpegPath().isEmpty()) {
            Platform platform;
            Video video;
            luma::RecordingSession session;
            luma::Settings settings;
            settings.width = 160; settings.height = 120; settings.fps = 30;
            settings.outputFolder = dir.path(); settings.recoveryFolder = dir.path();
            QEventLoop loop;
            QString result, error;
            bool stopped = false, progressed = false;
            QObject::connect(&session, &luma::RecordingSession::started, &loop, [&] {
                QTimer::singleShot(550, &loop, [&] { session.requestStop(); });
            });
            QObject::connect(&session, &luma::RecordingSession::finalizing, &loop, [&] { stopped = true; });
            QObject::connect(&session, &luma::RecordingSession::finalizationProgress, &loop,
                [&](QString, int percent) { progressed |= stopped && percent == 100; });
            QObject::connect(&session, &luma::RecordingSession::finished, &loop,
                [&](QString file, QString) { result = file; loop.quit(); });
            QObject::connect(&session, &luma::RecordingSession::failed, &loop,
                [&](QString message, QString) { error = message; loop.quit(); });
            QTimer::singleShot(15000, &loop, [&] { session.requestStop(); loop.quit(); });
            session.start(&platform, &video, settings,
                {{"one", "Silent mic", "", luma::AudioKind::Microphone},
                 {"two", "Silent app", "", luma::AudioKind::Application}});
            loop.exec();
            check(error.isEmpty() && !result.isEmpty() && QFileInfo(result).size() > 0 && progressed,
                  "synthetic recording did not stop, mix and complete with progress");
            QProcess decode;
            decode.start(luma::RecordingSession::ffmpegPath(),
                {"-v", "error", "-nostdin", "-i", result, "-map", "0:v:0", "-map", "0:a:0",
                 "-map", "0:a:1", "-map", "0:a:2", "-f", "null", "-"});
            check(decode.waitForFinished(10000) && decode.exitCode() == 0,
                  "video/mix/two independent tracks could not all decode");
        }
        std::cout << "PASS: fragmented progress, units, active long job, stalled job, failed job, silent recording/mix\n";
        return 0;
    } catch (const std::exception &ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
