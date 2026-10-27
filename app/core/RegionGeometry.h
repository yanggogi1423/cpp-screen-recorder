#pragma once
#include <QRect>
#include <QRectF>
#include <QSize>
#include <algorithm>
#include <cmath>

namespace luma {
// Crop coordinates are physical pixels relative to one display; Qt overlays
// use global logical coordinates. Keep the two scale axes independent.
inline QRect regionFromLogical(const QRectF &selection, const QRect &screen, QSize pixels) {
    if (screen.isEmpty() || pixels.isEmpty())
        return {};
    const auto selected = selection.normalized().intersected(QRectF(screen));
    if (selected.isEmpty())
        return {};
    const double sx = double(pixels.width()) / screen.width();
    const double sy = double(pixels.height()) / screen.height();
    const int x = std::clamp(int(std::floor((selected.x() - screen.x()) * sx)), 0, pixels.width());
    const int y = std::clamp(int(std::floor((selected.y() - screen.y()) * sy)), 0, pixels.height());
    const int right =
        std::clamp(int(std::ceil((selected.right() - screen.x()) * sx)), x, pixels.width());
    const int bottom =
        std::clamp(int(std::ceil((selected.bottom() - screen.y()) * sy)), y, pixels.height());
    return QRect(x, y, (right - x) & ~1, (bottom - y) & ~1);
}
inline QRect moveRegionPixels(QRect crop, QPoint target, QSize pixels) {
    if (crop.isEmpty() || crop.width() > pixels.width() || crop.height() > pixels.height())
        return {};
    crop.moveTopLeft(QPoint(std::clamp(target.x(), 0, pixels.width() - crop.width()),
                            std::clamp(target.y(), 0, pixels.height() - crop.height())));
    return crop;
}
inline QRectF regionToLogical(QRect crop, const QRect &screen, QSize pixels) {
    if (screen.isEmpty() || pixels.isEmpty())
        return {};
    return QRectF(screen.x() + double(crop.x()) * screen.width() / pixels.width(),
                  screen.y() + double(crop.y()) * screen.height() / pixels.height(),
                  double(crop.width()) * screen.width() / pixels.width(),
                  double(crop.height()) * screen.height() / pixels.height());
}
} // namespace luma
