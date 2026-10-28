#include "WindowsGpuEncoder.h"
#include <d3d10.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mftransform.h>
#include <codecapi.h>
#include <QDir>

namespace luma {
namespace {
using Microsoft::WRL::ComPtr;
void requireHr(HRESULT result) {
    if (FAILED(result))
        throw std::runtime_error(
            QString("GPU 인코더 오류 0x%1").arg(quint32(result), 8, 16, QChar('0')).toStdString());
}
class Foundation {
    HRESULT apartment_;

  public:
    Foundation() : apartment_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {
        if (FAILED(apartment_) && apartment_ != RPC_E_CHANGED_MODE)
            requireHr(apartment_);
        requireHr(MFStartup(MF_VERSION));
    }
    ~Foundation() {
        MFShutdown();
        if (SUCCEEDED(apartment_))
            CoUninitialize();
    }
};
class Encoder final : public NativeVideoEncoder {
    Foundation foundation_;
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11VideoDevice> videoDevice_;
    ComPtr<ID3D11VideoContext> videoContext_;
    ComPtr<ID3D11VideoProcessorEnumerator> enumerator_;
    ComPtr<ID3D11VideoProcessor> processor_;
    ComPtr<IMFDXGIDeviceManager> manager_;
    ComPtr<IMFSinkWriter> writer_;
    WindowsFrameSource source_;
    Settings settings_;
    QSize inputSize_;
    DWORD stream_ = 0;
    bool finished_ = false;
    void configureProcessor(QSize input) {
        D3D11_VIDEO_PROCESSOR_CONTENT_DESC desc{};
        desc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
        desc.InputFrameRate = {UINT(settings_.fps), 1};
        desc.OutputFrameRate = desc.InputFrameRate;
        desc.InputWidth = UINT(input.width());
        desc.InputHeight = UINT(input.height());
        desc.OutputWidth = UINT(settings_.width);
        desc.OutputHeight = UINT(settings_.height);
        desc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
        processor_.Reset();
        enumerator_.Reset();
        requireHr(videoDevice_->CreateVideoProcessorEnumerator(&desc, &enumerator_));
        requireHr(videoDevice_->CreateVideoProcessor(enumerator_.Get(), 0, &processor_));
        inputSize_ = input;
    }

  public:
    ~Encoder() override {
        if (writer_ && !finished_)
            writer_->Finalize();
    }
    Encoder(ID3D11Device *device, WindowsFrameSource source, const Settings &settings,
            const QString &file)
        : device_(device), source_(std::move(source)), settings_(settings) {
        device_->GetImmediateContext(&context_);
        requireHr(device_.As(&videoDevice_));
        requireHr(context_.As(&videoContext_));
        ComPtr<ID3D10Multithread> protection;
        requireHr(context_.As(&protection));
        protection->SetMultithreadProtected(TRUE);
        UINT reset = 0;
        requireHr(MFCreateDXGIDeviceManager(&reset, &manager_));
        requireHr(manager_->ResetDevice(device, reset));
        ComPtr<IMFAttributes> attributes;
        requireHr(MFCreateAttributes(&attributes, 5));
        requireHr(attributes->SetUnknown(MF_SINK_WRITER_D3D_MANAGER, manager_.Get()));
        requireHr(attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE));
        requireHr(attributes->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE));
        requireHr(attributes->SetUINT32(MF_LOW_LATENCY, TRUE));
        requireHr(MFCreateSinkWriterFromURL(reinterpret_cast<LPCWSTR>(file.utf16()), nullptr,
                                            attributes.Get(), &writer_));
        auto mediaType = [&](GUID format) {
            ComPtr<IMFMediaType> type;
            requireHr(MFCreateMediaType(&type));
            requireHr(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
            requireHr(type->SetGUID(MF_MT_SUBTYPE, format));
            requireHr(type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
            requireHr(
                MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, settings.width, settings.height));
            requireHr(MFSetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, settings.fps, 1));
            requireHr(MFSetAttributeRatio(type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
            return type;
        };
        auto output = mediaType(MFVideoFormat_H264);
        const auto bitrate = std::clamp<qint64>(
            qint64(settings.width) * settings.height * settings.fps / 8, 1000000, 150000000);
        requireHr(output->SetUINT32(MF_MT_AVG_BITRATE, UINT32(bitrate)));
        requireHr(output->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_High));
        requireHr(writer_->AddStream(output.Get(), &stream_));
        auto input = mediaType(MFVideoFormat_NV12);
        requireHr(writer_->SetInputMediaType(stream_, input.Get(), nullptr));
        requireHr(writer_->BeginWriting());
        // Enabling hardware alone permits a silent software fallback. Reject it.
        ComPtr<IMFSinkWriterEx> extended;
        requireHr(writer_.As(&extended));
        bool hardware = false;
        for (DWORD index = 0; index < 16; ++index) {
            GUID category{};
            ComPtr<IMFTransform> transform;
            if (FAILED(extended->GetTransformForStream(stream_, index, &category, &transform)))
                break;
            if (category == MFT_CATEGORY_VIDEO_ENCODER) {
                ComPtr<IMFAttributes> info;
                UINT length = 0;
                if (SUCCEEDED(transform->GetAttributes(&info)) &&
                    SUCCEEDED(info->GetStringLength(MFT_ENUM_HARDWARE_URL_Attribute, &length)) &&
                    length)
                    hardware = true;
            }
        }
        if (!hardware)
            throw std::runtime_error(
                "D3D11 하드웨어 H.264 인코더가 없습니다. CPU 인코더를 선택해 주세요.");
    }
    void append(qint64 index) override {
        source_([&](const WindowsGpuFrame &frame) {
            if (!frame.texture)
                throw std::runtime_error("GPU 캡처 프레임이 없습니다.");
            D3D11_TEXTURE2D_DESC input{};
            frame.texture->GetDesc(&input);
            const QSize size(int(input.Width), int(input.Height));
            if (inputSize_ != size)
                configureProcessor(size);
            D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputViewDesc{};
            inputViewDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
            ComPtr<ID3D11VideoProcessorInputView> inputView;
            requireHr(videoDevice_->CreateVideoProcessorInputView(
                frame.texture.Get(), enumerator_.Get(), &inputViewDesc, &inputView));
            D3D11_TEXTURE2D_DESC textureDesc{};
            textureDesc.Width = settings_.width;
            textureDesc.Height = settings_.height;
            textureDesc.MipLevels = 1;
            textureDesc.ArraySize = 1;
            textureDesc.Format = DXGI_FORMAT_NV12;
            textureDesc.SampleDesc.Count = 1;
            textureDesc.Usage = D3D11_USAGE_DEFAULT;
            textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
            ComPtr<ID3D11Texture2D> converted;
            requireHr(device_->CreateTexture2D(&textureDesc, nullptr, &converted));
            D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputViewDesc{};
            outputViewDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
            ComPtr<ID3D11VideoProcessorOutputView> outputView;
            requireHr(videoDevice_->CreateVideoProcessorOutputView(
                converted.Get(), enumerator_.Get(), &outputViewDesc, &outputView));
            QRect source = frame.crop.isEmpty() ? QRect(QPoint(), frame.content) : frame.crop;
            source = source.intersected(QRect(QPoint(), size));
            if (source.isEmpty())
                throw std::runtime_error("녹화 대상이 사라졌습니다.");
            const auto scaled =
                source.size().scaled(settings_.width, settings_.height, Qt::KeepAspectRatio);
            const RECT sourceRect{source.x(), source.y(), source.x() + source.width(),
                                  source.y() + source.height()};
            const LONG x = (settings_.width - scaled.width()) / 2,
                       y = (settings_.height - scaled.height()) / 2;
            const RECT destRect{x, y, x + scaled.width(), y + scaled.height()};
            const RECT targetRect{0, 0, settings_.width, settings_.height};
            D3D11_VIDEO_COLOR background{};
            background.RGBA.A = 1.0f;
            videoContext_->VideoProcessorSetOutputBackgroundColor(processor_.Get(), FALSE,
                                                                  &background);
            videoContext_->VideoProcessorSetOutputTargetRect(processor_.Get(), TRUE, &targetRect);
            videoContext_->VideoProcessorSetStreamSourceRect(processor_.Get(), 0, TRUE,
                                                             &sourceRect);
            videoContext_->VideoProcessorSetStreamDestRect(processor_.Get(), 0, TRUE, &destRect);
            videoContext_->VideoProcessorSetStreamFrameFormat(processor_.Get(), 0,
                                                              D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
            D3D11_VIDEO_PROCESSOR_STREAM stream{};
            stream.Enable = TRUE;
            stream.pInputSurface = inputView.Get();
            requireHr(videoContext_->VideoProcessorBlt(processor_.Get(), outputView.Get(), 0, 1,
                                                       &stream));
            context_->Flush();
            ComPtr<IMFMediaBuffer> buffer;
            requireHr(MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), converted.Get(), 0,
                                                FALSE, &buffer));
            ComPtr<IMF2DBuffer> twoDimensional;
            requireHr(buffer.As(&twoDimensional));
            DWORD length = 0;
            requireHr(twoDimensional->GetContiguousLength(&length));
            requireHr(buffer->SetCurrentLength(length));
            ComPtr<IMFSample> sample;
            requireHr(MFCreateSample(&sample));
            requireHr(sample->AddBuffer(buffer.Get()));
            const auto start = index * 10000000LL / settings_.fps;
            const auto end = (index + 1) * 10000000LL / settings_.fps;
            requireHr(sample->SetSampleTime(start));
            requireHr(sample->SetSampleDuration(end - start));
            requireHr(writer_->WriteSample(stream_, sample.Get()));
        });
    }
    void finish() override {
        if (!finished_) {
            requireHr(writer_->Finalize());
            finished_ = true;
        }
    }
};
} // namespace
std::unique_ptr<NativeVideoEncoder> createWindowsGpuEncoder(ID3D11Device *device,
                                                            WindowsFrameSource source,
                                                            const Settings &settings,
                                                            const QString &file) {
    return std::make_unique<Encoder>(device, std::move(source), settings, file);
}
void probeNativeVideoEncoder(const Settings &settings, const QString &file) {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    requireHr(
        D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                          D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
                          nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context));
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = settings.width;
    desc.Height = settings.height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> texture;
    requireHr(device->CreateTexture2D(&desc, nullptr, &texture));
    ComPtr<ID3D11RenderTargetView> view;
    requireHr(device->CreateRenderTargetView(texture.Get(), nullptr, &view));
    const float color[]{0.1f, 0.1f, 0.1f, 1.0f};
    context->ClearRenderTargetView(view.Get(), color);
    auto encoder = createWindowsGpuEncoder(
        device.Get(),
        [=](const auto &consume) {
            consume({texture, {}, QSize(settings.width, settings.height)});
        },
        settings, file);
    for (int i = 0; i < 3; ++i)
        encoder->append(i);
    encoder->finish();
}
} // namespace luma
