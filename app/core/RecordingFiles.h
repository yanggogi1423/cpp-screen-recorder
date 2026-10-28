#pragma once
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QSaveFile>
#include <QTemporaryFile>
#include <algorithm>

namespace luma {
inline qint64 estimatedRecordingBytes(qint64 videoBytes, qint64 frames, int fps, int sources) {
    // Final mux copies H.264 and encodes each audio track at 192 kbit/s.
    // Two or more sources also produce a default playback mix.
    const int tracks = sources + (sources > 1 ? 1 : 0);
    return std::max<qint64>(0, videoBytes) +
           std::max<qint64>(0, frames) * 24000 * tracks / std::max(1, fps);
}
inline QString recordingSizeText(qint64 bytes) {
    return QLocale().formattedDataSize(std::max<qint64>(0, bytes), 1);
}
struct FileSaveResult {
    QString file, error;
    bool needsOverwrite = false;
};
inline FileSaveResult saveRecordingCopy(const QString &source, const QString &target,
                                        bool overwrite) {
    const QFileInfo inputInfo(source), outputInfo(target);
    if (!inputInfo.isFile())
        return {{}, "녹화 원본을 찾을 수 없습니다."};
    if (outputInfo.exists() && inputInfo.canonicalFilePath() == outputInfo.canonicalFilePath())
        return {source, {}};
    if (outputInfo.exists() && !overwrite)
        return {{}, {}, true};
    QFile input(source);
    if (!input.open(QIODevice::ReadOnly))
        return {{}, "녹화 원본을 읽을 수 없습니다: " + input.errorString()};
    auto copy = [&input](QIODevice &output) {
        while (!input.atEnd()) {
            const auto chunk = input.read(4 * 1024 * 1024);
            if ((chunk.isEmpty() && input.error() != QFile::NoError) ||
                output.write(chunk) != chunk.size())
                return false;
        }
        return true;
    };
    if (overwrite) {
        QSaveFile output(target);
        // Never use direct-write fallback: failure must preserve the existing file.
        if (!output.open(QIODevice::WriteOnly) || !copy(output) || !output.commit())
            return {{}, "파일 저장에 실패했습니다. 원본은 보존했습니다: " + output.errorString()};
    } else {
        QTemporaryFile output(outputInfo.dir().filePath(".farecorder-save-XXXXXX"));
        if (!output.open() || !copy(output) || !output.flush())
            return {{}, "파일 저장에 실패했습니다. 원본은 보존했습니다: " + output.errorString()};
        output.close();
        // QFile::rename refuses to replace a file created during the copy.
        if (!output.rename(target))
            return {{}, "파일을 저장하지 못했습니다. 저장 위치와 파일 이름을 확인해 주세요."};
        output.setAutoRemove(false);
    }
    return {target, {}};
}
} // namespace luma
