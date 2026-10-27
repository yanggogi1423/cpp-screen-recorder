#include "core/RecordingSession.h"
#include "core/WaveWriter.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDataStream>
#include <QFile>
#include <iostream>

void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    try{
        luma::Settings s;s.outputFolder="test";s.validate();
        for(const auto fps:{0,241}){auto invalid=s;invalid.fps=fps;bool threw=false;try{invalid.validate();}catch(...){threw=true;}require(threw,"invalid FPS accepted");}
        s.width=1919;bool rejected=false;try{s.validate();}catch(...){rejected=true;}require(rejected,"odd width accepted");s.width=1920;
        const auto args=luma::RecordingSession::videoArguments(s,QSize(2560,1440),"file with space.mkv");
        require(args.contains("2560x1440")&&args.last()=="file with space.mkv","video input/output argument mismatch");
        luma::AudioMix mix;luma::AudioPacket packet{QByteArray(8,0),0};qToLittleEndian<qint16>(10000,packet.pcm.data());
        mix.gain=0.5f;luma::applyMix(packet,mix);require(qFromLittleEndian<qint16>(packet.pcm.constData())==5000,"live gain failed");
        mix.muted=true;luma::applyMix(packet,mix);require(packet.pcm==QByteArray(8,0),"live mute failed");
        mix.muted=false;mix.gain=2.0f;qToLittleEndian<qint16>(30000,packet.pcm.data());luma::applyMix(packet,mix);require(qFromLittleEndian<qint16>(packet.pcm.constData())==32767,"gain clipping overflow");
        QTemporaryDir dir;const auto file=dir.filePath("audio.wav");
        const auto origin=luma::clockNs();
        {
            luma::WaveWriter writer(file);
            QByteArray samples(480*4,'\x31');writer.append({samples,origin+10000000},origin);
            require(writer.frames()==960,"silence gap or packet size incorrect");
            writer.append({samples,origin+10000000},origin);require(writer.frames()==960,"overlap duplicated");
            writer.finish(30000000);
        }
        QFile wave(file);require(wave.open(QIODevice::ReadOnly),"WAV missing");auto bytes=wave.readAll();
        require(bytes.left(4)=="RF64"&&bytes.size()==80+1440*4,"RF64 header or tail duration incorrect");
        require(bytes.mid(80,480*4)==QByteArray(480*4,0),"leading silence incorrect");
        QDataStream header(bytes.mid(20,24));header.setByteOrder(QDataStream::LittleEndian);quint64 riff,data,frames;header>>riff>>data>>frames;
        require(riff==quint64(bytes.size()-8)&&data==1440*4&&frames==1440,"RF64 ds64 lengths incorrect");
        std::cout<<"PASS: settings, encoder arguments, RF64, timestamp gaps, overlap, silent tail\n";return 0;
    }catch(const std::exception& ex){std::cerr<<ex.what()<<"\n";return 1;}
}
