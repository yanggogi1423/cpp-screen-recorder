#pragma once
#include <QEvent>
#include <QPointer>
#include <QWindow>
#include <functional>
// Route native application Quit through the same confirmation as window Close.
class QuitGuard final : public QObject {
    QPointer<QWindow> window_;
    std::function<bool()> confirm_;
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() == QEvent::Quit && window_ && window_->isVisible() && confirm_()) {
            QMetaObject::invokeMethod(window_.data(), "close", Qt::QueuedConnection);
            return true;
        }
        return false;
    }

  public:
    QuitGuard(QWindow *window, std::function<bool()> confirm)
        : window_(window), confirm_(std::move(confirm)) {}
};
