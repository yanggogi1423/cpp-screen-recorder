#pragma once
#include "Types.h"
#include <memory>

namespace luma {
class VideoCapture {
public:
    virtual ~VideoCapture()=default;
    virtual void start(const Source& source,const QRect& region,bool cursor,ErrorCallback error)=0;
    virtual QImage latestFrame()=0;
    virtual void setCursor(bool enabled)=0;
    virtual void stop()=0;
};
class AudioCapture {
public:
    virtual ~AudioCapture()=default;
    virtual void start(const AudioSource& source,AudioCallback data,ErrorCallback error)=0;
    virtual void stop()=0;
};
// Platform code owns permissions, OS handles, device clocks and format conversion.
// Consumers receive immutable BGRA images and timestamped PCM in a shared clock domain.
class Platform {
public:
    virtual ~Platform()=default;
    virtual QList<Source> sources()=0;
    virtual QList<AudioSource> audioSources()=0;
    virtual std::unique_ptr<VideoCapture> createVideoCapture()=0;
    virtual std::unique_ptr<AudioCapture> createAudioCapture()=0;
    virtual QString name() const=0;
};
std::unique_ptr<Platform> createPlatform();
}
