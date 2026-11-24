#include "RecoveryFiles.h"
#include <QJsonDocument>
#include <QJsonObject>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
namespace luma {
namespace {
const auto marker = ".farecorder-recovery.json";
bool samePath(const QString &a, const QString &b) {
#ifdef Q_OS_WIN
    return a.compare(b, Qt::CaseInsensitive) == 0;
#else
    return a == b;
#endif
}
bool linked(const QString &path) {
    if (QFileInfo(path).isSymLink()) return true;
#ifdef Q_OS_WIN
    const auto flags = GetFileAttributesW(reinterpret_cast<LPCWSTR>(path.utf16()));
    return flags != INVALID_FILE_ATTRIBUTES && (flags & FILE_ATTRIBUTE_REPARSE_POINT);
#else
    return false;
#endif
}
bool linkedAncestor(QString path) {
    path = QFileInfo(path).absoluteFilePath();
    for (;;) {
        if (linked(path)) return true;
        const auto parent = QFileInfo(path).dir().absolutePath();
        if (samePath(path, parent)) return false;
        path = parent;
    }
}
bool contains(const QString &root, const QString &path) {
    return samePath(root, path) || path.startsWith(root + '/',
#ifdef Q_OS_WIN
                                                 Qt::CaseInsensitive
#else
                                                 Qt::CaseSensitive
#endif
    );
}
QString resolvedFile(const QString &path) {
    const QFileInfo file(path);
    if (file.exists()) return file.canonicalFilePath();
    const auto parent = file.dir().canonicalPath();
    return QDir(parent.isEmpty() ? file.absolutePath() : parent).filePath(file.fileName());
}
QString cleanup(const QString &root, const QString &folder) {
    const auto canonicalRoot = QFileInfo(root).canonicalFilePath();
    const auto canonicalFolder = QFileInfo(folder).canonicalFilePath();
    if (canonicalRoot.isEmpty() || canonicalFolder.isEmpty() ||
        linkedAncestor(root) || linkedAncestor(folder) ||
        !samePath(QFileInfo(canonicalFolder).dir().absolutePath(), canonicalRoot))
        return "복구 폴더의 소유 경로를 확인하지 못했습니다.";
    QFile ownership(QDir(folder).filePath(marker));
    if (!ownership.open(QIODevice::ReadOnly) || ownership.size() > 4096)
        return "복구 폴더의 식별 정보를 확인하지 못했습니다.";
    const auto data = QJsonDocument::fromJson(ownership.readAll()).object();
    ownership.close();
    if (data["app"].toString() != "FARecorder" ||
        data["session"].toString() != QFileInfo(canonicalFolder).fileName())
        return "FARecorder 복구 폴더로 확인되지 않아 보존했습니다.";
    QStringList pending{canonicalFolder};
    while (!pending.isEmpty()) {
        QDir directory(pending.takeLast());
        if (!directory.isReadable()) return "복구 폴더를 읽을 수 없습니다.";
        for (const auto &entry : directory.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot |
                                                        QDir::Hidden | QDir::System)) {
            if (linked(entry.absoluteFilePath())) return "복구 폴더 안의 연결된 경로는 삭제하지 않았습니다.";
            if (entry.isDir()) pending.append(entry.absoluteFilePath());
        }
    }
    // Only a marked, direct child of the configured Recovery root reaches here.
    return QDir(canonicalFolder).removeRecursively() ? QString() : "복구 원본 일부를 삭제하지 못했습니다.";
}
} // namespace
bool markRecoveryFolder(const QString &folder) {
    QSaveFile file(QDir(folder).filePath(marker));
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) return false;
    const auto bytes = QJsonDocument(QJsonObject{{"app", "FARecorder"},
        {"session", QFileInfo(folder).fileName()}}).toJson(QJsonDocument::Compact);
    return file.write(bytes) == bytes.size() && file.commit();
}
FileSaveResult saveRecordingAndCleanRecovery(const QString &source, const QString &target,
                                             bool overwrite, const QString &root,
                                             const QString &folder) {
    if (!folder.isEmpty()) {
        const auto canonicalRoot = QFileInfo(root).canonicalFilePath();
        if (!canonicalRoot.isEmpty() && contains(canonicalRoot, resolvedFile(target)))
            return {{}, "최종 영상은 복구 폴더 밖의 저장 위치를 선택해 주세요."};
    }
    auto result = saveRecordingCopy(source, target, overwrite);
    if (!result.error.isEmpty() || result.needsOverwrite || result.file.isEmpty() || folder.isEmpty())
        return result;
    const auto sessionPath = QFileInfo(folder).canonicalFilePath();
    const auto sourcePath = QFileInfo(source).canonicalFilePath();
    if (sessionPath.isEmpty() || sourcePath.isEmpty() || !contains(sessionPath, sourcePath)) {
        result.cleanupWarning = "저장 원본이 해당 복구 세션에 속하지 않아 복구본을 보존했습니다.";
        return result;
    }
    result.cleanupWarning = cleanup(root, folder);
    return result;
}
} // namespace luma
