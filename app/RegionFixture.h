#pragma once
#include <QBackingStore>
#include <QExposeEvent>
#include <QPainter>
#include <QWindow>

// Silent, process-owned capture target. No desktop or microphone is sampled.
class RegionFixture final : public QWindow {
    QBackingStore store_{this};
    void exposeEvent(QExposeEvent *) override {
        if (!isExposed())
            return;
        store_.resize(size());
        const QRegion region(QRect(QPoint(), size()));
        store_.beginPaint(region);
        {
            QPainter painter(store_.paintDevice());
            painter.fillRect(0, 0, width() / 2, height(), QColor(240, 20, 20));
            painter.fillRect(width() / 2, 0, width() - width() / 2, height(), QColor(20, 20, 240));
            painter.setPen(QPen(QColor(20, 240, 20), 20));
            painter.drawRect(0, 0, width(), height());
        }
        store_.endPaint();
        store_.flush(region);
    }

  public:
    RegionFixture() {
        setSurfaceType(QSurface::RasterSurface);
        setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
        setTitle("FARecorder silent region fixture");
        setGeometry(100, 100, 640, 360);
    }
};
