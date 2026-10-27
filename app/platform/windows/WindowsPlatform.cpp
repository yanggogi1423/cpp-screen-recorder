#include "core/Platform.h"
#include <windows.h>
#include <mmdeviceapi.h>
#include <propkeydef.h>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <atomic>
#include <audioclient.h>
#include <audiopolicy.h>
#include <d3d11.h>
#include <dxgi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <future>
#include <propvarutil.h>
#include <thread>
#include <tlhelp32.h>
#include <vector>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/base.h>
#include <wrl.h>

using Microsoft::WRL::ComPtr;
using namespace winrt;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
namespace luma {
namespace {
void check(HRESULT hr) {
    if (FAILED(hr))
        throw std::runtime_error(
            QString("Windows 캡처 오류 0x%1").arg(quint32(hr), 8, 16, QChar('0')).toStdString());
}
struct Apartment {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Apartment() {
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
            check(hr);
    }
    ~Apartment() {
        if (SUCCEEDED(hr))
            CoUninitialize();
    }
};
QString processName(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return QString("프로그램 %1").arg(pid);
    wchar_t path[32768];
    DWORD count = 32768;
    QString name;
    if (QueryFullProcessImageNameW(process, 0, path, &count))
        name = QFileInfo(QString::fromWCharArray(path, int(count))).completeBaseName();
    CloseHandle(process);
    return name.isEmpty() ? QString("프로그램 %1").arg(pid) : name;
}
QString friendly(IMMDevice *device) {
    ComPtr<IPropertyStore> props;
    check(device->OpenPropertyStore(STGM_READ, &props));
    PROPVARIANT value;
    PropVariantInit(&value);
    check(props->GetValue(PKEY_Device_FriendlyName, &value));
    const auto name = QString::fromWCharArray(value.pwszVal);
    PropVariantClear(&value);
    return name;
}
QString deviceId(IMMDevice *device) {
    LPWSTR str = nullptr;
    check(device->GetId(&str));
    auto id = QString::fromWCharArray(str);
    CoTaskMemFree(str);
    return id;
}

class WinVideo final : public VideoCapture {
    std::mutex mutex_;
    std::mutex frameMutex_;
    QImage latest_;
    QRect crop_;
    QSize canvas_;
    std::atomic<bool> stopped_{true};
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11Texture2D> staging_;
    IDirect3DDevice rtDevice_{nullptr};
    Direct3D11CaptureFramePool pool_{nullptr};
    GraphicsCaptureSession session_{nullptr};
    GraphicsCaptureItem item_{nullptr};
    event_token frameToken_{}, closedToken_{};
    ErrorCallback error_;

  public:
    ~WinVideo() override {
        try {
            stop();
        } catch (...) {
        }
    }
    void start(const Source &source, const QRect &region, bool cursor, ErrorCallback error) override
        try {
        stop();
        Apartment apartment;
        if (!GraphicsCaptureSession::IsSupported())
            throw std::runtime_error("이 PC에서 Windows Graphics Capture를 지원하지 않습니다.");
        error_ = std::move(error);
        crop_ = region;
        auto interop = get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        const auto handle = source.id.toULongLong();
        if (source.kind == SourceKind::Window && (!IsWindow(reinterpret_cast<HWND>(handle)) ||
                                                  !IsWindowVisible(reinterpret_cast<HWND>(handle))))
            throw std::runtime_error(
                "보이는 창을 선택해 주세요. 숨겨지거나 종료된 창은 녹화할 수 없습니다.");
        if (source.kind == SourceKind::Window)
            check(interop->CreateForWindow(reinterpret_cast<HWND>(handle),
                                           guid_of<GraphicsCaptureItem>(), put_abi(item_)));
        else
            check(interop->CreateForMonitor(reinterpret_cast<HMONITOR>(handle),
                                            guid_of<GraphicsCaptureItem>(), put_abi(item_)));
        const auto size = item_.Size();
        canvas_ = region.isEmpty() ? QSize(size.Width, size.Height) : region.size();
        check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                                &device_, nullptr, &context_));
        ComPtr<IDXGIDevice> dxgi;
        check(device_.As(&dxgi));
        com_ptr<IInspectable> inspectable;
        check(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), inspectable.put()));
        rtDevice_ = inspectable.as<IDirect3DDevice>();
        pool_ = Direct3D11CaptureFramePool::CreateFreeThreaded(
            rtDevice_, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
        stopped_ = false;
        closedToken_ = item_.Closed([this](auto &&, auto &&) {
            if (!stopped_ && error_)
                error_("녹화 대상 창이 닫혔습니다.");
        });
        frameToken_ = pool_.FrameArrived([this](auto &&sender, auto &&) {
            std::lock_guard lock(mutex_);
            if (stopped_)
                return;
            try {
                auto frame = sender.TryGetNextFrame();
                if (!frame)
                    return;
                const auto content = frame.ContentSize();
                if (content.Width < 1 || content.Height < 1)
                    return;
                auto access = frame.Surface()
                                  .as<::Windows::Graphics::DirectX::Direct3D11::
                                          IDirect3DDxgiInterfaceAccess>();
                ComPtr<ID3D11Texture2D> texture;
                check(access->GetInterface(IID_PPV_ARGS(&texture)));
                D3D11_TEXTURE2D_DESC desc;
                texture->GetDesc(&desc);
                D3D11_TEXTURE2D_DESC old{};
                if (staging_)
                    staging_->GetDesc(&old);
                if (!staging_ || old.Width != desc.Width || old.Height != desc.Height) {
                    staging_.Reset();
                    auto stagingDesc = desc;
                    stagingDesc.BindFlags = 0;
                    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                    stagingDesc.Usage = D3D11_USAGE_STAGING;
                    stagingDesc.MiscFlags = 0;
                    check(device_->CreateTexture2D(&stagingDesc, nullptr, &staging_));
                }
                context_->CopyResource(staging_.Get(), texture.Get());
                D3D11_MAPPED_SUBRESOURCE mapped{};
                check(context_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &mapped));
                QImage image(canvas_, QImage::Format_ARGB32);
                image.fill(Qt::black);
                const int x = crop_.isEmpty() ? 0 : crop_.x(), y = crop_.isEmpty() ? 0 : crop_.y();
                const int w = std::max(
                    0, std::min(canvas_.width(), std::min(int(desc.Width), content.Width) - x));
                const int h = std::max(
                    0, std::min(canvas_.height(), std::min(int(desc.Height), content.Height) - y));
                for (int row = 0; row < h; row++)
                    memcpy(image.scanLine(row),
                           static_cast<const char *>(mapped.pData) + (row + y) * mapped.RowPitch +
                               x * 4,
                           size_t(w) * 4);
                context_->Unmap(staging_.Get(), 0);
                {
                    std::lock_guard frameLock(frameMutex_);
                    latest_ = std::move(image);
                }
                if (desc.Width != UINT(content.Width) || desc.Height != UINT(content.Height)) {
                    frame.Close();
                    sender.Recreate(rtDevice_, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2,
                                    content);
                }
            } catch (const hresult_error &ex) {
                if (!stopped_ && error_)
                    error_(QString::fromWCharArray(ex.message().c_str()));
            } catch (const std::exception &ex) {
                if (!stopped_ && error_)
                    error_(QString::fromUtf8(ex.what()));
            }
        });
        session_ = pool_.CreateCaptureSession(item_);
        session_.IsCursorCaptureEnabled(cursor);
        session_.StartCapture();
    } catch (const hresult_error &ex) {
        try {
            stop();
        } catch (...) {
        }
        throw std::runtime_error(QString::fromWCharArray(ex.message().c_str()).toStdString());
    }
    QImage latestFrame() override {
        std::lock_guard lock(frameMutex_);
        return latest_;
    }
    void setCursor(bool enabled) override try {
        std::lock_guard lock(mutex_);
        if (session_)
            session_.IsCursorCaptureEnabled(enabled);
    } catch (const hresult_error &ex) {
        throw std::runtime_error(QString::fromWCharArray(ex.message().c_str()).toStdString());
    }
    void stop() override {
        stopped_ = true;
        if (pool_)
            pool_.FrameArrived(frameToken_);
        if (item_)
            item_.Closed(closedToken_);
        if (session_)
            session_.Close();
        if (pool_)
            pool_.Close();
        std::lock_guard lock(mutex_);
        session_ = nullptr;
        pool_ = nullptr;
        item_ = nullptr;
        rtDevice_ = nullptr;
        staging_.Reset();
        context_.Reset();
        device_.Reset();
        {
            std::lock_guard frameLock(frameMutex_);
            latest_ = {};
        }
    }
};

// These declarations match audioclientactivationparams.h (SDK 20348+).
// Keeping the small ABI declaration local allows building with the 19041 SDK.
struct ProcessActivationParams {
    int activationType = 1;
    DWORD processId = 0;
    int loopbackMode = 0;
};
class ActivationHandler final
    : public Microsoft::WRL::RuntimeClass<
          Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
          IActivateAudioInterfaceCompletionHandler, Microsoft::WRL::FtmBase> {
  public:
    HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HRESULT result = E_PENDING;
    ComPtr<IAudioClient> client;
    ~ActivationHandler() {
        if (event)
            CloseHandle(event);
    }
    STDMETHOD(ActivateCompleted)(IActivateAudioInterfaceAsyncOperation *operation) override {
        HRESULT activation = E_FAIL;
        ComPtr<IUnknown> unknown;
        result = operation->GetActivateResult(&activation, &unknown);
        if (SUCCEEDED(result))
            result = activation;
        if (SUCCEEDED(result))
            result = unknown.As(&client);
        SetEvent(event);
        return S_OK;
    }
};
class WinAudio final : public AudioCapture {
    std::jthread worker_;
    std::atomic<bool> stop_{false};

  public:
    ~WinAudio() override {
        stop();
    }
    void start(const AudioSource &source, AudioCallback data, ErrorCallback error) override {
        stop();
        stop_ = false;
        auto ready = std::make_shared<std::promise<void>>();
        auto result = ready->get_future();
        worker_ = std::jthread([=, this] {
            bool started = false;
            ComPtr<IAudioClient> client;
            try {
                Apartment apartment;
                if (source.kind == AudioKind::Application) {
                    ProcessActivationParams params;
                    params.processId = source.processId;
                    PROPVARIANT variant;
                    PropVariantInit(&variant);
                    variant.vt = VT_BLOB;
                    variant.blob.cbSize = sizeof(params);
                    variant.blob.pBlobData = reinterpret_cast<BYTE *>(&params);
                    auto handler = Microsoft::WRL::Make<ActivationHandler>();
                    ComPtr<IActivateAudioInterfaceAsyncOperation> operation;
                    check(ActivateAudioInterfaceAsync(L"VAD\\Process_Loopback",
                                                      __uuidof(IAudioClient), &variant,
                                                      handler.Get(), &operation));
                    if (WaitForSingleObject(handler->event, 10000) != WAIT_OBJECT_0)
                        throw std::runtime_error("프로그램 오디오 캡처 초기화 시간이 초과됐습니다. "
                                                 "Windows 11에서 다시 확인해 주세요.");
                    check(handler->result);
                    client = handler->client;
                } else {
                    ComPtr<IMMDeviceEnumerator> devices;
                    check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                           IID_PPV_ARGS(&devices)));
                    ComPtr<IMMDevice> device;
                    check(
                        devices->GetDevice(reinterpret_cast<LPCWSTR>(source.id.utf16()), &device));
                    check(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client));
                }
                WAVEFORMATEX format{};
                format.wFormatTag = WAVE_FORMAT_PCM;
                format.nChannels = 2;
                format.nSamplesPerSec = 48000;
                format.wBitsPerSample = 16;
                format.nBlockAlign = 4;
                format.nAvgBytesPerSec = 192000;
                DWORD flags =
                    AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
                if (source.kind != AudioKind::Microphone)
                    flags |= AUDCLNT_STREAMFLAGS_LOOPBACK;
                check(client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 1000000, 0, &format,
                                         nullptr));
                ComPtr<IAudioCaptureClient> capture;
                check(client->GetService(IID_PPV_ARGS(&capture)));
                LARGE_INTEGER frequency, counter;
                QueryPerformanceFrequency(&frequency);
                QueryPerformanceCounter(&counter);
                const auto offset =
                    clockNs() - qint64(counter.QuadPart * (1000000000.0 / frequency.QuadPart));
                check(client->Start());
                started = true;
                ready->set_value();
                while (!stop_) {
                    UINT32 available = 0;
                    check(capture->GetNextPacketSize(&available));
                    while (available && !stop_) {
                        BYTE *bytes = nullptr;
                        UINT32 frames = 0;
                        DWORD packetFlags = 0;
                        UINT64 position = 0, qpc = 0;
                        check(capture->GetBuffer(&bytes, &frames, &packetFlags, &position, &qpc));
                        AudioPacket packet;
                        packet.pcm = QByteArray(qsizetype(frames) * 4, 0);
                        if (!(packetFlags & AUDCLNT_BUFFERFLAGS_SILENT) && bytes)
                            memcpy(packet.pcm.data(), bytes, size_t(frames) * 4);
                        packet.timestampNs =
                            (packetFlags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) || qpc == 0
                                ? clockNs()
                                : qint64(qpc) * 100 + offset;
                        check(capture->ReleaseBuffer(frames));
                        data(std::move(packet));
                        check(capture->GetNextPacketSize(&available));
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(3));
                }
                client->Stop();
            } catch (...) {
                if (client)
                    client->Stop();
                if (!started)
                    ready->set_exception(std::current_exception());
                else
                    try {
                        throw;
                    } catch (const std::exception &ex) {
                        error(QString::fromUtf8(ex.what()));
                    } catch (...) {
                        error("Windows 오디오 캡처가 중단됐습니다.");
                    }
            }
        });
        result.get();
    }
    void stop() override {
        stop_ = true;
        if (worker_.joinable())
            worker_.join();
    }
};

class WinPlatform final : public Platform {
  public:
    QString name() const override {
        return "Windows";
    }
    QList<Source> sources() override {
        QList<Source> result;
        EnumDisplayMonitors(
            nullptr, nullptr,
            [](HMONITOR monitor, HDC, LPRECT rect, LPARAM data) -> BOOL {
                auto &list = *reinterpret_cast<QList<Source> *>(data);
                MONITORINFO info{sizeof(info)};
                GetMonitorInfoW(monitor, &info);
                const auto size = QRect(rect->left, rect->top, rect->right - rect->left,
                                        rect->bottom - rect->top);
                list.append({QString::number(quintptr(monitor)),
                             QString("디스플레이 %1  ·  %2 × %3%4")
                                 .arg(list.size() + 1)
                                 .arg(size.width())
                                 .arg(size.height())
                                 .arg(info.dwFlags & MONITORINFOF_PRIMARY ? "  ·  기본" : ""),
                             SourceKind::Display, size});
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&result));
        EnumWindows(
            [](HWND hwnd, LPARAM data) -> BOOL {
                DWORD pid = 0;
                GetWindowThreadProcessId(hwnd, &pid);
                if (!IsWindowVisible(hwnd) || IsIconic(hwnd) || pid == GetCurrentProcessId() ||
                    GetWindow(hwnd, GW_OWNER))
                    return TRUE;
                wchar_t title[512];
                if (!GetWindowTextW(hwnd, title, 512))
                    return TRUE;
                RECT bounds;
                GetWindowRect(hwnd, &bounds);
                if (bounds.right - bounds.left < 32 || bounds.bottom - bounds.top < 32)
                    return TRUE;
                reinterpret_cast<QList<Source> *>(data)->append(
                    {QString::number(quintptr(hwnd)), QString::fromWCharArray(title),
                     SourceKind::Window,
                     QRect(bounds.left, bounds.top, bounds.right - bounds.left,
                           bounds.bottom - bounds.top),
                     pid});
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&result));
        return result;
    }
    QList<AudioSource> audioSources() override {
        Apartment apartment;
        QList<AudioSource> result;
        QSet<QString> defaultIds;
        QSet<DWORD> pids;
        QHash<DWORD, DWORD> parents;
        QHash<DWORD, QString> names;
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W entry{sizeof(entry)};
            if (Process32FirstW(snapshot, &entry))
                do {
                    parents[entry.th32ProcessID] = entry.th32ParentProcessID;
                    names[entry.th32ProcessID] = QString::fromWCharArray(entry.szExeFile);
                } while (Process32NextW(snapshot, &entry));
            CloseHandle(snapshot);
        }
        ComPtr<IMMDeviceEnumerator> devices;
        check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                               IID_PPV_ARGS(&devices)));
        for (const auto flow : {eCapture, eRender}) {
            ComPtr<IMMDevice> preferred;
            if (SUCCEEDED(devices->GetDefaultAudioEndpoint(flow, eConsole, &preferred)))
                defaultIds.insert(deviceId(preferred.Get()));
            ComPtr<IMMDeviceCollection> collection;
            check(devices->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &collection));
            UINT count = 0;
            collection->GetCount(&count);
            for (UINT i = 0; i < count; i++) {
                ComPtr<IMMDevice> device;
                check(collection->Item(i, &device));
                result.append({deviceId(device.Get()), friendly(device.Get()),
                               flow == eCapture ? "마이크 · 독립 트랙"
                                                : "장치 전체 소리 · 앱 트랙과 중복될 수 있음",
                               flow == eCapture ? AudioKind::Microphone : AudioKind::System});
                if (flow == eCapture)
                    continue;
                ComPtr<IAudioSessionManager2> manager;
                if (FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                                            &manager)))
                    continue;
                ComPtr<IAudioSessionEnumerator> sessions;
                check(manager->GetSessionEnumerator(&sessions));
                int n = 0;
                sessions->GetCount(&n);
                for (int j = 0; j < n; j++) {
                    ComPtr<IAudioSessionControl> control;
                    check(sessions->GetSession(j, &control));
                    ComPtr<IAudioSessionControl2> session;
                    if (FAILED(control.As(&session)))
                        continue;
                    DWORD pid = 0;
                    session->GetProcessId(&pid);
                    if (!pid || pid == GetCurrentProcessId())
                        continue;
                    QSet<DWORD> seen;
                    while (parents.contains(pid) && names.value(parents[pid]) == names.value(pid) &&
                           !seen.contains(pid)) {
                        seen.insert(pid);
                        pid = parents[pid];
                    }
                    if (pids.contains(pid))
                        continue;
                    pids.insert(pid);
                    result.append({QString::number(pid), processName(pid),
                                   QString("앱 소리 · PID %1 · 하위 프로세스 포함").arg(pid),
                                   AudioKind::Application, pid});
                }
            }
        }
        std::stable_sort(result.begin(), result.end(),
                         [&](const AudioSource &a, const AudioSource &b) {
                             const auto rank = [&](const AudioSource &source) {
                                 return source.kind == AudioKind::Application ? 4
                                        : source.kind == AudioKind::Microphone
                                            ? (defaultIds.contains(source.id) ? 0 : 1)
                                            : (defaultIds.contains(source.id) ? 2 : 3);
                             };
                             return rank(a) < rank(b);
                         });
        return result;
    }
    std::unique_ptr<VideoCapture> createVideoCapture() override {
        return std::make_unique<WinVideo>();
    }
    std::unique_ptr<AudioCapture> createAudioCapture() override {
        return std::make_unique<WinAudio>();
    }
};
} // namespace
std::unique_ptr<Platform> createPlatform() {
    return std::make_unique<WinPlatform>();
}
} // namespace luma
