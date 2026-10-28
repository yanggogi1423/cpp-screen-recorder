#include "SourceWatcher.h"
#include <windows.h>
#include <mmdeviceapi.h>
#include <wrl.h>
#include <QCoreApplication>
#include <QHash>
#include <QPointer>
#include <atomic>

namespace {
QHash<HWINEVENTHOOK, QPointer<SourceWatcher>> watchers;
void CALLBACK windowEvent(HWINEVENTHOOK hook, DWORD event, HWND, LONG object, LONG child, DWORD,
                          DWORD) {
    if (object != OBJID_WINDOW || child != CHILDID_SELF ||
        (event != EVENT_OBJECT_CREATE && event != EVENT_OBJECT_DESTROY &&
         event != EVENT_OBJECT_SHOW && event != EVENT_OBJECT_HIDE &&
         event != EVENT_OBJECT_NAMECHANGE))
        return;
    if (auto owner = watchers.value(hook))
        emit owner->changed();
}
class DeviceNotifications final : public IMMNotificationClient {
    std::atomic<ULONG> refs_{1};
    QPointer<SourceWatcher> owner_;
    void notify() {
        auto owner = owner_;
        if (owner)
            QMetaObject::invokeMethod(
                owner,
                [owner] {
                    if (owner)
                        emit owner->changed();
                },
                Qt::QueuedConnection);
    }

  public:
    explicit DeviceNotifications(SourceWatcher *owner) : owner_(owner) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **value) override {
        if (!value)
            return E_POINTER;
        *value = nullptr;
        if (id == __uuidof(IUnknown) || id == __uuidof(IMMNotificationClient)) {
            *value = static_cast<IMMNotificationClient *>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        return ++refs_;
    }
    ULONG STDMETHODCALLTYPE Release() override {
        auto value = --refs_;
        if (!value)
            delete this;
        return value;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override {
        notify();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override {
        notify();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override {
        notify();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow, ERole, LPCWSTR) override {
        notify();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override {
        notify();
        return S_OK;
    }
};
} // namespace
struct SourceWatcher::Native {
    HWINEVENTHOOK hook = nullptr;
    Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
    Microsoft::WRL::ComPtr<DeviceNotifications> devices;
};
SourceWatcher::SourceWatcher(QObject *parent)
    : QObject(parent), native_(std::make_unique<Native>()) {}
SourceWatcher::~SourceWatcher() {
    stop();
}
void SourceWatcher::start() {
    if (native_->hook || native_->enumerator)
        return;
    native_->hook =
        SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_NAMECHANGE, nullptr, windowEvent, 0, 0,
                        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    if (native_->hook)
        watchers.insert(native_->hook, this);
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                   IID_PPV_ARGS(&native_->enumerator)))) {
        native_->devices.Attach(new DeviceNotifications(this));
        native_->enumerator->RegisterEndpointNotificationCallback(native_->devices.Get());
    }
    QCoreApplication::instance()->installNativeEventFilter(this);
}
void SourceWatcher::stop() {
    QCoreApplication::instance()->removeNativeEventFilter(this);
    if (native_->hook) {
        watchers.remove(native_->hook);
        UnhookWinEvent(native_->hook);
        native_->hook = nullptr;
    }
    if (native_->enumerator && native_->devices)
        native_->enumerator->UnregisterEndpointNotificationCallback(native_->devices.Get());
    native_->devices.Reset();
    native_->enumerator.Reset();
}
bool SourceWatcher::nativeEventFilter(const QByteArray &, void *message, qintptr *) {
    auto *event = static_cast<MSG *>(message);
    if (event->message == WM_DISPLAYCHANGE || event->message == WM_DEVICECHANGE)
        emit changed();
    return false;
}
