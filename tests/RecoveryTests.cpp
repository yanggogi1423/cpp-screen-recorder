#include "core/RecoveryFiles.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <filesystem>
#include <iostream>
void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
void write(const QString &path, const QByteArray &bytes) {
    QFile file(path); require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "fixture write failed");
}
QByteArray read(const QString &path) {
    QFile file(path); require(file.open(QIODevice::ReadOnly), "fixture read failed"); return file.readAll();
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir temporary;
        require(temporary.isValid(), "temporary folder unavailable");
        const auto root = QDir(QFileInfo(temporary.path()).canonicalFilePath()).filePath("Recovery");
        QDir().mkpath(root);
        auto session = [&](const QString &name) {
            const auto folder = QDir(root).filePath(name);
            require(QDir().mkpath(folder) && luma::markRecoveryFolder(folder), "ownership marker failed");
            write(QDir(folder).filePath("clip.mp4"), "complete-video-bytes");
            write(QDir(folder).filePath("audio.wav"), QByteArray(10000, 'a'));
            return folder;
        };
        auto folder = session("successful-save");
        auto result = luma::saveRecordingAndCleanRecovery(QDir(folder).filePath("clip.mp4"),
            temporary.filePath("saved.mp4"), false, root, folder);
        require(result.error.isEmpty() && result.cleanupWarning.isEmpty() && !QFileInfo::exists(folder) &&
                read(result.file) == "complete-video-bytes", "successful save did not clean recovery or lost final video");
        folder = session("overwrite");
        const auto target = temporary.filePath("existing.mp4"); write(target, "old-video");
        result = luma::saveRecordingAndCleanRecovery(QDir(folder).filePath("clip.mp4"), target, false, root, folder);
        require(result.needsOverwrite && QFileInfo::exists(folder) && read(target) == "old-video", "unconfirmed overwrite cleaned recovery");
        result = luma::saveRecordingAndCleanRecovery(QDir(folder).filePath("clip.mp4"), target, true, root, folder);
        require(result.error.isEmpty() && result.cleanupWarning.isEmpty() && !QFileInfo::exists(folder) &&
                read(target) == "complete-video-bytes", "confirmed overwrite cleanup failed");
        folder = session("failed-save");
        result = luma::saveRecordingAndCleanRecovery(QDir(folder).filePath("clip.mp4"),
            temporary.filePath("missing/clip.mp4"), false, root, folder);
        require(!result.error.isEmpty() && read(QDir(folder).filePath("clip.mp4")) == "complete-video-bytes", "failed save lost recovery");
        result = luma::saveRecordingAndCleanRecovery(QDir(folder).filePath("clip.mp4"),
            QDir(folder).filePath("final.mp4"), false, root, folder);
        require(!result.error.isEmpty() && QFileInfo::exists(folder), "saving into temporary Recovery was accepted");
        const auto unowned = QDir(root).filePath("unidentified-folder"); QDir().mkpath(unowned);
        write(QDir(unowned).filePath("clip.mp4"), "unidentified-video");
        result = luma::saveRecordingAndCleanRecovery(QDir(unowned).filePath("clip.mp4"),
            temporary.filePath("unowned-saved.mp4"), false, root, unowned);
        require(result.error.isEmpty() && !result.cleanupWarning.isEmpty() && QFileInfo::exists(unowned), "unidentified folder was deleted");
        const auto outside = temporary.filePath("outside"); QDir().mkpath(outside);
        require(luma::markRecoveryFolder(outside), "outside marker failed");
        write(QDir(outside).filePath("clip.mp4"), "outside-video");
        result = luma::saveRecordingAndCleanRecovery(QDir(outside).filePath("clip.mp4"),
            temporary.filePath("outside-saved.mp4"), false, root, outside);
        require(!result.cleanupWarning.isEmpty() && QFileInfo::exists(outside), "cleanup escaped its configured Recovery root");
        folder = session("unrelated-source");
        result = luma::saveRecordingAndCleanRecovery(QDir(outside).filePath("clip.mp4"),
            temporary.filePath("unrelated-final.mp4"), false, root, folder);
        require(!result.cleanupWarning.isEmpty() && QFileInfo::exists(folder), "an unrelated save deleted a pending session");
        folder = session("edited-recording");
        write(QDir(folder).filePath("trimmed.mp4"), "edited-video");
        result = luma::saveRecordingAndCleanRecovery(QDir(folder).filePath("trimmed.mp4"),
            temporary.filePath("edited-final.mp4"), false, root, folder);
        require(result.cleanupWarning.isEmpty() && !QFileInfo::exists(folder) && read(result.file) == "edited-video", "edited save did not clean originals");
        folder = session("linked-folder");
        write(QDir(outside).filePath("keep.txt"), "external-data");
        std::error_code error;
        const auto link = QDir(folder).filePath("external-link");
        std::filesystem::create_directory_symlink(std::filesystem::u8path(outside.toUtf8().constData()),
            std::filesystem::u8path(link.toUtf8().constData()), error);
        if (!error) {
            result = luma::saveRecordingAndCleanRecovery(QDir(folder).filePath("clip.mp4"),
                temporary.filePath("linked-saved.mp4"), false, root, folder);
            require(!result.cleanupWarning.isEmpty() && QFileInfo::exists(folder) &&
                read(QDir(outside).filePath("keep.txt")) == "external-data", "linked cleanup affected external data");
            std::filesystem::remove(std::filesystem::u8path(link.toUtf8().constData()), error);
        } else std::cout << "SKIP symlink creation unavailable: " << error.message() << '\n';
        std::cout << "PASS saved/edited cleanup, overwrite confirmation, failed save preservation, ownership and path guards\n";
        return 0;
    } catch (const std::exception &ex) { std::cerr << ex.what() << '\n'; return 1; }
}
