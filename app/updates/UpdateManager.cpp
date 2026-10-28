#include "UpdateManager.h"
#include "AppInfo.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QDesktopServices>
#include <QUuid>
#include <QTimer>

UpdateManager::UpdateManager(bool startup, QObject *parent) : QObject(parent) {
    QSettings settings;
    automatic_ = settings.value("updates/automatic", true).toBool();
    beta_ = settings.value("updates/beta", QString(appinfo::version).contains('-')).toBool();
    network_.setTransferTimeout(30000);
    installPoll_.setInterval(100);
    connect(&installPoll_, &QTimer::timeout, this, [this] {
        if (QFileInfo::exists(helper_ + ".error") || installTimer_.elapsed() > 90000) {
            installPoll_.stop();
            QFile canceled(helper_ + ".cancel");
            canceled.open(QIODevice::WriteOnly);
            setState("error", "업데이트를 준비하지 못했습니다. 이전 버전은 유지됩니다.");
            return;
        }
        if (QFileInfo::exists(helper_ + ".ready")) {
            installPoll_.stop();
            setState("installing", "업데이트를 적용하고 앱을 다시 시작합니다");
            emit readyToRestart();
        }
    });
    if (startup)
        QTimer::singleShot(15000, this, [this] {
            if (automatic())
                check(false);
        });
}
UpdateManager::~UpdateManager() {
    if (reply_) {
        disconnect(reply_, nullptr, this, nullptr);
        reply_->abort();
    }
}
bool UpdateManager::automatic() const {
    return automatic_;
}
bool UpdateManager::beta() const {
    return beta_;
}
void UpdateManager::setAutomatic(bool value) {
    automatic_ = value;
    QSettings().setValue("updates/automatic", value);
    emit changed();
}
void UpdateManager::setBeta(bool value) {
    if (reply_ || state_ == "ready")
        return;
    beta_ = value;
    QSettings().setValue("updates/beta", value);
    release_ = {};
    setState("idle", "업데이트 채널을 변경했습니다");
    check();
}
void UpdateManager::setState(QString state, QString message) {
    state_ = std::move(state);
    message_ = std::move(message);
    emit changed();
}
QNetworkRequest UpdateManager::request(const QUrl &url) const {
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "FARecorder/" + QString(appinfo::version));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setMaximumRedirectsAllowed(5);
    return request;
}
void UpdateManager::check(bool manual) {
    if (reply_ || state_ == "ready")
        return;
    QSettings settings;
    const auto last = settings.value("updates/lastCheck").toLongLong();
    if (QDateTime::currentSecsSinceEpoch() < settings.value("updates/retryAfter").toLongLong()) {
        if (manual)
            setState("error", "GitHub 조회 한도가 복구된 뒤 다시 확인해 주세요.");
        return;
    }
    if (!manual && QDateTime::currentSecsSinceEpoch() - last < 86400)
        return;
    setState("checking", "새 버전을 확인하는 중…");
    auto req = request(
        QUrl("https://api.github.com/repos/yanggogi1423/cpp-screen-recorder/releases?per_page=50"));
    const auto cached = settings.value("updates/catalog").toByteArray();
    if (!cached.isEmpty())
        req.setRawHeader("If-None-Match", settings.value("updates/etag").toByteArray());
    auto *reply = network_.get(req);
    reply_ = reply;
    connect(reply, &QNetworkReply::readyRead, this, [reply] {
        if (reply->bytesAvailable() > 2 * 1024 * 1024)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, cached] {
        reply_ = nullptr;
        reply->deleteLater();
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 304 && (reply->error() != QNetworkReply::NoError || status != 200)) {
            if (status == 403 || status == 429)
                QSettings().setValue("updates/retryAfter",
                                     QDateTime::currentSecsSinceEpoch() + 3600);
            setState("error", status == 403 || status == 429
                                  ? "GitHub 조회 한도에 도달했습니다. 잠시 후 다시 확인해 주세요."
                                  : "업데이트를 확인하지 못했습니다. 인터넷 연결을 확인해 주세요.");
            return;
        }
        const auto bytes = status == 304 ? cached : reply->readAll();
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(bytes, &error);
        if (bytes.size() > 2 * 1024 * 1024 || error.error != QJsonParseError::NoError ||
            !document.isArray()) {
            setState("error", "업데이트 정보를 읽을 수 없습니다.");
            return;
        }
        QSettings settings;
        settings.setValue("updates/lastCheck", QDateTime::currentSecsSinceEpoch());
        settings.setValue("updates/catalog", bytes);
        if (status != 304)
            settings.setValue("updates/etag", reply->rawHeader("ETag"));
        release_ =
            updates::selectRelease(document.array(), appinfo::version, updates::platform(), beta());
        if (release_.valid()) {
            setState("available", "새 버전 " + release_.version + "을 사용할 수 있습니다");
            emit available();
        } else
            setState("current", "이 업데이트 채널의 최신 버전입니다");
    });
}
void UpdateManager::download() {
    if (reply_ || !release_.valid() || !allowed_)
        return;
    directory_ = QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                     .filePath("Updates/" + QUuid::createUuid().toString(QUuid::Id128));
    if (!QDir().mkpath(directory_)) {
        setState("error", "업데이트 폴더를 만들지 못했습니다.");
        return;
    }
    archive_ = QDir(directory_).filePath(release_.assetName);
    output_ = std::make_unique<QSaveFile>(archive_);
    output_->setDirectWriteFallback(false);
    if (!output_->open(QIODevice::WriteOnly)) {
        setState("error", "업데이트 파일을 저장할 수 없습니다.");
        return;
    }
    hash_.reset();
    received_ = 0;
    progress_ = 0;
    progressTimer_.start();
    setState("downloading", "업데이트 다운로드 중…");
    auto *reply = network_.get(request(release_.download));
    reply_ = reply;
    auto consume = [this, reply] {
        const auto chunk = reply->readAll();
        received_ += chunk.size();
        if (received_ > release_.size || output_->write(chunk) != chunk.size()) {
            reply->abort();
            return;
        }
        hash_.addData(chunk);
        progress_ = double(received_) / release_.size;
        if (progressTimer_.elapsed() >= 100) {
            progressTimer_.restart();
            emit changed();
        }
    };
    connect(reply, &QNetworkReply::readyRead, this, consume);
    connect(reply, &QNetworkReply::finished, this, [this, reply, consume] {
        consume();
        reply_ = nullptr;
        reply->deleteLater();
        if (state_ == "canceled" || reply->error() != QNetworkReply::NoError ||
            received_ != release_.size ||
            QString::fromLatin1(hash_.result().toHex()) != release_.digest) {
            output_->cancelWriting();
            output_.reset();
            if (state_ != "canceled")
                setState("error", "다운로드 또는 SHA256 검증에 실패했습니다. 설치하지 않았습니다.");
            return;
        }
        if (!output_->commit()) {
            output_.reset();
            setState("error", "업데이트 파일 저장에 실패했습니다.");
            return;
        }
        output_.reset();
        setState("ready", "다운로드와 SHA256 검증을 마쳤습니다");
    });
}
bool UpdateManager::install() {
    if (state_ != "ready" || !allowed_)
        return false;
    QDir root(QCoreApplication::applicationDirPath());
#ifdef Q_OS_WIN
    if (root.dirName() != "bin") {
        setState("error", "배포 ZIP을 모두 압축 해제한 위치에서 실행해 주세요.");
        return false;
    }
    root.cdUp();
    const QString executable = "FARecorderUpdater.exe";
#else
    root.cdUp();
    root.cdUp();
    const QString executable = "FARecorderUpdater";
#endif
    QFile marker(root.filePath("farecorder-package.txt"));
    if (!marker.open(QIODevice::ReadOnly) || marker.readLine().trimmed() != "FARecorder") {
        setState(
            "error",
            "자동 설치를 지원하는 배포 폴더가 아닙니다. 릴리즈 페이지에서 새 ZIP을 받아 주세요.");
        return false;
    }
    const auto source = QDir(QCoreApplication::applicationDirPath()).filePath(executable);
    const auto helper =
        QDir(directory_).filePath(QUuid::createUuid().toString(QUuid::Id128) + "-" + executable);
    if (!QFile::copy(source, helper)) {
        setState("error", "업데이트 도우미를 준비하지 못했습니다.");
        return false;
    }
    QFile::setPermissions(helper, QFile::permissions(helper) | QFileDevice::ExeOwner);
    qint64 child = 0;
    const QStringList args{
        "--apply",       archive_,         root.absolutePath(),
        release_.digest, release_.version, QString::number(QCoreApplication::applicationPid())};
    if (!QProcess::startDetached(helper, args, directory_, &child)) {
        setState("error", "업데이트 도우미를 시작하지 못했습니다.");
        return false;
    }
    helper_ = helper;
    installTimer_.start();
    installPoll_.start();
    setState("preparing", "새 버전을 준비하는 중…");
    return true;
}
void UpdateManager::cancel() {
    if (state_ == "preparing") {
        QFile canceled(helper_ + ".cancel");
        canceled.open(QIODevice::WriteOnly);
        installPoll_.stop();
        setState("canceled", "업데이트 설치를 취소했습니다. 이전 버전은 유지됩니다.");
    } else if (reply_) {
        setState("canceled", "업데이트 다운로드를 취소했습니다");
        reply_->abort();
    }
}
void UpdateManager::openReleasePage() {
    QDesktopServices::openUrl(
        release_.page.isEmpty() ? QUrl(QString(appinfo::repository) + "/releases") : release_.page);
}
