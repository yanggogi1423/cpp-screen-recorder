#pragma once
#include "core/RecordingSession.h"
#include "core/AudioCheck.h"
#include "GlobalHotkeys.h"
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QMutex>
#include <QObject>
#include <QProcess>
#include <QQuickImageProvider>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QVariantList>

class PreviewProvider final : public QQuickImageProvider {
  public:
    PreviewProvider() : QQuickImageProvider(Image) {}
    QImage requestImage(const QString &, QSize *size, const QSize &requested) override;
    void setFrame(QImage image);

  private:
    QMutex mutex_;
    QImage image_;
};
class RecorderController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList displays READ displays NOTIFY sourcesChanged)
    Q_PROPERTY(QVariantList windows READ windows NOTIFY sourcesChanged)
    Q_PROPERTY(QVariantList audio READ audio NOTIFY sourcesChanged)
    Q_PROPERTY(QVariantMap audioLevels READ audioLevels NOTIFY tick)
    Q_PROPERTY(QVariantMap mixStates READ mixStates NOTIFY mixerChanged)
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY sourceChanged)
    Q_PROPERTY(bool hasSource READ hasSource NOTIFY sourceChanged)
    Q_PROPERTY(int previewRevision READ previewRevision NOTIFY previewChanged)
    Q_PROPERTY(int countdown READ countdown NOTIFY stateChanged)
    Q_PROPERTY(QString elapsed READ elapsed NOTIFY tick)
    Q_PROPERTY(QUrl lastFile READ lastFile NOTIFY lastFileChanged)
    Q_PROPERTY(QString platformName READ platformName CONSTANT)
    Q_PROPERTY(bool encoderReady READ encoderReady CONSTANT)
    Q_PROPERTY(QString encoderStatus READ encoderStatus NOTIFY encoderChanged)
    Q_PROPERTY(QString encoderDetail READ encoderDetail NOTIFY encoderChanged)
    Q_PROPERTY(bool audioTestActive READ audioTestActive NOTIFY audioTestChanged)
    Q_PROPERTY(int audioTestProgress READ audioTestProgress NOTIFY tick)
    Q_PROPERTY(QString audioTestStatus READ audioTestStatus NOTIFY audioTestChanged)
    Q_PROPERTY(QUrl audioSample READ audioSample NOTIFY audioTestChanged)
    Q_PROPERTY(QString hotkeyStatus READ hotkeyStatus CONSTANT)
    Q_PROPERTY(bool globalHotkeysAvailable READ globalHotkeysAvailable CONSTANT)
    Q_PROPERTY(QString diskSpace READ diskSpace NOTIFY tick)
    Q_PROPERTY(bool expanded READ expanded NOTIFY modeChanged)
  public:
    explicit RecorderController(PreviewProvider *, QObject *parent = nullptr);
    ~RecorderController() override;
    QVariantList displays() const;
    QVariantList windows() const;
    QVariantList audio() const;
    QVariantMap audioLevels() const;
    QVariantMap mixStates() const;
    QVariantMap settings() const;
    QString state() const {
        return state_;
    }
    QString message() const {
        return message_;
    }
    QString sourceName() const {
        return sourceName_;
    }
    bool hasSource() const {
        return video_ != nullptr;
    }
    int previewRevision() const {
        return revision_;
    }
    int countdown() const {
        return countdown_;
    }
    QString elapsed() const;
    QUrl lastFile() const {
        return QUrl::fromLocalFile(lastFile_);
    }
    QString platformName() const {
        return platform_->name();
    }
    bool encoderReady() const {
        return !luma::RecordingSession::ffmpegPath().isEmpty();
    }
    QString encoderStatus() const {
        return encoderStatus_;
    }
    QString encoderDetail() const {
        return encoderDetail_;
    }
    bool audioTestActive() const {
        return audioCheck_.active();
    }
    int audioTestProgress() const;
    QString audioTestStatus() const {
        return audioTestStatus_;
    }
    QUrl audioSample() const {
        return QUrl::fromLocalFile(audioSample_);
    }
    QString hotkeyStatus() const {
        return hotkeys_.status();
    }
    bool globalHotkeysAvailable() const {
        return hotkeys_.available();
    }
    QString diskSpace() const;
    bool expanded() const {
        return expanded_;
    }
    Q_INVOKABLE void setExpanded(bool value);
    Q_PROPERTY(QString throughput READ throughput NOTIFY tick)
    QString throughput() const {
        return throughput_;
    }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void connectSource(QString id, bool window = false);
    Q_INVOKABLE void beginRegion(QString displayId);
    Q_INVOKABLE void selectRegion(QString id, double x, double y, double width, double height,
                                  double scale);
    Q_INVOKABLE void selectAudio(QString id, bool selected);
    Q_INVOKABLE void setAudioGain(QString id, double gain);
    Q_INVOKABLE void setAudioMuted(QString id, bool muted);
    Q_INVOKABLE void setSetting(QString key, QVariant value);
    Q_INVOKABLE void setOutputFolder(QUrl folder);
    Q_INVOKABLE void toggleRecording();
    Q_INVOKABLE void togglePause();
    Q_INVOKABLE void toggleAudioTest();
    Q_INVOKABLE void checkEncoder();
    // C++-only fixture setup: the test may capture this process's own window only.
    void configureWorkflowTest(const luma::Source &, const QList<luma::AudioSource> &);
    Q_INVOKABLE void openOutputFolder();
    Q_INVOKABLE void openRecording(QUrl file);
    Q_INVOKABLE void clearMessage();
    Q_INVOKABLE void exportTrim(double startSeconds, double endSeconds, bool overwrite);
  signals:
    void sourcesChanged();
    void settingsChanged();
    void stateChanged();
    void messageChanged();
    void sourceChanged();
    void previewChanged();
    void tick();
    void lastFileChanged();
    void regionRequested(QString displayId, int x, int y, int width, int height, double scale);
    void editorRequested();
    void trimFinished(QUrl file);
    void closeAllowed();
    void encoderChanged();
    void audioTestChanged();
    void modeChanged();
    void mixerChanged();

  private:
    void setState(QString state);
    void showMessage(QString message);
    void startNow();
    void saveSettings();
    void attach(const luma::Source &, QRect crop = {});
    QList<luma::AudioSource> selectedSources() const;
    void prepareRecording();
    void beginCountdown();
    QString encoderFingerprint() const;
    std::unique_ptr<luma::Platform> platform_;
    std::unique_ptr<luma::VideoCapture> video_;
    luma::RecordingSession session_;
    luma::AudioCheck audioCheck_;
    GlobalHotkeys hotkeys_;
    QTemporaryDir audioTemp_;
    QElapsedTimer audioTestTimer_;
    QString audioSample_, audioTestStatus_ = "선택한 소리를 5초간 확인합니다";
    QProcess encoderProbe_;
    QTimer encoderTimeout_;
    QString encoderStatus_ = "unchecked", encoderDetail_ = "검사 전", checkedFingerprint_,
            probingFingerprint_;
    bool pendingStart_ = false, probeCancelled_ = false;
    QString diskSpace_ = "확인 중", throughput_ = "—";
    int statsTicks_ = 0;
    qint64 statFrames_ = 0, statTime_ = 0;
    bool expanded_ = false;
    luma::Settings settings_;
    QList<luma::Source> sourceList_;
    QList<luma::AudioSource> audioList_;
    QSet<QString> selectedAudio_;
    PreviewProvider *preview_;
    QTimer previewTimer_, countdownTimer_;
    QString state_ = "idle", message_, sourceName_, lastFile_;
    int revision_ = 0, countdown_ = 0;
    QProcess trimProcess_;
    QString trimTemp_, trimTarget_;
    bool overwrite_ = false;
};
