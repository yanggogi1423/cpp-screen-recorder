#pragma once
#include <QJsonArray>
#include <QUrl>
namespace updates {
struct Release {
    QString version, notes, assetName, digest;
    QUrl download, page;
    qint64 size = 0;
    bool valid() const {
        return !download.isEmpty();
    }
};
int compareVersions(QString left, QString right);
bool validVersion(QString version);
Release selectRelease(const QJsonArray &, const QString &current, const QString &platform,
                      bool beta);
QString platform();
} // namespace updates
