#include "MacGpuEncoder.h"
#import <AVFoundation/AVFoundation.h>
#import <CoreImage/CoreImage.h>
#import <Metal/Metal.h>
#import <VideoToolbox/VideoToolbox.h>
#include <QElapsedTimer>
#include <thread>
namespace luma {
namespace {
void ensure(bool ok, NSString *message) {
    if (!ok)
        throw std::runtime_error(message ? message.UTF8String : "Apple GPU 인코더 오류");
}
class Encoder final : public NativeVideoEncoder {
    AVAssetWriter *writer_;
    AVAssetWriterInput *input_;
    AVAssetWriterInputPixelBufferAdaptor *adaptor_;
    CIContext *context_;
    MacFrameSource source_;
    Settings settings_;
    bool finished_ = false;

  public:
    Encoder(MacFrameSource source, const Settings &settings, const QString &file)
        : source_(std::move(source)), settings_(settings) {
        @autoreleasepool {
            id<MTLDevice> device = MTLCreateSystemDefaultDevice();
            ensure(device != nil, @"Metal GPU를 사용할 수 없습니다.");
            context_ = [CIContext contextWithMTLDevice:device
                                               options:@{
                                                   kCIContextUseSoftwareRenderer : @NO
                                               }];
            ensure(context_ != nil, @"Metal 영상 변환을 초기화하지 못했습니다.");
            NSError *error = nil;
            writer_ = [[AVAssetWriter alloc]
                initWithURL:[NSURL
                                fileURLWithPath:[NSString
                                                    stringWithUTF8String:file.toUtf8().constData()]]
                   fileType:AVFileTypeMPEG4
                      error:&error];
            ensure(writer_ != nil, error.localizedDescription);
            const auto bitrate = std::clamp<qint64>(
                qint64(settings.width) * settings.height * settings.fps / 8, 1000000, 150000000);
            NSDictionary *options = @{
                AVVideoCodecKey : AVVideoCodecTypeH264,
                AVVideoWidthKey : @(settings.width),
                AVVideoHeightKey : @(settings.height),
                AVVideoEncoderSpecificationKey : @{
                    (__bridge NSString *)
                    kVTVideoEncoderSpecification_RequireHardwareAcceleratedVideoEncoder : @YES
                },
                AVVideoCompressionPropertiesKey : @{
                    AVVideoAverageBitRateKey : @(bitrate),
                    AVVideoExpectedSourceFrameRateKey : @(settings.fps),
                    AVVideoMaxKeyFrameIntervalKey : @(settings.fps * 2),
                    AVVideoAllowFrameReorderingKey : @NO
                }
            };
            input_ = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo
                                                        outputSettings:options];
            input_.expectsMediaDataInRealTime = YES;
            adaptor_ = [AVAssetWriterInputPixelBufferAdaptor
                assetWriterInputPixelBufferAdaptorWithAssetWriterInput:input_
                                           sourcePixelBufferAttributes:@{
                                               (__bridge NSString *)
                                               kCVPixelBufferPixelFormatTypeKey :
                                                   @(kCVPixelFormatType_32BGRA),
                                               (__bridge NSString *)
                                               kCVPixelBufferWidthKey : @(settings.width),
                                               (__bridge NSString *)
                                               kCVPixelBufferHeightKey : @(settings.height),
                                               (__bridge NSString *)
                                               kCVPixelBufferIOSurfacePropertiesKey : @{},
                                               (__bridge NSString *)
                                               kCVPixelBufferMetalCompatibilityKey : @YES
                                           }];
            ensure([writer_ canAddInput:input_], @"H.264 하드웨어 인코더를 사용할 수 없습니다.");
            [writer_ addInput:input_];
            ensure([writer_ startWriting], writer_.error.localizedDescription);
            [writer_ startSessionAtSourceTime:kCMTimeZero];
        }
    }
    ~Encoder() override {
        if (!finished_)
            [writer_ cancelWriting];
    }
    void append(qint64 frame) override {
        @autoreleasepool {
            QElapsedTimer wait;
            wait.start();
            while (!input_.readyForMoreMediaData) {
                ensure(writer_.status == AVAssetWriterStatusWriting && wait.elapsed() < 5000,
                       writer_.error.localizedDescription ?: @"GPU 인코딩 시간이 초과됐습니다.");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            CVPixelBufferRef output = nullptr;
            ensure(adaptor_.pixelBufferPool &&
                       CVPixelBufferPoolCreatePixelBuffer(nullptr, adaptor_.pixelBufferPool,
                                                          &output) == kCVReturnSuccess,
                   @"GPU 프레임 버퍼를 만들지 못했습니다.");
            try {
                source_([&](CVPixelBufferRef pixel, QRect crop) {
                    ensure(pixel != nullptr, @"GPU 캡처 프레임이 없습니다.");
                    CIImage *image = [CIImage imageWithCVPixelBuffer:pixel];
                    const auto h = int(CVPixelBufferGetHeight(pixel));
                    if (crop.isEmpty())
                        crop = QRect(0, 0, int(CVPixelBufferGetWidth(pixel)), h);
                    CGRect rect = CGRectMake(crop.x(), h - crop.y() - crop.height(), crop.width(),
                                             crop.height());
                    image = [[image imageByCroppingToRect:rect]
                        imageByApplyingTransform:CGAffineTransformMakeTranslation(-rect.origin.x,
                                                                                  -rect.origin.y)];
                    const auto scale = std::min(double(settings_.width) / crop.width(),
                                                double(settings_.height) / crop.height());
                    image =
                        [image imageByApplyingTransform:CGAffineTransformMakeScale(scale, scale)];
                    image = [image
                        imageByApplyingTransform:CGAffineTransformMakeTranslation(
                                                     (settings_.width - crop.width() * scale) / 2,
                                                     (settings_.height - crop.height() * scale) /
                                                         2)];
                    image = [image
                        imageByCompositingOverImage:[CIImage
                                                        imageWithColor:[CIColor colorWithRed:0
                                                                                       green:0
                                                                                        blue:0
                                                                                       alpha:1]]];
                    CGColorSpaceRef color = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
                    [context_ render:image
                        toCVPixelBuffer:output
                                 bounds:CGRectMake(0, 0, settings_.width, settings_.height)
                             colorSpace:color];
                    CGColorSpaceRelease(color);
                });
                ensure([adaptor_ appendPixelBuffer:output
                              withPresentationTime:CMTimeMake(frame, settings_.fps)],
                       writer_.error.localizedDescription);
            } catch (...) {
                CVPixelBufferRelease(output);
                throw;
            }
            CVPixelBufferRelease(output);
        }
    }
    void finish() override {
        if (finished_)
            return;
        [input_ markAsFinished];
        auto done = dispatch_semaphore_create(0);
        [writer_ finishWritingWithCompletionHandler:^{
          dispatch_semaphore_signal(done);
        }];
        ensure(dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC)) ==
                   0,
               @"GPU 파일 마무리 시간이 초과됐습니다.");
        ensure(writer_.status == AVAssetWriterStatusCompleted, writer_.error.localizedDescription);
        finished_ = true;
    }
};
} // namespace
std::unique_ptr<NativeVideoEncoder>
createMacGpuEncoder(MacFrameSource source, const Settings &settings, const QString &file) {
    return std::make_unique<Encoder>(std::move(source), settings, file);
}
void probeNativeVideoEncoder(const Settings &settings, const QString &file) {
    CVPixelBufferRef pixel = nullptr;
    NSDictionary *attrs = @{
        (__bridge NSString *)kCVPixelBufferIOSurfacePropertiesKey : @{},
        (__bridge NSString *)kCVPixelBufferMetalCompatibilityKey : @YES
    };
    ensure(CVPixelBufferCreate(nullptr, settings.width, settings.height, kCVPixelFormatType_32BGRA,
                               (__bridge CFDictionaryRef)attrs, &pixel) == kCVReturnSuccess,
           @"GPU 검사 프레임 생성 실패");
    try {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        ensure(device != nil, @"Metal GPU를 사용할 수 없습니다.");
        CIContext *context = [CIContext contextWithMTLDevice:device];
        CGColorSpaceRef color = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        [context render:[CIImage imageWithColor:[CIColor colorWithRed:0 green:0 blue:0 alpha:1]]
            toCVPixelBuffer:pixel
                     bounds:CGRectMake(0, 0, settings.width, settings.height)
                 colorSpace:color];
        CGColorSpaceRelease(color);
        auto encoder =
            createMacGpuEncoder([=](const auto &consume) { consume(pixel, {}); }, settings, file);
        for (int i = 0; i < 3; ++i)
            encoder->append(i);
        encoder->finish();
    } catch (...) {
        CVPixelBufferRelease(pixel);
        throw;
    }
    CVPixelBufferRelease(pixel);
}
} // namespace luma
