#include "RecorderController.h"
#include "AppInfo.h"
#include <QDesktopServices>
#include <QDir>
#include <QGuiApplication>
#include <QMutexLocker>
#include <QSaveFile>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>
#include <QStorageInfo>
#include <cmath>

namespace {
void tracePreflight(const QString &stage) {
    if (!QCoreApplication::arguments().contains("--encoder-test"))
        return;
    QDir().mkpath("artifacts");
    QFile file("artifacts/preflight-trace.log");
    if (file.open(QIODevice::WriteOnly | QIODevice::Append))
        file.write((QString::number(luma::clockNs() / 1e9, 'f', 3) + " " + stage + "\n").toUtf8());
}
} // namespace

QImage PreviewProvider::requestImage(const QString &, QSize *size, const QSize &requested) {
    QMutexLocker lock(&mutex_);
    if (image_.isNull()) {
        QImage blank(2, 2, QImage::Format_ARGB32);
        blank.fill(Qt::black);
        if (size)
            *size = blank.size();
        return blank;
    }
    if (size)
        *size = image_.size();
    return requested.isValid()
               ? image_.scaled(requested, Qt::KeepAspectRatio, Qt::SmoothTransformation)
               : image_;
}
void PreviewProvider::setFrame(QImage image) {
    QMutexLocker lock(&mutex_);
    image_ = std::move(image);
}
RecorderController::RecorderController(PreviewProvider *preview, QObject *parent)
    : QObject(parent), platform_(luma::createPlatform()), preview_(preview) {
    QSettings store;
    // Carry forward existing capture preferences after the product rename. The new
    // product starts with MP4; a later explicit container selection is preserved.
    if (!store.value("migration/FARecorder", false).toBool()) {
        QSettings legacy(QSettings::defaultFormat(), QSettings::UserScope, "Luma", "Luma Recorder");
        for (const auto &key : legacy.allKeys())
            if (key != "container" && !store.contains(key))
                store.setValue(key, legacy.value(key));
        store.setValue("migration/FARecorder", true);
    }
    expanded_ = store.value("expanded", false).toBool();
    settings_.width = store.value("width", 1920).toInt();
    settings_.height = store.value("height", 1080).toInt();
    settings_.fps = store.value("fps", 60).toInt();
    settings_.delaySeconds = store.value("delay", 3).toInt();
    settings_.encoder = store.value("encoder", "libx264").toString();
    settings_.container = store.value("container", "mp4").toString();
    settings_.cursor = store.value("cursor", true).toBool();
    settings_.outputFolder =
        store
            .value("output", QDir(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation))
                                 .filePath("FARecorder"))
            .toString();
    previewTimer_.setInterval(150);
    connect(&previewTimer_, &QTimer::timeout, this, [this] {
        if (video_ && !optimized_) {
            auto image = video_->latestFrame();
            if (!image.isNull()) {
                preview_->setFrame(
                    image.scaled(1200, 750, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                revision_++;
                emit previewChanged();
            }
        }
        if (++statsTicks_ % 7 == 0) {
            const auto now = luma::clockNs();
            const auto frames = session_.frames();
            throughput_ =
                state_ == "recording" && statTime_ && frames >= statFrames_
                    ? QString::number((frames - statFrames_) * 1e9 / (now - statTime_), 'f', 1) +
                          " fps"
                    : "—";
            statTime_ = now;
            statFrames_ = frames;
        }
        if (statsTicks_ % 30 == 1) {
            QDir path(settings_.outputFolder);
            while (!path.exists() && path.cdUp()) {
            }
            QStorageInfo storage(path.absolutePath());
            diskSpace_ =
                storage.isValid()
                    ? QString::number(storage.bytesAvailable() / 1073741824.0, 'f', 1) + " GB 여유"
                    : "저장 위치 확인 필요";
        }
        emit tick();
    });
    previewTimer_.start();
    countdownTimer_.setInterval(1000);
    connect(&countdownTimer_, &QTimer::timeout, this, [this] {
        if (--countdown_ <= 0) {
            countdownTimer_.stop();
            startNow();
        } else
            emit stateChanged();
    });
    connect(&session_, &luma::RecordingSession::started, this, [this](QString) {
        setState("recording");
        setOptimized(true);
    });
    connect(&session_, &luma::RecordingSession::finalizing, this, [this] { setState("saving"); });
    connect(&session_, &luma::RecordingSession::finished, this,
            [this](QString file, QString warning) {
                lastFile_ = file;
                setState("idle");
                emit lastFileChanged();
                if (!warning.isEmpty())
                    showMessage(warning);
                emit editorRequested();
            });
    connect(&session_, &luma::RecordingSession::failed, this,
            [this](QString error, QString folder) {
                setState("idle");
                showMessage(error + (folder.isEmpty() ? "" : "\n복구 파일: " + folder));
            });
    connect(&trimProcess_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus) {
                const auto log = trimProcess_.readAllStandardError();
                if (code != 0) {
                    QFile::remove(trimTemp_);
                    setState("idle");
                    showMessage("편집본을 저장하지 못했습니다. 원본은 보존했습니다.\n" +
                                QString::fromUtf8(log).right(1500));
                    return;
                }
                bool saved = false;
                if (overwrite_) {
                    // QSaveFile commits by atomic replacement only after the complete edit exists.
                    QFile input(trimTemp_);
                    QSaveFile output(trimTarget_);
                    output.setDirectWriteFallback(false);
                    if (input.open(QIODevice::ReadOnly) && output.open(QIODevice::WriteOnly)) {
                        bool ok = true;
                        while (!input.atEnd()) {
                            const auto bytes = input.read(4 * 1024 * 1024);
                            if (bytes.isEmpty() && input.error() != QFile::NoError) {
                                ok = false;
                                break;
                            }
                            if (output.write(bytes) != bytes.size()) {
                                ok = false;
                                break;
                            }
                        }
                        if (ok)
                            saved = output.commit();
                        else
                            output.cancelWriting();
                    }
                    if (saved)
                        QFile::remove(trimTemp_);
                } else
                    saved = QFile::rename(trimTemp_, trimTarget_);
                setState("idle");
                if (saved) {
                    lastFile_ = trimTarget_;
                    emit lastFileChanged();
                    emit trimFinished(QUrl::fromLocalFile(lastFile_));
                } else
                    showMessage("파일 교체에 실패했습니다. 원본은 보존했습니다. 편집본: " +
                                trimTemp_);
            });
    connect(&trimProcess_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError err) {
        if (err == QProcess::FailedToStart) {
            setState("idle");
            showMessage("편집 인코더를 실행하지 못했습니다.");
        }
    });
    connect(&hotkeys_, &GlobalHotkeys::recordPressed, this, &RecorderController::toggleRecording);
    connect(&hotkeys_, &GlobalHotkeys::pausePressed, this, &RecorderController::togglePause);
    connect(&audioCheck_, &luma::AudioCheck::ready, this, [this] {
        audioTestTimer_.restart();
        audioTestStatus_ = "소리를 내거나 마이크에 말해 보세요";
        emit audioTestChanged();
    });
    connect(&audioCheck_, &luma::AudioCheck::completed, this,
            [this](QString file, QString error, double peak) {
                audioSample_ = file;
                audioTestStatus_ =
                    !error.isEmpty() ? error
                    : peak < 0.001 ? "소리가 감지되지 않았습니다. 소스·뮤트·장치 권한을 확인하세요."
                                   : "테스트 완료 · 샘플을 들어볼 수 있습니다";
                emit audioTestChanged();
                if (pendingStart_) {
                    pendingStart_ = false;
                    if (error.isEmpty()) {
                        setState("idle");
                        prepareRecording();
                    } else {
                        setState("idle");
                        showMessage(error);
                    }
                }
            });
    encoderTimeout_.setSingleShot(true);
    encoderTimeout_.setInterval(15000);
    connect(&encoderTimeout_, &QTimer::timeout, this, [this] {
        tracePreflight("timeout");
        probeTimedOut_ = true;
        encoderProbe_.kill();
    });
    connect(
        &encoderProbe_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
        [this](int code, QProcess::ExitStatus exit) {
            tracePreflight(QString("finished code=%1 cancel=%2").arg(code).arg(probeCancelled_));
            encoderTimeout_.stop();
            const auto error = QString::fromUtf8(encoderProbe_.readAllStandardError()).trimmed();
            if (probeCancelled_) {
                pendingStart_ = false;
                encoderStatus_ = "unchecked";
                encoderDetail_ = "검사 취소";
                setState("idle");
                emit encoderChanged();
                return;
            }
            const bool passed = exit == QProcess::NormalExit && code == 0;
            encoderStatus_ = passed ? "passed" : "failed";
            encoderDetail_ = passed           ? "선택한 해상도·FPS의 실제 인코딩 통과"
                             : probeTimedOut_ ? "15초 안에 검사를 마치지 못했습니다. CPU "
                                                "인코더나 낮은 해상도로 다시 검사해 주세요."
                                              : "이 인코더를 사용할 수 없습니다. CPU 인코더를 "
                                                "선택하거나 그래픽 드라이버를 확인해 주세요.";
            const bool fixture = QCoreApplication::arguments().contains("--encoder-test") ||
                                 QCoreApplication::arguments().contains("--workflow-test");
            const auto logs =
                fixture
                    ? QDir::current().absoluteFilePath("artifacts")
                    : QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                          .filePath("logs");
            if (QDir().mkpath(logs)) {
                QFile log(QDir(logs).filePath("encoder-check.log"));
                if (log.open(QIODevice::WriteOnly))
                    log.write((probingFingerprint_ + "\n" + error + "\n").toUtf8());
            }
            checkedFingerprint_ = passed ? probingFingerprint_ : QString();
            const bool start = pendingStart_;
            pendingStart_ = false;
            setState("idle");
            emit encoderChanged();
            tracePreflight("emitted " + encoderStatus_);
            if (passed && start)
                beginCountdown();
            else if (!passed)
                showMessage(encoderDetail_);
        });
    connect(&encoderProbe_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            encoderTimeout_.stop();
            pendingStart_ = false;
            encoderStatus_ = "failed";
            encoderDetail_ = "인코더를 실행하지 못했습니다.";
            setState("idle");
            emit encoderChanged();
            showMessage(encoderDetail_);
        }
    });
    // Listing capture sources can request OS permission. Do it on explicit user refresh.
}
RecorderController::~RecorderController() {
    audioCheck_.stopAndWait();
    if (encoderProbe_.state() != QProcess::NotRunning) {
        encoderProbe_.kill();
        encoderProbe_.waitForFinished(2000);
    }
    if (trimProcess_.state() != QProcess::NotRunning) {
        trimProcess_.kill();
        trimProcess_.waitForFinished();
    }
    session_.requestStop();
    // session_ is declared after video_, so its worker joins before capture is destroyed.
}
void RecorderController::setState(QString value) {
    state_ = std::move(value);
    if (state_ != "recording" && state_ != "paused")
        setOptimized(false);
    emit stateChanged();
}
void RecorderController::showMessage(QString value) {
    message_ = std::move(value);
    emit messageChanged();
}
void RecorderController::clearMessage() {
    showMessage({});
}
QVariantList RecorderController::displays() const {
    QVariantList out;
    for (const auto &s : sourceList_)
        if (s.kind == luma::SourceKind::Display)
            out << QVariantMap{{"id", s.id}, {"name", s.name}};
    return out;
}
QVariantList RecorderController::windows() const {
    QVariantList out;
    for (const auto &s : sourceList_)
        if (s.kind == luma::SourceKind::Window)
            out << QVariantMap{{"id", s.id}, {"name", s.name}};
    return out;
}
QVariantList RecorderController::audio() const {
    QVariantList out;
    for (const auto &s : audioList_)
        out << QVariantMap{{"id", (s.kind == luma::AudioKind::Application  ? "app:"
                                   : s.kind == luma::AudioKind::Microphone ? "mic:"
                                                                           : "system:") +
                                      s.id},
                           {"name", s.name},
                           {"detail", s.detail},
                           {"selected", selectedAudio_.contains(
                                            (s.kind == luma::AudioKind::Application  ? "app:"
                                             : s.kind == luma::AudioKind::Microphone ? "mic:"
                                                                                     : "system:") +
                                            s.id)},
                           {"kind", int(s.kind)},
                           {"gain", s.mix->gain.load()},
                           {"muted", s.mix->muted.load()}};
    return out;
}
QVariantMap RecorderController::audioLevels() const {
    QVariantMap out;
    for (const auto &s : audioList_) {
        const QString key = (s.kind == luma::AudioKind::Application  ? "app:"
                             : s.kind == luma::AudioKind::Microphone ? "mic:"
                                                                     : "system:") +
                            s.id;
        out[key] =
            state_ == "recording" || audioCheck_.active() ? s.mix->peak.exchange(0.0f) : 0.0f;
    }
    return out;
}
QVariantMap RecorderController::mixStates() const {
    QVariantMap out;
    for (const auto &s : audioList_) {
        const QString prefix = s.kind == luma::AudioKind::Application  ? "app:"
                               : s.kind == luma::AudioKind::Microphone ? "mic:"
                                                                       : "system:";
        out[prefix + s.id] =
            QVariantMap{{"gain", s.mix->gain.load()}, {"muted", s.mix->muted.load()}};
    }
    return out;
}
QVariantMap RecorderController::settings() const {
    return {{"width", settings_.width},     {"height", settings_.height},
            {"fps", settings_.fps},         {"delay", settings_.delaySeconds},
            {"encoder", settings_.encoder}, {"container", settings_.container},
            {"cursor", settings_.cursor},   {"output", settings_.outputFolder}};
}
QString RecorderController::elapsed() const {
    const auto seconds = session_.frames() / std::max(1, settings_.fps);
    return QString("%1:%2:%3")
        .arg(seconds / 3600, 2, 10, QChar('0'))
        .arg(seconds / 60 % 60, 2, 10, QChar('0'))
        .arg(seconds % 60, 2, 10, QChar('0'));
}
void RecorderController::refresh() {
    if (state_ != "idle" || audioCheck_.active())
        return;
    try {
        sourceList_ = platform_->sources();
        auto fresh = platform_->audioSources();
        for (auto &next : fresh)
            for (const auto &previous : audioList_)
                if (next.id == previous.id && next.kind == previous.kind) {
                    next.mix = previous.mix;
                    break;
                }
        audioList_ = std::move(fresh);
        emit sourcesChanged();
    } catch (const std::exception &ex) {
        showMessage(QString::fromUtf8(ex.what()));
    }
}
void RecorderController::attach(const luma::Source &source, QRect crop) {
    try {
        auto capture = platform_->createVideoCapture();
        capture->start(source, crop, settings_.cursor, [this](QString error) {
            QMetaObject::invokeMethod(
                this,
                [this, error] {
                    showMessage(error);
                    if (state_ == "recording" || state_ == "paused")
                        session_.requestStop();
                },
                Qt::QueuedConnection);
        });
        video_ = std::move(capture);
        preview_->setFrame({});
        revision_++;
        emit previewChanged();
        sourceName_ =
            source.name +
            (crop.isEmpty() ? "" : QString(" · %1 × %2 영역").arg(crop.width()).arg(crop.height()));
        emit sourceChanged();
    } catch (const std::exception &ex) {
        showMessage(QString::fromUtf8(ex.what()));
    }
}
void RecorderController::connectSource(QString id, bool window) {
    if (state_ != "idle")
        return;
    for (const auto &source : sourceList_)
        if (source.id == id && (source.kind == luma::SourceKind::Window) == window) {
            attach(source);
            return;
        }
}
void RecorderController::beginRegion(QString id) {
    if (state_ != "idle")
        return;
    int index = 0;
    for (const auto &source : sourceList_)
        if (source.kind == luma::SourceKind::Display) {
            if (source.id == id) {
                const auto screens = QGuiApplication::screens();
                const auto screen = screens.value(index, QGuiApplication::primaryScreen());
                const auto geo = screen->geometry();
                emit regionRequested(id, geo.x(), geo.y(), geo.width(), geo.height(),
                                     double(source.bounds.width()) / geo.width());
                return;
            }
            index++;
        }
}
void RecorderController::selectRegion(QString id, double x, double y, double width, double height,
                                      double scale) {
    if (state_ != "idle")
        return;
    for (const auto &source : sourceList_)
        if (source.id == id && source.kind == luma::SourceKind::Display) {
            QRect crop(qRound(x * scale), qRound(y * scale), qRound(width * scale),
                       qRound(height * scale));
            crop = crop.intersected(QRect(QPoint(0, 0), source.bounds.size()));
            if (crop.width() < 16 || crop.height() < 16) {
                showMessage("16 × 16 픽셀 이상의 영역을 선택해 주세요.");
                return;
            }
            attach(source, crop);
            return;
        }
}
void RecorderController::selectAudio(QString id, bool selected) {
    if (state_ != "idle" || audioCheck_.active())
        return;
    if (selected) {
        // System loopback already includes app audio; avoid accidental double capture.
        if (id.startsWith("app:")) {
            for (auto it = selectedAudio_.begin(); it != selectedAudio_.end();)
                if (it->startsWith("system:"))
                    it = selectedAudio_.erase(it);
                else
                    ++it;
        }
        if (id.startsWith("system:")) {
            for (auto it = selectedAudio_.begin(); it != selectedAudio_.end();)
                if (it->startsWith("app:"))
                    it = selectedAudio_.erase(it);
                else
                    ++it;
        }
        selectedAudio_.insert(id);
    } else
        selectedAudio_.remove(id);
    emit sourcesChanged();
}
void RecorderController::setAudioGain(QString id, double gain) {
    if (!std::isfinite(gain))
        return;
    for (auto &s : audioList_) {
        const QString key = (s.kind == luma::AudioKind::Application  ? "app:"
                             : s.kind == luma::AudioKind::Microphone ? "mic:"
                                                                     : "system:") +
                            s.id;
        if (key == id)
            s.mix->gain = float(std::clamp(gain, 0.0, 2.0));
    }
    emit mixerChanged();
}
void RecorderController::setAudioMuted(QString id, bool muted) {
    for (auto &s : audioList_) {
        const QString key = (s.kind == luma::AudioKind::Application  ? "app:"
                             : s.kind == luma::AudioKind::Microphone ? "mic:"
                                                                     : "system:") +
                            s.id;
        if (key == id)
            s.mix->muted = muted;
    }
    emit mixerChanged();
}
void RecorderController::setSetting(QString key, QVariant value) {
    if (state_ != "idle")
        return;
    if (key == "width")
        settings_.width = value.toInt();
    else if (key == "height")
        settings_.height = value.toInt();
    else if (key == "fps")
        settings_.fps = value.toInt();
    else if (key == "delay")
        settings_.delaySeconds = value.toInt();
    else if (key == "encoder")
        settings_.encoder = value.toString();
    else if (key == "container")
        settings_.container = value.toString();
    else if (key == "cursor") {
        try {
            if (video_)
                video_->setCursor(value.toBool());
            settings_.cursor = value.toBool();
        } catch (const std::exception &ex) {
            showMessage(QString::fromUtf8(ex.what()));
        }
    }
    emit settingsChanged();
    if (encoderFingerprint() != checkedFingerprint_) {
        encoderStatus_ = "unchecked";
        encoderDetail_ = "설정 변경 · 다시 검사 필요";
        emit encoderChanged();
    }
    saveSettings();
}
void RecorderController::setOutputFolder(QUrl url) {
    if (state_ != "idle" || !url.isLocalFile())
        return;
    settings_.outputFolder = url.toLocalFile();
    emit settingsChanged();
    saveSettings();
}
void RecorderController::saveSettings() {
    QSettings store;
    const auto map = settings();
    for (auto it = map.cbegin(); it != map.cend(); ++it)
        store.setValue(it.key(), it.value());
}
void RecorderController::toggleRecording() {
    if (state_ == "checking") {
        tracePreflight("cancel via record action");
        probeCancelled_ = true;
        pendingStart_ = false;
        encoderProbe_.kill();
        return;
    }
    if (state_ == "preparing") {
        pendingStart_ = false;
        audioCheck_.requestStop();
        setState("idle");
        return;
    }
    if (state_ == "countdown") {
        countdownTimer_.stop();
        countdown_ = 0;
        setState("idle");
        return;
    }
    if (state_ == "recording" || state_ == "paused") {
        setState("saving");
        session_.requestStop();
        return;
    }
    if (state_ != "idle")
        return;
    try {
        settings_.validate();
        if (!video_ || video_->latestFrame().isNull())
            throw std::runtime_error("먼저 녹화할 화면을 연결해 주세요.");
        if (!encoderReady())
            throw std::runtime_error("FFmpeg를 설치해 주세요. README의 설치 방법을 확인해 주세요.");
        if (audioCheck_.active()) {
            pendingStart_ = true;
            setState("preparing");
            audioCheck_.requestStop();
            return;
        }
        prepareRecording();
    } catch (const std::exception &ex) {
        showMessage(QString::fromUtf8(ex.what()));
    }
}
QString RecorderController::diskSpace() const {
    return diskSpace_;
}
void RecorderController::setExpanded(bool value) {
    expanded_ = value;
    QSettings().setValue("expanded", value);
    emit modeChanged();
}
void RecorderController::setOptimized(bool value) {
    value = value && (state_ == "recording" || state_ == "paused");
    if (optimized_ == value)
        return;
    optimized_ = value;
    emit optimizedChanged();
}
QVariantMap RecorderController::appInfo() const {
    return {{"name", appinfo::name},
            {"version", appinfo::version},
            {"author", appinfo::author},
            {"updated", appinfo::updated},
            {"repository", appinfo::repository}};
}
int RecorderController::audioTestProgress() const {
    return !audioCheck_.active() || !audioTestTimer_.isValid()
               ? 0
               : std::clamp(int(audioTestTimer_.elapsed() / 50), 0, 100);
}
QList<luma::AudioSource> RecorderController::selectedSources() const {
    QList<luma::AudioSource> result;
    for (const auto &source : audioList_) {
        const QString prefix = source.kind == luma::AudioKind::Application  ? "app:"
                               : source.kind == luma::AudioKind::Microphone ? "mic:"
                                                                            : "system:";
        if (selectedAudio_.contains(prefix + source.id))
            result << source;
    }
    return result;
}
void RecorderController::toggleAudioTest() {
    if (audioCheck_.active()) {
        audioCheck_.requestStop();
        return;
    }
    if (state_ != "idle")
        return;
    const auto selected = selectedSources();
    if (selected.isEmpty()) {
        showMessage("테스트할 마이크나 앱 소리를 먼저 선택해 주세요.");
        return;
    }
    if (!audioTemp_.isValid()) {
        showMessage("오디오 테스트 임시 폴더를 만들지 못했습니다.");
        return;
    }
    audioTestTimer_.invalidate();
    audioTestStatus_ = "오디오 소스 연결 중…";
    audioSample_.clear();
    audioCheck_.start(platform_.get(), selected,
                      audioTemp_.filePath(QUuid::createUuid().toString(QUuid::Id128) + ".wav"));
    emit audioTestChanged();
}
void RecorderController::togglePause() {
    if (state_ == "recording") {
        session_.setPaused(true);
        setState("paused");
    } else if (state_ == "paused") {
        session_.setPaused(false);
        setState("recording");
    }
}
QString RecorderController::encoderFingerprint() const {
    return QString("%1:%2:%3:%4")
        .arg(settings_.encoder)
        .arg(settings_.width)
        .arg(settings_.height)
        .arg(settings_.fps);
}
void RecorderController::checkEncoder() {
    tracePreflight("requested state=" + state_);
    if (state_ != "idle")
        return;
    try {
        settings_.validate();
        if (!encoderReady())
            throw std::runtime_error("녹화 엔진을 찾을 수 없습니다.");
    } catch (const std::exception &error) {
        pendingStart_ = false;
        showMessage(QString::fromUtf8(error.what()));
        return;
    }
    probeCancelled_ = false;
    probeTimedOut_ = false;
    encoderStatus_ = "checking";
    encoderDetail_ = "선택한 설정으로 실제 인코딩 중…";
    probingFingerprint_ = encoderFingerprint();
    setState("checking");
    emit encoderChanged();
    encoderProbe_.start(luma::RecordingSession::ffmpegPath(),
                        luma::RecordingSession::probeArguments(settings_));
    tracePreflight("launch " + encoderFingerprint());
    encoderTimeout_.start();
}
void RecorderController::prepareRecording() {
    if (checkedFingerprint_ != encoderFingerprint()) {
        pendingStart_ = true;
        checkEncoder();
    } else
        beginCountdown();
}
void RecorderController::beginCountdown() {
    if (settings_.delaySeconds) {
        countdown_ = settings_.delaySeconds;
        setState("countdown");
        countdownTimer_.start();
    } else
        startNow();
}
void RecorderController::configureWorkflowTest(const luma::Source &source,
                                               const QList<luma::AudioSource> &audio) {
    if (source.processId != quint32(QCoreApplication::applicationPid()))
        throw std::runtime_error("Test capture must belong to this process.");
    sourceList_ = {source};
    audioList_ = audio;
    selectedAudio_.clear();
    for (const auto &entry : audio)
        selectedAudio_.insert("app:" + entry.id);
    settings_.width = 640;
    settings_.height = 360;
    settings_.fps = 30;
    settings_.delaySeconds = 0;
    settings_.encoder = "libx264";
    settings_.outputFolder = QDir::current().absoluteFilePath("artifacts/recordings");
    emit settingsChanged();
    emit sourcesChanged();
    attach(source);
}
void RecorderController::startNow() {
    QList<luma::AudioSource> selected;
    for (const auto &source : audioList_) {
        const QString prefix = source.kind == luma::AudioKind::Application  ? "app:"
                               : source.kind == luma::AudioKind::Microphone ? "mic:"
                                                                            : "system:";
        if (selectedAudio_.contains(prefix + source.id))
            selected << source;
    }
    setState("starting");
    try {
        session_.start(platform_.get(), video_.get(), settings_, selected);
    } catch (const std::exception &ex) {
        setState("idle");
        showMessage(QString::fromUtf8(ex.what()));
    }
}
void RecorderController::openOutputFolder() {
    if (!QDir().mkpath(settings_.outputFolder) ||
        !QDesktopServices::openUrl(QUrl::fromLocalFile(settings_.outputFolder)))
        showMessage("녹화 폴더를 열 수 없습니다. 저장 위치를 확인해 주세요.");
}
void RecorderController::openRecording(QUrl file) {
    if (state_ != "idle" || !file.isLocalFile() || !QFileInfo::exists(file.toLocalFile()))
        return;
    lastFile_ = file.toLocalFile();
    emit lastFileChanged();
    emit editorRequested();
}
void RecorderController::exportTrim(double start, double end, bool overwrite) {
    if (state_ != "idle")
        return;
    if (!std::isfinite(start) || !std::isfinite(end) || start < 0 || end <= start ||
        !QFileInfo::exists(lastFile_)) {
        showMessage("자르기 시작·종료 시간을 확인해 주세요.");
        return;
    }
    const QFileInfo original(lastFile_);
    const auto token = QUuid::createUuid().toString(QUuid::Id128).left(8);
    trimTemp_ = original.dir().filePath(".farecorder-edit-" + token + "." + original.suffix());
    trimTarget_ = overwrite ? lastFile_
                            : original.dir().filePath(original.completeBaseName() + "_trimmed_" +
                                                      token + "." + original.suffix());
    overwrite_ = overwrite;
    QStringList args = {"-hide_banner",
                        "-n",
                        "-ss",
                        QString::number(start, 'f', 6),
                        "-i",
                        lastFile_,
                        "-t",
                        QString::number(end - start, 'f', 6),
                        "-map",
                        "0",
                        "-c:v",
                        "libx264",
                        "-preset",
                        "veryfast",
                        "-crf",
                        "18",
                        "-c:a",
                        "aac",
                        "-b:a",
                        "192k",
                        "-map_metadata",
                        "0"};
    if (original.suffix() == "mp4")
        args << "-movflags" << "+faststart";
    args << trimTemp_;
    setState("editing");
    trimProcess_.start(luma::RecordingSession::ffmpegPath(), args);
}
