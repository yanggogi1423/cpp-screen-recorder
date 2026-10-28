#include "GlobalHotkeys.h"
#include <QCoreApplication>
#ifdef Q_OS_WIN
#include <windows.h>
#elif defined(Q_OS_MACOS)
#include <Carbon/Carbon.h>
#endif

struct GlobalHotkeys::Native {
#ifdef Q_OS_MACOS
    EventHotKeyRef record = nullptr, pause = nullptr;
    EventHandlerRef handler = nullptr;
    EventHandlerUPP callback = nullptr;
#endif
};
GlobalHotkeys::GlobalHotkeys(QObject *parent)
    : QObject(parent), native_(std::make_unique<Native>()) {
#ifdef Q_OS_WIN
    const bool record = RegisterHotKey(nullptr, RecordId, MOD_CONTROL | MOD_NOREPEAT, 'R');
    const bool pause = RegisterHotKey(nullptr, PauseId, MOD_CONTROL | MOD_NOREPEAT, 'P');
    available_ = record && pause;
    if (!available_) {
        if (record)
            UnregisterHotKey(nullptr, RecordId);
        if (pause)
            UnregisterHotKey(nullptr, PauseId);
    } else
        QCoreApplication::instance()->installNativeEventFilter(this);
    status_ = available_ ? "Ctrl+R 시작/종료 · Ctrl+P 일시정지/재개 · 앱이 뒤에 있어도 동작합니다."
                         : "단축키가 다른 앱에서 사용 중입니다. 앱 안의 버튼을 사용해 주세요.";
#elif defined(Q_OS_MACOS)
    native_->callback =
        NewEventHandlerUPP([](EventHandlerCallRef, EventRef event, void *context) -> OSStatus {
            EventHotKeyID id{};
            if (GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, nullptr,
                                  sizeof(id), nullptr, &id) != noErr)
                return eventNotHandledErr;
            auto *self = static_cast<GlobalHotkeys *>(context);
            QMetaObject::invokeMethod(
                self,
                [self, id] {
                    if (id.id == RecordId)
                        emit self->recordPressed();
                    else if (id.id == PauseId)
                        emit self->pausePressed();
                },
                Qt::QueuedConnection);
            return noErr;
        });
    const EventTypeSpec type{kEventClassKeyboard, kEventHotKeyPressed};
    const auto handler = InstallEventHandler(GetApplicationEventTarget(), native_->callback, 1,
                                             &type, this, &native_->handler);
    EventHotKeyID record{0x4c554d41, RecordId}, pause{0x4c554d41, PauseId};
    const auto a = RegisterEventHotKey(kVK_ANSI_R, cmdKey, record, GetApplicationEventTarget(), 0,
                                       &native_->record);
    const auto b = RegisterEventHotKey(kVK_ANSI_P, cmdKey, pause, GetApplicationEventTarget(), 0,
                                       &native_->pause);
    available_ = handler == noErr && a == noErr && b == noErr;
    if (!available_) {
        if (native_->record) {
            UnregisterEventHotKey(native_->record);
            native_->record = nullptr;
        }
        if (native_->pause) {
            UnregisterEventHotKey(native_->pause);
            native_->pause = nullptr;
        }
    }
    status_ = available_ ? "⌘R 시작/종료 · ⌘P 일시정지/재개 · 앱이 뒤에 있어도 동작합니다."
                         : "전역 단축키를 등록하지 못했습니다. 앱 안의 버튼을 사용해 주세요.";
#endif
}
GlobalHotkeys::~GlobalHotkeys() {
#ifdef Q_OS_WIN
    if (available_) {
        UnregisterHotKey(nullptr, RecordId);
        UnregisterHotKey(nullptr, PauseId);
        QCoreApplication::instance()->removeNativeEventFilter(this);
    }
#elif defined(Q_OS_MACOS)
    if (native_->record)
        UnregisterEventHotKey(native_->record);
    if (native_->pause)
        UnregisterEventHotKey(native_->pause);
    if (native_->handler)
        RemoveEventHandler(native_->handler);
    if (native_->callback)
        DisposeEventHandlerUPP(native_->callback);
#endif
}
bool GlobalHotkeys::nativeEventFilter(const QByteArray &, void *message, qintptr *result) {
#ifdef Q_OS_WIN
    auto *msg = static_cast<MSG *>(message);
    if (msg->message == WM_HOTKEY && (msg->wParam == RecordId || msg->wParam == PauseId)) {
        if (msg->wParam == RecordId)
            emit recordPressed();
        else
            emit pausePressed();
        if (result)
            *result = 0;
        return true;
    }
#else
    Q_UNUSED(message);
    Q_UNUSED(result);
#endif
    return false;
}
