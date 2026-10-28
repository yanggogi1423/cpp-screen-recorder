#include "updates/UpdateCatalog.h"
#include <QCoreApplication>
#include <QJsonObject>
#include <iostream>
#include <stdexcept>
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
QJsonObject release(QString version, bool prerelease = true) {
    const QString name = "FARecorder-" + version + "-windows-x64.zip";
    return {
        {"tag_name", "v" + version},
        {"draft", false},
        {"prerelease", prerelease},
        {"body", "Changes"},
        {"assets", QJsonArray{QJsonObject{
                       {"name", name},
                       {"state", "uploaded"},
                       {"size", 1024},
                       {"digest", "sha256:" + QString(64, 'a')},
                       {"browser_download_url",
                        "https://github.com/yanggogi1423/cpp-screen-recorder/releases/download/v" +
                            version + "/" + name}}}}};
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        require(updates::compareVersions("0.5.0-beta.10", "0.5.0-beta.2") > 0,
                "numeric beta comparison");
        require(updates::compareVersions("v0.5.0", "0.5.0-rc.20") > 0,
                "stable must follow prerelease");
        require(updates::compareVersions("0.5.0-alpha.2", "0.5.0-beta.1") < 0, "channel ordering");
        require(!updates::validVersion("0.5.0-beta.01") && !updates::validVersion("../0.5.0") &&
                    !updates::validVersion("0.5.0;evil") && !updates::validVersion("0.5"),
                "invalid versions accepted");
        const QJsonArray catalog{release("0.5.0-beta.2"), release("0.5.0-beta.10"),
                                 release("0.4.1", false)};
        require(updates::selectRelease(catalog, "0.4.0-beta.3", "windows-x64", true).version ==
                    "0.5.0-beta.10",
                "best semantic version not selected");
        require(updates::selectRelease(catalog, "0.4.0-beta.3", "windows-x64", false).version ==
                    "0.4.1",
                "beta leaked into stable channel");
        require(!updates::selectRelease(catalog, "0.6.0", "windows-x64", true).valid(),
                "downgrade offered");
        require(!updates::selectRelease(catalog, "0.4.0", "macos-arm64", true).valid(),
                "wrong architecture offered");
        for (const QString key : {"digest", "state", "browser_download_url", "size", "name"}) {
            auto item = release("0.5.0-beta.1");
            auto asset = item["assets"].toArray().first().toObject();
            asset[key] = key == "size" ? QJsonValue(-1) : QJsonValue("invalid");
            item["assets"] = QJsonArray{asset};
            require(!updates::selectRelease({item}, "0.4.0", "windows-x64", true).valid(),
                    "invalid asset accepted");
        }
        for (const QString url :
             {"https://evil.example/file.zip", "http://github.com/file.zip",
              "https://github.com/yanggogi1423/cpp-screen-recorder/releases/download/v0.5.0-beta.1/"
              "FARecorder-0.5.0-beta.1-windows-x64.zip?secret=x",
              "https://user@github.com/yanggogi1423/cpp-screen-recorder/releases/download/"
              "v0.5.0-beta.1/FARecorder-0.5.0-beta.1-windows-x64.zip"}) {
            auto item = release("0.5.0-beta.1");
            auto asset = item["assets"].toArray().first().toObject();
            asset["browser_download_url"] = url;
            item["assets"] = QJsonArray{asset};
            require(!updates::selectRelease({item}, "0.4.0", "windows-x64", true).valid(),
                    "untrusted asset URL accepted");
        }
        auto draft = release("9.0.0");
        draft["draft"] = true;
        require(!updates::selectRelease({draft}, "0.4.0", "windows-x64", true).valid(),
                "draft offered");
        std::cout
            << "PASS: version ordering, release channels, hashes, asset origin and architecture\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
