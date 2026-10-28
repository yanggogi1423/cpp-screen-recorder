#include "UpdateCatalog.h"
#include <QJsonObject>
#include <QRegularExpression>
#include <QSysInfo>
#include <array>
#include <stdexcept>
namespace updates {
namespace {
const QRegularExpression versionPattern("^v?(0|[1-9][0-9]{0,5})\\.(0|[1-9][0-9]{0,5})\\.(0|[1-9][0-"
                                        "9]{0,5})(?:-(alpha|beta|rc)\\.(0|[1-9][0-9]{0,5}))?$");
std::array<int, 5> key(const QString &version) {
    auto match = versionPattern.match(version);
    if (!match.hasMatch())
        throw std::runtime_error("Invalid release version");
    const auto channel = match.captured(4);
    const int rank = channel.isEmpty() ? 3 : channel == "rc" ? 2 : channel == "beta" ? 1 : 0;
    return {match.captured(1).toInt(), match.captured(2).toInt(), match.captured(3).toInt(), rank,
            match.captured(5).toInt()};
}
} // namespace
bool validVersion(QString version) {
    return versionPattern.match(version).hasMatch();
}
int compareVersions(QString left, QString right) {
    const auto a = key(left), b = key(right);
    return a < b ? -1 : a > b ? 1 : 0;
}
QString platform() {
#ifdef Q_OS_WIN
    return QSysInfo::currentCpuArchitecture() == "x86_64" ? "windows-x64" : QString();
#elif defined(Q_OS_MACOS)
    return QSysInfo::currentCpuArchitecture() == "arm64" ? "macos-arm64" : "macos-x64";
#else
    return {};
#endif
}
Release selectRelease(const QJsonArray &releases, const QString &current, const QString &platform,
                      bool beta) {
    Release best;
    if (platform.isEmpty() || !validVersion(current))
        return best;
    for (const auto &value : releases) {
        const auto release = value.toObject();
        const QString tag = release["tag_name"].toString();
        if (release["draft"].toBool() ||
            (!beta && (release["prerelease"].toBool() || tag.contains('-'))) ||
            !tag.startsWith('v') || !validVersion(tag) || compareVersions(tag, current) <= 0 ||
            (best.valid() && compareVersions(tag, best.version) <= 0))
            continue;
        const auto version = tag.mid(1);
        const QString name = "FARecorder-" + version + "-" + platform + ".zip";
        const QString path =
            "/yanggogi1423/cpp-screen-recorder/releases/download/" + tag + "/" + name;
        for (const auto &entry : release["assets"].toArray()) {
            const auto asset = entry.toObject();
            const QUrl url(asset["browser_download_url"].toString());
            const QString digest = asset["digest"].toString();
            const qint64 size = asset["size"].toInteger();
            if (asset["name"].toString() != name || asset["state"].toString() != "uploaded" ||
                size <= 0 || size > 512LL * 1024 * 1024 || url.scheme() != "https" ||
                url.host() != "github.com" || (url.port() != -1 && url.port() != 443) ||
                url.path() != path || !url.query().isEmpty() || !url.fragment().isEmpty() ||
                !url.userInfo().isEmpty() ||
                !QRegularExpression("^sha256:[a-f0-9]{64}$").match(digest).hasMatch())
                continue;
            best = {version,
                    release["body"].toString().left(64000),
                    name,
                    digest.mid(7),
                    url,
                    QUrl("https://github.com/yanggogi1423/cpp-screen-recorder/releases/tag/" + tag),
                    size};
        }
    }
    return best;
}
} // namespace updates
