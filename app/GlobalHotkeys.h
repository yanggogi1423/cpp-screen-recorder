#pragma once
#include <QObject>
#include <QAbstractNativeEventFilter>
#include <memory>

class GlobalHotkeys : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
  public:
    explicit GlobalHotkeys(QObject *parent = nullptr);
    ~GlobalHotkeys() override;
    bool available() const {
        return available_;
    }
    QString status() const {
        return status_;
    }
    bool nativeEventFilter(const QByteArray &, void *, qintptr *) override;
    static constexpr int RecordId = 0x4c01, PauseId = 0x4c02;
  signals:
    void recordPressed();
    void pausePressed();

  private:
    struct Native;
    std::unique_ptr<Native> native_;
    bool available_ = false;
    QString status_;
};
