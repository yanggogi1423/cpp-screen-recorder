#include "core/Platform.h"
#import <AVFoundation/AVFoundation.h>
#import <AppKit/AppKit.h>
#import <CoreMedia/CoreMedia.h>
#include <QMutex>
#include <QMutexLocker>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#include <future>
#include <mutex>
#include <unistd.h>

// Objective-C++ stays behind the same C++ interface as the Windows backend.
@interface LumaStreamOutput : NSObject <SCStreamOutput, SCStreamDelegate> {
  @public
    std::function<void(CMSampleBufferRef, SCStreamOutputType)> onSample;
    luma::ErrorCallback onError;
}
@end
@implementation LumaStreamOutput
- (void)stream:(SCStream *)stream
    didOutputSampleBuffer:(CMSampleBufferRef)sample
                   ofType:(SCStreamOutputType)type {
    if (onSample)
        onSample(sample, type);
}
- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error {
    if (onError)
        onError(QString::fromUtf8(error.localizedDescription.UTF8String));
}
@end

namespace luma {
namespace {
QString text(NSString *value) {
    return value ? QString::fromUtf8(value.UTF8String) : QString();
}
void require(bool ok, NSString *message) {
    if (!ok)
        throw std::runtime_error(text(message).toStdString());
}
SCShareableContent *content() {
    __block SCShareableContent *result = nil;
    __block NSError *failure = nil;
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    [SCShareableContent
        getShareableContentExcludingDesktopWindows:YES
                               onScreenWindowsOnly:YES
                                 completionHandler:^(SCShareableContent *value, NSError *error) {
                                   result = value;
                                   failure = error;
                                   dispatch_semaphore_signal(done);
                                 }];
    if (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 15 * NSEC_PER_SEC)))
        throw std::runtime_error("화면 녹화 권한을 허용한 뒤 앱으로 돌아와 주세요.");
    require(result != nil, failure.localizedDescription ?: @"화면 녹화 권한이 필요합니다.");
    return result;
}
void startStream(SCStream *stream) {
    __block NSError *failure = nil;
    auto done = dispatch_semaphore_create(0);
    [stream startCaptureWithCompletionHandler:^(NSError *err) {
      failure = err;
      dispatch_semaphore_signal(done);
    }];
    if (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 15 * NSEC_PER_SEC)))
        throw std::runtime_error("캡처 시작 시간이 초과됐습니다.");
    require(!failure, failure.localizedDescription);
}
void stopStream(SCStream *stream) {
    if (!stream)
        return;
    auto done = dispatch_semaphore_create(0);
    [stream stopCaptureWithCompletionHandler:^(NSError *) {
      dispatch_semaphore_signal(done);
    }];
    dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));
}
SCContentFilter *filterFor(SCShareableContent *available, const Source &source) {
    if (source.kind == SourceKind::Window) {
        for (SCWindow *window in available.windows)
            if (window.windowID == source.id.toUInt())
                return [[SCContentFilter alloc] initWithDesktopIndependentWindow:window];
    } else
        for (SCDisplay *display in available.displays)
            if (display.displayID == source.id.toUInt())
                return [[SCContentFilter alloc] initWithDisplay:display excludingWindows:@[]];
    throw std::runtime_error("선택한 캡처 소스가 사라졌습니다.");
}
class MacVideo final : public VideoCapture {
    SCStream *stream_ = nil;
    SCStreamConfiguration *config_ = nil;
    LumaStreamOutput *output_ = nil;
    dispatch_queue_t queue_ = dispatch_queue_create("studio.luma.video", DISPATCH_QUEUE_SERIAL);
    std::mutex mutex_;
    QImage image_;
    QImage fullFrame_;
    QRect crop_;

  public:
    ~MacVideo() override {
        stop();
    }
    void start(const Source &source, const QRect &crop, bool cursor, ErrorCallback error) override {
        stop();
        crop_ = crop;
        auto available = content();
        auto filter = filterFor(available, source);
        auto config = [[SCStreamConfiguration alloc] init];
        config_ = config;
        config.width = source.bounds.width();
        config.height = source.bounds.height();
        config.pixelFormat = kCVPixelFormatType_32BGRA;
        config.minimumFrameInterval = CMTimeMake(1, 240);
        config.queueDepth = 3;
        config.showsCursor = cursor;
        output_ = [[LumaStreamOutput alloc] init];
        output_->onError = std::move(error);
        output_->onSample = [this](CMSampleBufferRef sample, SCStreamOutputType type) {
            if (type != SCStreamOutputTypeScreen || !CMSampleBufferIsValid(sample))
                return;
            auto pixel = CMSampleBufferGetImageBuffer(sample);
            if (!pixel)
                return;
            CVPixelBufferLockBaseAddress(pixel, kCVPixelBufferLock_ReadOnly);
            QImage view(static_cast<const uchar *>(CVPixelBufferGetBaseAddress(pixel)),
                        int(CVPixelBufferGetWidth(pixel)), int(CVPixelBufferGetHeight(pixel)),
                        qsizetype(CVPixelBufferGetBytesPerRow(pixel)), QImage::Format_ARGB32);
            std::lock_guard lock(mutex_);
            fullFrame_ = view.copy();
            image_ = crop_.isEmpty() ? fullFrame_ : fullFrame_.copy(crop_);
            CVPixelBufferUnlockBaseAddress(pixel, kCVPixelBufferLock_ReadOnly);
        };
        stream_ = [[SCStream alloc] initWithFilter:filter configuration:config delegate:output_];
        NSError *failure = nil;
        require([stream_ addStreamOutput:output_
                                    type:SCStreamOutputTypeScreen
                      sampleHandlerQueue:queue_
                                   error:&failure],
                failure.localizedDescription);
        startStream(stream_);
    }
    QImage latestFrame() override {
        std::lock_guard lock(mutex_);
        return image_;
    }
    void setRegion(const QRect &region) override {
        std::lock_guard lock(mutex_);
        if (crop_.isEmpty() || region.size() != crop_.size() || region.x() < 0 || region.y() < 0)
            throw std::runtime_error("녹화 중에는 영역 크기를 유지해야 합니다.");
        crop_ = region;
        if (!fullFrame_.isNull())
            image_ = fullFrame_.copy(crop_);
    }
    void setCursor(bool enabled) override {
        if (stream_) {
            config_.showsCursor = enabled;
            [stream_ updateConfiguration:config_
                       completionHandler:^(NSError *){
                       }];
        }
    }
    void stop() override {
        stopStream(stream_);
        dispatch_sync(queue_, ^{
                      });
        if (output_) {
            output_->onSample = {};
            output_->onError = {};
        }
        stream_ = nil;
        output_ = nil;
        std::lock_guard lock(mutex_);
        image_ = {};
        fullFrame_ = {};
    }
};

class MacAudio final : public AudioCapture {
    SCStream *stream_ = nil;
    AVAudioEngine *engine_ = nil;
    LumaStreamOutput *output_ = nil;
    dispatch_queue_t queue_ = dispatch_queue_create("studio.luma.audio", DISPATCH_QUEUE_SERIAL);
    AVAudioConverter *converter_ = nil;
    AVAudioFormat *target_ = [[AVAudioFormat alloc] initWithCommonFormat:AVAudioPCMFormatInt16
                                                              sampleRate:48000
                                                                channels:2
                                                             interleaved:YES];
    AudioCallback data_;
    ErrorCallback error_;
    qint64 nextNs_ = 0;
    void convert(AVAudioPCMBuffer *input, qint64 timestamp) {
        if (!input || input.frameLength == 0)
            return;
        if (!converter_)
            converter_ = [[AVAudioConverter alloc] initFromFormat:input.format toFormat:target_];
        const auto frames =
            AVAudioFrameCount(input.frameLength * 48000.0 / input.format.sampleRate + 64);
        auto converted = [[AVAudioPCMBuffer alloc] initWithPCMFormat:target_ frameCapacity:frames];
        __block bool supplied = false;
        NSError *failure = nil;
        [converter_ convertToBuffer:converted
                              error:&failure
                 withInputFromBlock:^AVAudioBuffer *(AVAudioPacketCount,
                                                     AVAudioConverterInputStatus *status) {
                   if (supplied) {
                       *status = AVAudioConverterInputStatus_NoDataNow;
                       return nil;
                   }
                   supplied = true;
                   *status = AVAudioConverterInputStatus_HaveData;
                   return input;
                 }];
        if (failure) {
            error_(text(failure.localizedDescription));
            return;
        }
        const auto *list = converted.audioBufferList;
        AudioPacket packet{QByteArray(static_cast<const char *>(list->mBuffers[0].mData),
                                      converted.frameLength * 4),
                           timestamp};
        data_(std::move(packet));
    }

  public:
    ~MacAudio() override {
        stop();
    }
    void start(const AudioSource &source, AudioCallback data, ErrorCallback error) override {
        stop();
        data_ = std::move(data);
        error_ = std::move(error);
        converter_ = nil;
        if (source.kind == AudioKind::Microphone) {
            const auto status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
            if (status == AVAuthorizationStatusNotDetermined) {
                auto done = dispatch_semaphore_create(0);
                __block BOOL allowed = NO;
                [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
                                         completionHandler:^(BOOL granted) {
                                           allowed = granted;
                                           dispatch_semaphore_signal(done);
                                         }];
                dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC));
                require(allowed, @"마이크 권한을 허용해 주세요.");
            } else
                require(status == AVAuthorizationStatusAuthorized,
                        @"시스템 설정에서 Luma의 마이크 권한을 허용해 주세요.");
            engine_ = [[AVAudioEngine alloc] init];
            auto input = engine_.inputNode;
            auto format = [input outputFormatForBus:0];
            [input installTapOnBus:0
                        bufferSize:1024
                            format:format
                             block:^(AVAudioPCMBuffer *buffer, AVAudioTime *time) {
                               const auto packetNs =
                                   clockNs() - qint64(buffer.frameLength / format.sampleRate * 1e9);
                               convert(buffer, packetNs);
                             }];
            NSError *failure = nil;
            require([engine_ startAndReturnError:&failure], failure.localizedDescription);
            return;
        }
        auto available = content();
        require(available.displays.count > 0, @"녹화 가능한 화면이 없습니다.");
        SCContentFilter *filter = nil;
        if (source.kind == AudioKind::Application) {
            SCRunningApplication *app = nil;
            for (SCRunningApplication *entry in available.applications)
                if (entry.processID == source.processId) {
                    app = entry;
                    break;
                }
            require(app != nil, @"선택한 프로그램이 종료됐습니다.");
            filter = [[SCContentFilter alloc] initWithDisplay:available.displays.firstObject
                                        includingApplications:@[ app ]
                                             exceptingWindows:@[]];
        } else
            filter = [[SCContentFilter alloc] initWithDisplay:available.displays.firstObject
                                        excludingApplications:@[]
                                             exceptingWindows:@[]];
        auto config = [[SCStreamConfiguration alloc] init];
        config.capturesAudio = YES;
        config.sampleRate = 48000;
        config.channelCount = 2;
        config.excludesCurrentProcessAudio = YES;
        config.width = 2;
        config.height = 2;
        config.minimumFrameInterval = CMTimeMake(1, 1);
        output_ = [[LumaStreamOutput alloc] init];
        output_->onError = error_;
        output_->onSample = [this](CMSampleBufferRef sample, SCStreamOutputType type) {
            if (type != SCStreamOutputTypeAudio || !CMSampleBufferDataIsReady(sample))
                return;
            auto desc = CMSampleBufferGetFormatDescription(sample);
            const auto *asbd = CMAudioFormatDescriptionGetStreamBasicDescription(desc);
            if (!asbd)
                return;
            auto format = [[AVAudioFormat alloc] initWithStreamDescription:asbd];
            auto count = AVAudioFrameCount(CMSampleBufferGetNumSamples(sample));
            auto buffer = [[AVAudioPCMBuffer alloc] initWithPCMFormat:format frameCapacity:count];
            buffer.frameLength = count;
            const auto status = CMSampleBufferCopyPCMDataIntoAudioBufferList(
                sample, 0, int32_t(count), buffer.mutableAudioBufferList);
            if (status != noErr) {
                error_("ScreenCaptureKit 오디오 버퍼 변환에 실패했습니다.");
                return;
            }
            // ScreenCaptureKit presentation times use the host clock. Correlate once per packet.
            const auto host = CMClockGetTime(CMClockGetHostTimeClock());
            const auto timestamp =
                clockNs() + qint64(CMTimeGetSeconds(CMTimeSubtract(
                                       CMSampleBufferGetPresentationTimeStamp(sample), host)) *
                                   1e9);
            convert(buffer, timestamp);
        };
        stream_ = [[SCStream alloc] initWithFilter:filter configuration:config delegate:output_];
        NSError *failure = nil;
        require([stream_ addStreamOutput:output_
                                    type:SCStreamOutputTypeAudio
                      sampleHandlerQueue:queue_
                                   error:&failure],
                failure.localizedDescription);
        startStream(stream_);
    }
    void stop() override {
        if (engine_) {
            [engine_.inputNode removeTapOnBus:0];
            [engine_ stop];
            engine_ = nil;
        }
        stopStream(stream_);
        dispatch_sync(queue_, ^{
                      });
        if (output_) {
            output_->onSample = {};
            output_->onError = {};
        }
        stream_ = nil;
        output_ = nil;
        converter_ = nil;
    }
};

class MacPlatform final : public Platform {
  public:
    QString name() const override {
        return "macOS";
    }
    QList<Source> sources() override {
        auto available = content();
        QList<Source> result;
        for (SCDisplay *display in available.displays) {
            const auto bounds = CGDisplayBounds(display.displayID);
            const int w = int(CGDisplayPixelsWide(display.displayID)),
                      h = int(CGDisplayPixelsHigh(display.displayID));
            result.append({QString::number(display.displayID),
                           QString("디스플레이 %1 · %2 × %3").arg(result.size() + 1).arg(w).arg(h),
                           SourceKind::Display,
                           QRect(int(bounds.origin.x), int(bounds.origin.y), w, h), 0,
                           QRect(int(bounds.origin.x), int(bounds.origin.y), int(bounds.size.width),
                                 int(bounds.size.height))});
        }
        for (SCWindow *window in available.windows) {
            if (window.owningApplication.processID == getpid() || window.frame.size.width < 32 ||
                window.frame.size.height < 32)
                continue;
            const auto title = text(window.title);
            if (title.isEmpty())
                continue;
            // Window dimensions are logical points; use its display's backing scale.
            double scale = 1.0;
            for (SCDisplay *display in available.displays)
                if (CGRectIntersectsRect(display.frame, window.frame)) {
                    scale =
                        double(CGDisplayPixelsWide(display.displayID)) / display.frame.size.width;
                    break;
                }
            result.append(
                {QString::number(window.windowID),
                 text(window.owningApplication.applicationName) + " · " + title, SourceKind::Window,
                 QRect(int(window.frame.origin.x), int(window.frame.origin.y),
                       int(window.frame.size.width * scale), int(window.frame.size.height * scale)),
                 quint32(window.owningApplication.processID)});
        }
        return result;
    }
    QList<AudioSource> audioSources() override {
        QList<AudioSource> result{
            {"default", "기본 마이크", "시스템 기본 입력 장치 · 독립 트랙", AudioKind::Microphone},
            {"system", "시스템 소리", "전체 앱 소리 · 앱 트랙과 중복될 수 있음",
             AudioKind::System}};
        auto available = content();
        for (SCRunningApplication *app in available.applications)
            if (app.processID != getpid())
                result.append({QString::number(app.processID), text(app.applicationName),
                               "앱 소리 · 독립 트랙", AudioKind::Application,
                               quint32(app.processID)});
        return result;
    }
    std::unique_ptr<VideoCapture> createVideoCapture() override {
        return std::make_unique<MacVideo>();
    }
    std::unique_ptr<AudioCapture> createAudioCapture() override {
        return std::make_unique<MacAudio>();
    }
};
} // namespace
std::unique_ptr<Platform> createPlatform() {
    return std::make_unique<MacPlatform>();
}
} // namespace luma
