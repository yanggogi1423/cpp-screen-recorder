#pragma once
#include "UpdateCatalog.h"
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSaveFile>
#include <QCryptographicHash>
#include <QPointer>
#include <QElapsedTimer>
#include <QTimer>
#include <memory>
class UpdateManager final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QString version READ version NOTIFY changed)
    Q_PROPERTY(QString notes READ notes NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(bool automatic READ automatic WRITE setAutomatic NOTIFY changed)
    Q_PROPERTY(bool beta READ beta WRITE setBeta NOTIFY changed)
    Q_PROPERTY(bool allowed READ allowed NOTIFY changed)
    QNetworkAccessManager network_;
    QPointer<QNetworkReply> reply_;
    std::unique_ptr<QSaveFile> output_;
    QCryptographicHash hash_{QCryptographicHash::Sha256};
    updates::Release release_;
    QString state_ = "idle", message_ = "업데이트를 확인할 수 있습니다", directory_, archive_;
    double progress_ = 0;
    qint64 received_ = 0;
    bool allowed_ = true;
    bool automatic_ = true, beta_ = false;
    QElapsedTimer progressTimer_;
    QTimer installPoll_;
    QElapsedTimer installTimer_;
    QString helper_;
    void setState(QString state, QString message);
    QNetworkRequest request(const QUrl &) const;

  public:
    explicit UpdateManager(bool startup, QObject *parent = nullptr);
    ~UpdateManager() override;
    QString state() const {
        return state_;
    }
    QString message() const {
        return message_;
    }
    QString version() const {
        return release_.version;
    }
    QString notes() const {
        return release_.notes;
    }
    double progress() const {
        return progress_;
    }
    bool allowed() const {
        return allowed_;
    }
    bool automatic() const;
    bool beta() const;
    void setAutomatic(bool);
    void setBeta(bool);
    void setAllowed(bool value) {
        allowed_ = value;
        emit changed();
    }
    Q_INVOKABLE void check(bool manual = true);
    Q_INVOKABLE void download();
    Q_INVOKABLE bool install();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void openReleasePage();
  signals:
    void changed();
    void available();
    void readyToRestart();
};
