#include "updates/UpdateManager.h"
#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    app.setOrganizationName("FARecorderFixture"); app.setApplicationName("Updates");
    const auto name = "FARecorder-9.0.0-beta.1-" + updates::platform() + ".zip";
    const QJsonObject release{{"tag_name", "v9.0.0-beta.1"}, {"prerelease", true}, {"draft", false},
        {"assets", QJsonArray{QJsonObject{{"name", name}, {"state", "uploaded"}, {"size", 10},
            {"digest", "sha256:" + QString(64, 'a')},
            {"browser_download_url", "https://github.com/yanggogi1423/cpp-screen-recorder/releases/download/v9.0.0-beta.1/" + name}}}}};
    QSettings store;
    store.setValue("updates/beta", true);
    store.setValue("updates/lastCheck", QDateTime::currentSecsSinceEpoch());
    store.setValue("updates/catalog", QJsonDocument(QJsonArray{release}).toJson());
    UpdateManager manager(false);
    int notices = 0;
    QObject::connect(&manager, &UpdateManager::available, [&] { ++notices; });
    manager.setAllowed(false);
    manager.check(false);
    if (manager.state() != "available" || manager.version() != "9.0.0-beta.1" || notices != 1) return 1;
    manager.startUpdate(); // Busy/unsaved work must never start a download or replacement.
    if (manager.state() != "available") return 2;
    manager.setAllowed(true);
    if (notices != 2) return 3;
    manager.setAllowed(true);
    if (notices != 2) return 4;
    // A second startup reuses the same cache and still exposes the pending update.
    UpdateManager restarted(false);
    restarted.check(false);
    if (restarted.state() != "available") return 5;
    std::cout << "PASS cached startup notice, deferred notification and busy install guard (no network)\n";
    return 0;
}
