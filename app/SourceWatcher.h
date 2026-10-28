#pragma once
#include <QObject>
#include <QAbstractNativeEventFilter>
#include <memory>

class SourceWatcher final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
  public:
    explicit SourceWatcher(QObject *parent = nullptr);
    ~SourceWatcher() override;
    void start();
    void stop();
    bool nativeEventFilter(const QByteArray &, void *, qintptr *) override;
  signals:
    void changed();

  private:
    struct Native;
    std::unique_ptr<Native> native_;
};
