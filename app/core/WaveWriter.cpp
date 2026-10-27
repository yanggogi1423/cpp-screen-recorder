#include "WaveWriter.h"
#include <QDataStream>
#include <QMutexLocker>
#include <algorithm>

namespace luma {
WaveWriter::WaveWriter(const QString &path) : file_(path) {
    if (!file_.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        throw std::runtime_error(file_.errorString().toStdString());
    QDataStream out(&file_);
    out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData("RF64", 4);
    out << quint32(0xffffffff);
    out.writeRawData("WAVEds64", 8);
    out << quint32(28) << quint64(0) << quint64(0) << quint64(0) << quint32(0);
    out.writeRawData("fmt ", 4);
    out << quint32(16) << quint16(1) << quint16(2) << quint32(48000) << quint32(192000)
        << quint16(4) << quint16(16);
    out.writeRawData("data", 4);
    out << quint32(0xffffffff);
    if (out.status() != QDataStream::Ok)
        throw std::runtime_error("오디오 파일 헤더를 저장하지 못했습니다.");
}
void WaveWriter::write(const char *data, qint64 size) {
    if (file_.write(data, size) != size)
        throw std::runtime_error(file_.errorString().toStdString());
}
void WaveWriter::pad(qint64 target) {
    static const QByteArray zeros(16384, 0);
    qint64 bytes = std::max<qint64>(0, target - frames_) * 4;
    while (bytes > 0) {
        const auto n = std::min<qint64>(bytes, zeros.size());
        write(zeros.constData(), n);
        bytes -= n;
    }
    frames_ = std::max(frames_, target);
}
void WaveWriter::append(const AudioPacket &packet, qint64 originNs) {
    QMutexLocker lock(&mutex_);
    if (finished_)
        return;
    auto pos = (packet.timestampNs - originNs) * 48000 / 1000000000;
    if (pos > (clockNs() - originNs) * 48000 / 1000000000 + 48000)
        throw std::runtime_error("오디오 장치의 타임스탬프가 올바르지 않습니다.");
    const auto skip = std::clamp<qint64>(frames_ - pos, 0, packet.pcm.size() / 4);
    if (pos > frames_)
        pad(pos);
    const auto bytes = packet.pcm.size() - skip * 4;
    if (bytes > 0) {
        write(packet.pcm.constData() + skip * 4, bytes);
        frames_ += bytes / 4;
    }
}
void WaveWriter::finish(qint64 durationNs) {
    QMutexLocker lock(&mutex_);
    if (finished_)
        return;
    pad(durationNs * 48000 / 1000000000);
    const auto size = file_.size();
    file_.seek(20);
    QDataStream out(&file_);
    out.setByteOrder(QDataStream::LittleEndian);
    out << quint64(size - 8) << quint64(size - 80) << quint64(frames_);
    if (!file_.flush() || out.status() != QDataStream::Ok)
        throw std::runtime_error("오디오 파일을 마무리하지 못했습니다.");
    file_.close();
    finished_ = true;
}
WaveWriter::~WaveWriter() {
    try {
        finish(0);
    } catch (...) {
    }
}
} // namespace luma
