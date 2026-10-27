#pragma once
#include "core/RecordingSession.h"
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QQuickImageProvider>
#include <QSet>
#include <QProcess>
#include <QUrl>
#include <QMutex>

class PreviewProvider final:public QQuickImageProvider {
public:
    PreviewProvider():QQuickImageProvider(Image){}
    QImage requestImage(const QString&,QSize* size,const QSize& requested)override;
    void setFrame(QImage image);
private:QMutex mutex_;QImage image_;
};
class RecorderController final:public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList displays READ displays NOTIFY sourcesChanged)
    Q_PROPERTY(QVariantList windows READ windows NOTIFY sourcesChanged)
    Q_PROPERTY(QVariantList audio READ audio NOTIFY sourcesChanged)
    Q_PROPERTY(QVariantMap audioLevels READ audioLevels NOTIFY tick)
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
public:
    explicit RecorderController(PreviewProvider*,QObject* parent=nullptr);
    ~RecorderController()override;
    QVariantList displays()const;QVariantList windows()const;QVariantList audio()const;
    QVariantMap audioLevels()const;
    QVariantMap settings()const;
    QString state()const{return state_;}QString message()const{return message_;}
    QString sourceName()const{return sourceName_;}bool hasSource()const{return video_!=nullptr;}
    int previewRevision()const{return revision_;}int countdown()const{return countdown_;}
    QString elapsed()const;
    QUrl lastFile()const{return QUrl::fromLocalFile(lastFile_);}
    QString platformName()const{return platform_->name();}
    bool encoderReady()const{return !luma::RecordingSession::ffmpegPath().isEmpty();}
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void connectSource(QString id,bool window=false);
    Q_INVOKABLE void beginRegion(QString displayId);
    Q_INVOKABLE void selectRegion(QString id,double x,double y,double width,double height,double scale);
    Q_INVOKABLE void selectAudio(QString id,bool selected);
    Q_INVOKABLE void setAudioGain(QString id,double gain);
    Q_INVOKABLE void setAudioMuted(QString id,bool muted);
    Q_INVOKABLE void setSetting(QString key,QVariant value);
    Q_INVOKABLE void setOutputFolder(QUrl folder);
    Q_INVOKABLE void toggleRecording();
    Q_INVOKABLE void openOutputFolder();
    Q_INVOKABLE void clearMessage();
    Q_INVOKABLE void exportTrim(double startSeconds,double endSeconds,bool overwrite);
signals:
    void sourcesChanged();void settingsChanged();void stateChanged();void messageChanged();void sourceChanged();void previewChanged();void tick();void lastFileChanged();
    void regionRequested(QString displayId,int x,int y,int width,int height,double scale);
    void editorRequested();void trimFinished(QUrl file);void closeAllowed();
private:
    void setState(QString state);void showMessage(QString message);void startNow();void saveSettings();
    void attach(const luma::Source&,QRect crop={});
    std::unique_ptr<luma::Platform> platform_;
    std::unique_ptr<luma::VideoCapture> video_;
    luma::RecordingSession session_;
    luma::Settings settings_;
    QList<luma::Source> sourceList_;
    QList<luma::AudioSource> audioList_;
    QSet<QString> selectedAudio_;
    PreviewProvider* preview_;
    QTimer previewTimer_,countdownTimer_;
    QString state_="idle",message_,sourceName_,lastFile_;
    int revision_=0,countdown_=0;
    QProcess trimProcess_;
    QString trimTemp_,trimTarget_;
    bool overwrite_=false;
};
