#include "RecorderController.h"
#include <QMutexLocker>
#include <QGuiApplication>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QDesktopServices>
#include <QDir>
#include <QSaveFile>
#include <QUuid>
#include <cmath>

QImage PreviewProvider::requestImage(const QString&,QSize* size,const QSize& requested){
    QMutexLocker lock(&mutex_);if(size)*size=image_.size();return requested.isValid()?image_.scaled(requested,Qt::KeepAspectRatio,Qt::SmoothTransformation):image_;
}
void PreviewProvider::setFrame(QImage image){QMutexLocker lock(&mutex_);image_=std::move(image);}
RecorderController::RecorderController(PreviewProvider* preview,QObject* parent):QObject(parent),platform_(luma::createPlatform()),preview_(preview){
    QSettings store;
    settings_.width=store.value("width",1920).toInt();settings_.height=store.value("height",1080).toInt();settings_.fps=store.value("fps",60).toInt();
    settings_.delaySeconds=store.value("delay",3).toInt();settings_.encoder=store.value("encoder","libx264").toString();settings_.container=store.value("container","mkv").toString();
    settings_.cursor=store.value("cursor",true).toBool();settings_.outputFolder=store.value("output",QDir(QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)).filePath("Luma")).toString();
    previewTimer_.setInterval(150);connect(&previewTimer_,&QTimer::timeout,this,[this]{
        if(video_){auto image=video_->latestFrame();if(!image.isNull()){preview_->setFrame(image.scaled(1200,750,Qt::KeepAspectRatio,Qt::SmoothTransformation));revision_++;emit previewChanged();}}
        emit tick();
    });previewTimer_.start();
    countdownTimer_.setInterval(1000);connect(&countdownTimer_,&QTimer::timeout,this,[this]{if(--countdown_<=0){countdownTimer_.stop();startNow();}else emit stateChanged();});
    connect(&session_,&luma::RecordingSession::started,this,[this](QString){setState("recording");});
    connect(&session_,&luma::RecordingSession::finalizing,this,[this]{setState("saving");});
    connect(&session_,&luma::RecordingSession::finished,this,[this](QString file,QString warning){lastFile_=file;setState("idle");emit lastFileChanged();if(!warning.isEmpty())showMessage(warning);emit editorRequested();});
    connect(&session_,&luma::RecordingSession::failed,this,[this](QString error,QString folder){setState("idle");showMessage(error+(folder.isEmpty()?"":"\n복구 파일: "+folder));});
    connect(&trimProcess_,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),this,[this](int code,QProcess::ExitStatus){
        const auto log=trimProcess_.readAllStandardError();
        if(code!=0){QFile::remove(trimTemp_);setState("idle");showMessage("편집본을 저장하지 못했습니다. 원본은 보존했습니다.\n"+QString::fromUtf8(log).right(1500));return;}
        bool saved=false;
        if(overwrite_){
            // QSaveFile commits by atomic replacement only after the complete edit exists.
            QFile input(trimTemp_);QSaveFile output(trimTarget_);output.setDirectWriteFallback(false);
            if(input.open(QIODevice::ReadOnly)&&output.open(QIODevice::WriteOnly)){
                bool ok=true;while(!input.atEnd()){const auto bytes=input.read(4*1024*1024);if(bytes.isEmpty()&&input.error()!=QFile::NoError){ok=false;break;}if(output.write(bytes)!=bytes.size()){ok=false;break;}}
                if(ok)saved=output.commit();else output.cancelWriting();
            }
            if(saved)QFile::remove(trimTemp_);
        }else saved=QFile::rename(trimTemp_,trimTarget_);
        setState("idle");
        if(saved){lastFile_=trimTarget_;emit lastFileChanged();emit trimFinished(QUrl::fromLocalFile(lastFile_));}
        else showMessage("파일 교체에 실패했습니다. 원본은 보존했습니다. 편집본: "+trimTemp_);
    });
    connect(&trimProcess_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError err){if(err==QProcess::FailedToStart){setState("idle");showMessage("편집 인코더를 실행하지 못했습니다.");}});
    // Listing capture sources can request OS permission. Do it on explicit user refresh.
}
RecorderController::~RecorderController(){
    if(trimProcess_.state()!=QProcess::NotRunning){trimProcess_.kill();trimProcess_.waitForFinished();}
    session_.requestStop();
    // session_ is declared after video_, so its worker joins before capture is destroyed.
}
void RecorderController::setState(QString value){state_=std::move(value);emit stateChanged();}
void RecorderController::showMessage(QString value){message_=std::move(value);emit messageChanged();}
void RecorderController::clearMessage(){showMessage({});}
QVariantList RecorderController::displays()const{QVariantList out;for(const auto& s:sourceList_)if(s.kind==luma::SourceKind::Display)out<<QVariantMap{{"id",s.id},{"name",s.name}};return out;}
QVariantList RecorderController::windows()const{QVariantList out;for(const auto& s:sourceList_)if(s.kind==luma::SourceKind::Window)out<<QVariantMap{{"id",s.id},{"name",s.name}};return out;}
QVariantList RecorderController::audio()const{QVariantList out;for(const auto& s:audioList_)out<<QVariantMap{{"id",(s.kind==luma::AudioKind::Application?"app:":s.kind==luma::AudioKind::Microphone?"mic:":"system:")+s.id},{"name",s.name},{"detail",s.detail},{"selected",selectedAudio_.contains((s.kind==luma::AudioKind::Application?"app:":s.kind==luma::AudioKind::Microphone?"mic:":"system:")+s.id)},{"kind",int(s.kind)},{"gain",s.mix->gain.load()},{"muted",s.mix->muted.load()}};return out;}
QVariantMap RecorderController::audioLevels()const{QVariantMap out;for(const auto& s:audioList_){const QString key=(s.kind==luma::AudioKind::Application?"app:":s.kind==luma::AudioKind::Microphone?"mic:":"system:")+s.id;out[key]=state_=="recording"?s.mix->peak.exchange(0.0f):0.0f;}return out;}
QVariantMap RecorderController::settings()const{return {{"width",settings_.width},{"height",settings_.height},{"fps",settings_.fps},{"delay",settings_.delaySeconds},{"encoder",settings_.encoder},{"container",settings_.container},{"cursor",settings_.cursor},{"output",settings_.outputFolder}};}
QString RecorderController::elapsed()const{const auto seconds=session_.frames()/std::max(1,settings_.fps);return QString("%1:%2:%3").arg(seconds/3600,2,10,QChar('0')).arg(seconds/60%60,2,10,QChar('0')).arg(seconds%60,2,10,QChar('0'));}
void RecorderController::refresh(){if(state_!="idle")return;try{
    sourceList_=platform_->sources();auto fresh=platform_->audioSources();
    for(auto& next:fresh)for(const auto& previous:audioList_)if(next.id==previous.id&&next.kind==previous.kind){next.mix=previous.mix;break;}
    audioList_=std::move(fresh);emit sourcesChanged();
}catch(const std::exception& ex){showMessage(QString::fromUtf8(ex.what()));}}
void RecorderController::attach(const luma::Source& source,QRect crop){
    try{
        auto capture=platform_->createVideoCapture();
        capture->start(source,crop,settings_.cursor,[this](QString error){QMetaObject::invokeMethod(this,[this,error]{showMessage(error);if(state_=="recording")session_.requestStop();},Qt::QueuedConnection);});
        video_=std::move(capture);sourceName_=source.name+(crop.isEmpty()?"":QString(" · %1 × %2 영역").arg(crop.width()).arg(crop.height()));emit sourceChanged();
    }catch(const std::exception& ex){showMessage(QString::fromUtf8(ex.what()));}
}
void RecorderController::connectSource(QString id,bool window){if(state_!="idle")return;for(const auto& source:sourceList_)if(source.id==id&&(source.kind==luma::SourceKind::Window)==window){attach(source);return;}}
void RecorderController::beginRegion(QString id){
    if(state_!="idle")return;int index=0;
    for(const auto& source:sourceList_)if(source.kind==luma::SourceKind::Display){
        if(source.id==id){const auto screens=QGuiApplication::screens();const auto screen=screens.value(index,QGuiApplication::primaryScreen());const auto geo=screen->geometry();emit regionRequested(id,geo.x(),geo.y(),geo.width(),geo.height(),double(source.bounds.width())/geo.width());return;}index++;
    }
}
void RecorderController::selectRegion(QString id,double x,double y,double width,double height,double scale){
    if(state_!="idle")return;
    for(const auto& source:sourceList_)if(source.id==id&&source.kind==luma::SourceKind::Display){
        QRect crop(qRound(x*scale),qRound(y*scale),qRound(width*scale),qRound(height*scale));crop=crop.intersected(QRect(QPoint(0,0),source.bounds.size()));
        if(crop.width()<16||crop.height()<16){showMessage("16 × 16 픽셀 이상의 영역을 선택해 주세요.");return;}attach(source,crop);return;
    }
}
void RecorderController::selectAudio(QString id,bool selected){
    if(state_!="idle")return;
    if(selected){
        // System loopback already includes app audio; avoid accidental double capture.
        if(id.startsWith("app:")){for(auto it=selectedAudio_.begin();it!=selectedAudio_.end();)if(it->startsWith("system:"))it=selectedAudio_.erase(it);else ++it;}
        if(id.startsWith("system:")){for(auto it=selectedAudio_.begin();it!=selectedAudio_.end();)if(it->startsWith("app:"))it=selectedAudio_.erase(it);else ++it;}
        selectedAudio_.insert(id);
    }else selectedAudio_.remove(id);
    emit sourcesChanged();
}
void RecorderController::setAudioGain(QString id,double gain){for(auto& s:audioList_){const QString key=(s.kind==luma::AudioKind::Application?"app:":s.kind==luma::AudioKind::Microphone?"mic:":"system:")+s.id;if(key==id)s.mix->gain=float(std::clamp(gain,0.0,2.0));}}
void RecorderController::setAudioMuted(QString id,bool muted){for(auto& s:audioList_){const QString key=(s.kind==luma::AudioKind::Application?"app:":s.kind==luma::AudioKind::Microphone?"mic:":"system:")+s.id;if(key==id)s.mix->muted=muted;}}
void RecorderController::setSetting(QString key,QVariant value){
    if(state_!="idle")return;
    if(key=="width")settings_.width=value.toInt();else if(key=="height")settings_.height=value.toInt();else if(key=="fps")settings_.fps=value.toInt();else if(key=="delay")settings_.delaySeconds=value.toInt();
    else if(key=="encoder")settings_.encoder=value.toString();else if(key=="container")settings_.container=value.toString();else if(key=="cursor"){settings_.cursor=value.toBool();if(video_)video_->setCursor(settings_.cursor);}
    emit settingsChanged();saveSettings();
}
void RecorderController::setOutputFolder(QUrl url){if(state_!="idle"||!url.isLocalFile())return;settings_.outputFolder=url.toLocalFile();emit settingsChanged();saveSettings();}
void RecorderController::saveSettings(){QSettings store;const auto map=settings();for(auto it=map.cbegin();it!=map.cend();++it)store.setValue(it.key(),it.value());}
void RecorderController::toggleRecording(){
    if(state_=="countdown"){countdownTimer_.stop();countdown_=0;setState("idle");return;}
    if(state_=="recording"){setState("saving");session_.requestStop();return;}
    if(state_!="idle")return;
    try{settings_.validate();if(!video_||video_->latestFrame().isNull())throw std::runtime_error("먼저 녹화할 화면을 연결해 주세요.");if(!encoderReady())throw std::runtime_error("FFmpeg를 설치해 주세요. README의 설치 방법을 확인해 주세요.");
        if(settings_.delaySeconds){countdown_=settings_.delaySeconds;setState("countdown");countdownTimer_.start();}else startNow();
    }catch(const std::exception& ex){showMessage(QString::fromUtf8(ex.what()));}
}
void RecorderController::startNow(){
    QList<luma::AudioSource> selected;
    for(const auto& source:audioList_){const QString prefix=source.kind==luma::AudioKind::Application?"app:":source.kind==luma::AudioKind::Microphone?"mic:":"system:";if(selectedAudio_.contains(prefix+source.id))selected<<source;}
    setState("starting");try{session_.start(platform_.get(),video_.get(),settings_,selected);}catch(const std::exception& ex){setState("idle");showMessage(QString::fromUtf8(ex.what()));}
}
void RecorderController::openOutputFolder(){QDesktopServices::openUrl(QUrl::fromLocalFile(lastFile_.isEmpty()?settings_.outputFolder:QFileInfo(lastFile_).absolutePath()));}
void RecorderController::openRecording(QUrl file){if(state_!="idle"||!file.isLocalFile()||!QFileInfo::exists(file.toLocalFile()))return;lastFile_=file.toLocalFile();emit lastFileChanged();emit editorRequested();}
void RecorderController::exportTrim(double start,double end,bool overwrite){
    if(state_!="idle")return;
    if(!std::isfinite(start)||!std::isfinite(end)||start<0||end<=start||!QFileInfo::exists(lastFile_)){showMessage("자르기 시작·종료 시간을 확인해 주세요.");return;}
    const QFileInfo original(lastFile_);const auto token=QUuid::createUuid().toString(QUuid::Id128).left(8);
    trimTemp_=original.dir().filePath(".luma-edit-"+token+"."+original.suffix());
    trimTarget_=overwrite?lastFile_:original.dir().filePath(original.completeBaseName()+"_trimmed_"+token+"."+original.suffix());overwrite_=overwrite;
    QStringList args={"-hide_banner","-n","-ss",QString::number(start,'f',6),"-i",lastFile_,"-t",QString::number(end-start,'f',6),"-map","0","-c:v","libx264","-preset","veryfast","-crf","18","-c:a","aac","-b:a","192k","-map_metadata","0"};
    if(original.suffix()=="mp4")args<<"-movflags"<<"+faststart";
    args<<trimTemp_;setState("editing");trimProcess_.start(luma::RecordingSession::ffmpegPath(),args);
}
