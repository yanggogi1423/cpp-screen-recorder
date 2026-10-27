#include "RecordingSession.h"
#include "WaveWriter.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QUuid>
#include <QPainter>
#include <vector>

namespace luma {
QString RecordingSession::ffmpegPath(){
#ifdef Q_OS_WIN
    const QString exe="ffmpeg.exe";
#else
    const QString exe="ffmpeg";
#endif
    QDir dir(QCoreApplication::applicationDirPath());
    for(int i=0;i<8;i++){
        for(const auto& rel:QStringList{exe,"tools/ffmpeg/bin/"+exe,"../Resources/"+exe}){
            const auto path=dir.absoluteFilePath(rel);if(QFileInfo::exists(path))return QDir::cleanPath(path);
        }
        if(!dir.cdUp())break;
    }
    return {};
}
QStringList RecordingSession::videoArguments(const Settings& s,QSize input,const QString& file){
    QStringList args={"-hide_banner","-y","-f","rawvideo","-pixel_format","bgra","-video_size",QString("%1x%2").arg(input.width()).arg(input.height()),
        "-framerate",QString::number(s.fps),"-i","pipe:0","-an","-vf",
        QString("scale=%1:%2:force_original_aspect_ratio=decrease:flags=bicubic,pad=%1:%2:(ow-iw)/2:(oh-ih)/2,setsar=1").arg(s.width).arg(s.height),"-c:v",s.encoder};
    if(s.encoder=="libx264")args<<"-preset"<<"veryfast"<<"-crf"<<QString::number(s.quality);
    else if(s.encoder=="h264_nvenc")args<<"-preset"<<"p4"<<"-rc"<<"vbr"<<"-cq"<<QString::number(s.quality)<<"-b:v"<<"0";
    else if(s.encoder=="h264_qsv")args<<"-global_quality"<<QString::number(s.quality);
    else if(s.encoder=="h264_amf")args<<"-rc"<<"cqp"<<"-qp_i"<<QString::number(s.quality)<<"-qp_p"<<QString::number(s.quality);
    else args<<"-b:v"<<QString::number(qint64(s.width)*s.height*s.fps/8);
    return args<<"-pix_fmt"<<"yuv420p"<<"-g"<<QString::number(s.fps*2)<<file;
}
RecordingSession::~RecordingSession(){stop_=true;if(thread_.joinable())thread_.join();}
void RecordingSession::start(Platform* platform,VideoCapture* video,const Settings& settings,const QList<AudioSource>& sources){
    settings.validate();if(thread_.joinable())thread_.join();stop_=false;frames_=0;
    thread_=std::jthread([=,this]{run(platform,video,settings,sources);});
}
void RecordingSession::run(Platform* platform,VideoCapture* video,Settings s,QList<AudioSource> sources){
    QString folder;
    std::vector<std::unique_ptr<AudioCapture>> captures;
    std::vector<std::unique_ptr<WaveWriter>> writers;
    QProcess encoder;
    QMutex errorMutex;QString audioError;
    const auto stopCaptures=[&]{for(auto& capture:captures)capture->stop();};
    try{
        const auto ffmpeg=ffmpegPath();if(ffmpeg.isEmpty())throw std::runtime_error("FFmpeg가 없습니다. tools/setup-ffmpeg.ps1을 먼저 실행해 주세요.");
        folder=QDir(s.outputFolder).filePath("Luma_"+QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss")+"_"+QUuid::createUuid().toString(QUuid::Id128).left(4));
        if(!QDir().mkpath(folder))throw std::runtime_error("녹화 폴더를 만들지 못했습니다.");
        auto first=video->latestFrame();if(first.isNull())throw std::runtime_error("미리보기가 준비된 뒤 녹화를 시작해 주세요.");
        const auto inputSize=first.size();
        const auto rawVideo=QDir(folder).filePath("video.mkv");
        QFile log(QDir(folder).filePath("encoder.log"));log.open(QIODevice::WriteOnly);
        encoder.setProgram(ffmpeg);encoder.setArguments(videoArguments(s,inputSize,rawVideo));encoder.start();
        if(!encoder.waitForStarted(5000))throw std::runtime_error(encoder.errorString().toStdString());
        QJsonArray trackInfo;
        const qint64 origin=clockNs();
        for(qsizetype i=0;i<sources.size();i++){
            const auto& source=sources[i];
            auto safe=source.name.left(60);safe.replace(QRegularExpression("[\\\\/:*?\"<>|]"),"_");
            const auto file=QDir(folder).filePath(QString("%1_%2.wav").arg(i+1,2,10,QChar('0')).arg(safe));
            writers.push_back(std::make_unique<WaveWriter>(file));auto* writer=writers.back().get();
            auto capture=platform->createAudioCapture();
            // Register ownership before starting so all partial failures are cleaned up.
            captures.push_back(std::move(capture));
            captures.back()->start(source,[&,writer,origin,mix=source.mix](AudioPacket packet){
                try{applyMix(packet,*mix);writer->append(packet,origin);}catch(const std::exception& ex){QMutexLocker lock(&errorMutex);audioError=QString::fromUtf8(ex.what());}
            },[&](QString err){QMutexLocker lock(&errorMutex);audioError=err;});
            trackInfo.append(QJsonObject{{"name",source.name},{"file",file},{"sourceId",source.id}});
        }
        emit started(folder);
        QByteArray tail;
        QString warning;
        qint64 duplicateFrames=0;
        QImage previous;
        while(!stop_){
            {QMutexLocker lock(&errorMutex);if(!audioError.isEmpty()){warning=audioError;break;}}
            const auto elapsed=clockNs()-origin;
            const auto due=elapsed*s.fps/1000000000;
            if(frames_>due){std::this_thread::sleep_for(std::chrono::milliseconds(1));continue;}
            auto image=video->latestFrame();
            if(image.isNull()){warning="녹화할 화면이 사라졌습니다.";break;}
            if(image.cacheKey()==previous.cacheKey())duplicateFrames++;
            previous=image;
            if(image.size()!=inputSize)image=image.scaled(inputSize,Qt::KeepAspectRatio,Qt::FastTransformation);
            if(image.size()!=inputSize){QImage canvas(inputSize,QImage::Format_ARGB32);canvas.fill(Qt::black);QPainter painter(&canvas);painter.drawImage(0,0,image);painter.end();image=canvas;}
            if(image.format()!=QImage::Format_ARGB32)image=image.convertToFormat(QImage::Format_ARGB32);
            const auto bytes=image.sizeInBytes();
            if(encoder.write(reinterpret_cast<const char*>(image.constBits()),bytes)!=bytes)throw std::runtime_error("인코더로 영상을 전달하지 못했습니다.");
            while(encoder.bytesToWrite()>0){
                if(!encoder.waitForBytesWritten(2000))throw std::runtime_error(("인코더가 영상을 처리하지 못했습니다.\n"+QString::fromUtf8(tail)).toStdString());
                const auto line=encoder.readAllStandardError();log.write(line);tail=(tail+line).right(5000);
            }
            frames_++;
            const auto line=encoder.readAllStandardError();log.write(line);tail=(tail+line).right(5000);
            if(encoder.state()==QProcess::NotRunning)throw std::runtime_error(("인코더가 종료됐습니다.\n"+QString::fromUtf8(tail)).toStdString());
            if(clockNs()-origin-frames_.load()*1000000000/s.fps>5000000000LL){warning="인코딩 속도가 부족합니다. GPU 인코더를 선택하거나 해상도·FPS를 낮춰 주세요.";break;}
        }
        emit finalizing();
        stopCaptures();
        const auto duration=frames_.load()*1000000000/s.fps;
        for(auto& writer:writers)writer->finish(duration);
        encoder.closeWriteChannel();
        if(!encoder.waitForFinished(30000)){encoder.kill();encoder.waitForFinished();throw std::runtime_error("영상 저장 종료 시간이 초과됐습니다. 원본 파일을 보관했습니다.");}
        log.write(encoder.readAllStandardError());log.close();
        if(encoder.exitCode()!=0 || frames_==0)throw std::runtime_error(("영상 저장에 실패했습니다. encoder.log를 확인해 주세요.\n"+QString::fromUtf8(tail)).toStdString());
        const auto output=QDir(folder).filePath("recording."+s.container);
        QStringList args={"-hide_banner","-y","-i",rawVideo};
        for(const auto& entry:trackInfo)args<<"-i"<<entry.toObject()["file"].toString();
        args<<"-map"<<"0:v:0";
        for(qsizetype i=0;i<sources.size();i++)args<<"-map"<<QString("%1:a:0").arg(i+1)<<QString("-metadata:s:a:%1").arg(i)<<"title="+sources[i].name<<QString("-filter:a:%1").arg(i)<<"apad";
        args<<"-c:v"<<"copy"<<"-c:a"<<"aac"<<"-b:a"<<"192k"<<"-t"<<QString::number(duration/1e9,'f',6);
        if(s.container=="mp4")args<<"-movflags"<<"+faststart";
        args<<output;
        QProcess mux;mux.start(ffmpeg,args);
        if(!mux.waitForStarted(5000))throw std::runtime_error("오디오 트랙 결합을 시작하지 못했습니다.");
        if(!mux.waitForFinished(300000)){mux.kill();mux.waitForFinished();throw std::runtime_error("트랙 결합 시간이 초과됐습니다. 원본을 보관했습니다.");}
        QFile muxLog(QDir(folder).filePath("mux.log"));if(muxLog.open(QIODevice::WriteOnly))muxLog.write(mux.readAllStandardError());
        if(mux.exitCode()!=0)throw std::runtime_error("트랙 결합에 실패했습니다. video.mkv와 WAV 원본은 녹화 폴더에 있습니다.");
        QFile manifest(QDir(folder).filePath("session.json"));
        if(manifest.open(QIODevice::WriteOnly))manifest.write(QJsonDocument(QJsonObject{
            {"version","0.1.0"},{"width",s.width},{"height",s.height},{"fps",s.fps},{"frames",double(frames_)},
            {"duplicateFrames",double(duplicateFrames)},{"durationSeconds",duration/1e9},{"tracks",trackInfo},{"warning",warning}
        }).toJson());
        emit finished(output,warning);
    }catch(const std::exception& ex){
        stopCaptures();
        if(encoder.state()!=QProcess::NotRunning){encoder.closeWriteChannel();if(!encoder.waitForFinished(5000)){encoder.kill();encoder.waitForFinished();}}
        emit failed(QString::fromUtf8(ex.what()),folder);
    }
}
}
