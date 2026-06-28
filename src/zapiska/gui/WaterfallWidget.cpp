#include "WaterfallWidget.h"

#include <QPainter>
#include <QPaintEvent>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr int WaterfallRows = 180;
constexpr float MinimumDisplayDbfs = -120.0F;
constexpr float MaximumDisplayDbfs = -20.0F;

int interpolate(int start, int end, float ratio)
{
    return static_cast<int>(
        static_cast<float>(start) + (static_cast<float>(end - start) * ratio));
}

QRgb interpolateColor(const QColor &start, const QColor &end, float ratio)
{
    const float clampedRatio = std::clamp(ratio, 0.0F, 1.0F);
    return qRgb(
        interpolate(start.red(), end.red(), clampedRatio),
        interpolate(start.green(), end.green(), clampedRatio),
        interpolate(start.blue(), end.blue(), clampedRatio));
}

} // namespace

WaterfallWidget::WaterfallWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(WaterfallRows);
}

QSize WaterfallWidget::minimumSizeHint() const
{
    return QSize(640, WaterfallRows);
}

void WaterfallWidget::setChannelMarkers(const QVector<WaterfallChannelMarker> &markers)
{
    channelMarkers = markers;
    update();
}

void WaterfallWidget::setSpectrumFrame(const zapiska::SdrSpectrumFrame &frame)
{
    if (frame.powerDbfs.isEmpty()) {
        return;
    }

    latestFrame = frame;
    hasFrame = true;

    const int bins = frame.powerDbfs.size();
    if (waterfallImage.size() != QSize(bins, WaterfallRows)) {
        waterfallImage = QImage(bins, WaterfallRows, QImage::Format_RGB32);
        waterfallImage.fill(qRgb(4, 6, 10));
    }

    const int bytesPerLine = waterfallImage.bytesPerLine();
    for (int row = waterfallImage.height() - 1; row > 0; --row) {
        std::memmove(waterfallImage.scanLine(row), waterfallImage.constScanLine(row - 1), bytesPerLine);
    }

    auto *topLine = reinterpret_cast<QRgb *>(waterfallImage.scanLine(0));
    for (int bin = 0; bin < bins; ++bin) {
        topLine[bin] = colorForPower(frame.powerDbfs.at(bin));
    }

    update();
}

void WaterfallWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.fillRect(rect(), QColor(4, 6, 10));

    if (!waterfallImage.isNull()) {
        painter.drawImage(rect(), waterfallImage);
    }

    if (!hasFrame || latestFrame.sampleRateHz <= 0) {
        return;
    }

    painter.setRenderHint(QPainter::Antialiasing, false);
    const QRect plotRect = rect();
    for (const auto &marker : channelMarkers) {
        const int x = xForFrequency(marker.frequencyHz, plotRect.width());
        if (x < 0 || x >= plotRect.width()) {
            continue;
        }

        if (marker.playing) {
            painter.fillRect(
                QRect(plotRect.left() + x - 3, plotRect.top(), 7, plotRect.height()),
                QColor(80, 255, 190, 40));
        }

        const QColor lineColor = marker.playing
            ? QColor(90, 255, 190, 240)
            : (marker.selected ? QColor(255, 235, 120, 230) : QColor(210, 220, 230, 70));
        painter.setPen(QPen(lineColor, marker.playing ? 3 : (marker.selected ? 2 : 1)));
        painter.drawLine(plotRect.left() + x, plotRect.top(), plotRect.left() + x, plotRect.bottom());

        if (marker.selected) {
            painter.setPen(marker.playing ? QColor(190, 255, 225) : QColor(255, 245, 190));
            painter.drawText(plotRect.left() + x + 3, plotRect.top() + 14, marker.name);
        }
    }
}

QRgb WaterfallWidget::colorForPower(float powerDbfs) const
{
    const float normalized = std::clamp(
        (powerDbfs - MinimumDisplayDbfs) / (MaximumDisplayDbfs - MinimumDisplayDbfs),
        0.0F,
        1.0F);

    if (normalized < 0.25F) {
        return interpolateColor(QColor(3, 5, 12), QColor(15, 32, 120), normalized / 0.25F);
    }
    if (normalized < 0.5F) {
        return interpolateColor(QColor(15, 32, 120), QColor(0, 180, 210), (normalized - 0.25F) / 0.25F);
    }
    if (normalized < 0.75F) {
        return interpolateColor(QColor(0, 180, 210), QColor(245, 220, 40), (normalized - 0.5F) / 0.25F);
    }
    return interpolateColor(QColor(245, 220, 40), QColor(230, 40, 20), (normalized - 0.75F) / 0.25F);
}

int WaterfallWidget::xForFrequency(qint64 frequencyHz, int width) const
{
    if (!hasFrame || latestFrame.sampleRateHz <= 0 || width <= 0) {
        return -1;
    }

    const double startFrequency = static_cast<double>(latestFrame.centerFrequencyHz)
        - (static_cast<double>(latestFrame.sampleRateHz) / 2.0);
    const double offset = static_cast<double>(frequencyHz) - startFrequency;
    const double normalized = offset / static_cast<double>(latestFrame.sampleRateHz);
    return static_cast<int>(std::lround(normalized * static_cast<double>(width - 1)));
}
