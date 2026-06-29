#include "WaterfallWidget.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

constexpr int WaterfallRows = 180;
constexpr float MinimumDisplayDbfs = -120.0F;
constexpr float MaximumDisplayDbfs = -20.0F;
constexpr double MaximumHorizontalZoom = 64.0;
constexpr double MinimumPanEpsilon = 0.0001;

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

void WaterfallWidget::setHorizontalZoom(double zoom)
{
    setHorizontalView(zoom, horizontalPan, true);
}

double WaterfallWidget::horizontalZoom() const
{
    return horizontalZoomLevel;
}

void WaterfallWidget::setHorizontalPanRatio(double ratio)
{
    setHorizontalView(horizontalZoomLevel, ratio, true);
}

double WaterfallWidget::horizontalPanRatio() const
{
    return horizontalPan;
}

void WaterfallWidget::setSpectrumFrame(const zapiska::SdrSpectrumFrame &frame)
{
    if (frame.powerDbfs.isEmpty()) {
        return;
    }

    latestFrame = frame;
    hasFrame = true;
    setHorizontalView(horizontalZoomLevel, horizontalPan, true);

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
        const double sourceX = visibleImageStartFraction() * static_cast<double>(waterfallImage.width());
        const double sourceWidth = static_cast<double>(waterfallImage.width()) / horizontalZoomLevel;
        const QRectF sourceRect(
            sourceX,
            0.0,
            std::max(1.0, sourceWidth),
            static_cast<double>(waterfallImage.height()));
        painter.drawImage(rect(), waterfallImage, sourceRect);
    }

    if (!hasFrame || latestFrame.sampleRateHz <= 0) {
        return;
    }

    painter.setRenderHint(QPainter::Antialiasing, false);
    const QRect plotRect = rect();
    const QFontMetrics labelMetrics = painter.fontMetrics();
    const int labelSpacing = 6;
    const int labelLaneHeight = labelMetrics.height() + 2;
    const int labelLaneCount = std::max(
        1,
        std::min(6, std::max(1, plotRect.height() / labelLaneHeight)));
    const int bottomLabelBaseline = std::max(
        plotRect.top() + labelMetrics.ascent(),
        plotRect.bottom() - labelMetrics.descent() - 3);
    struct ChannelLabel
    {
        QString text;
        int x = 0;
        int width = 0;
        QColor color;
    };
    QVector<ChannelLabel> labels;
    labels.reserve(channelMarkers.size());

    for (const auto &marker : channelMarkers) {
        const int x = xForFrequency(marker.frequencyHz, plotRect.width());
        if (x < 0 || x >= plotRect.width()) {
            continue;
        }

        if (marker.unsquelched) {
            painter.fillRect(
                QRect(plotRect.left() + x - 3, plotRect.top(), 7, plotRect.height()),
                QColor(255, 135, 30, 60));
        } else if (marker.selected) {
            painter.fillRect(
                QRect(plotRect.left() + x - 3, plotRect.top(), 7, plotRect.height()),
                QColor(80, 255, 190, 40));
        }

        QColor lineColor(210, 220, 230, 70);
        int lineWidth = 1;
        if (marker.unsquelched) {
            lineColor = QColor(255, 135, 30, 245);
            lineWidth = 3;
        } else if (marker.selected) {
            lineColor = QColor(90, 255, 190, 240);
            lineWidth = 3;
        }

        painter.setPen(QPen(lineColor, lineWidth));
        painter.drawLine(plotRect.left() + x, plotRect.top(), plotRect.left() + x, plotRect.bottom());

        QColor textColor(210, 220, 230, 170);
        if (marker.unsquelched) {
            textColor = QColor(255, 230, 190);
        } else if (marker.selected) {
            textColor = QColor(190, 255, 225);
        }

        const int labelWidth = labelMetrics.horizontalAdvance(marker.name);
        const int labelX = std::clamp(
            plotRect.left() + x + 3,
            plotRect.left(),
            std::max(plotRect.left(), plotRect.right() - labelWidth));
        labels.append(ChannelLabel { marker.name, labelX, labelWidth, textColor });
    }

    std::sort(labels.begin(), labels.end(), [](const ChannelLabel &left, const ChannelLabel &right) {
        return left.x < right.x;
    });

    QVector<int> laneRightEdges(labelLaneCount, plotRect.left() - labelSpacing);
    for (auto &label : labels) {
        int lane = 0;
        bool foundLane = false;
        for (int candidate = 0; candidate < laneRightEdges.size(); ++candidate) {
            if (label.x >= laneRightEdges.at(candidate) + labelSpacing) {
                lane = candidate;
                foundLane = true;
                break;
            }
        }

        if (!foundLane) {
            for (int candidate = 1; candidate < laneRightEdges.size(); ++candidate) {
                if (laneRightEdges.at(candidate) < laneRightEdges.at(lane)) {
                    lane = candidate;
                }
            }
            label.x = std::clamp(
                laneRightEdges.at(lane) + labelSpacing,
                plotRect.left(),
                std::max(plotRect.left(), plotRect.right() - label.width));
        }

        const int labelBaseline = std::max(
            plotRect.top() + labelMetrics.ascent(),
            bottomLabelBaseline - (lane * labelLaneHeight));
        laneRightEdges[lane] = label.x + label.width;
        painter.setPen(label.color);
        painter.drawText(label.x, labelBaseline, label.text);
    }
}

void WaterfallWidget::wheelEvent(QWheelEvent *event)
{
    if (!hasFrame || width() <= 1) {
        event->ignore();
        return;
    }

    const double steps = static_cast<double>(event->angleDelta().y()) / 120.0;
    if (std::abs(steps) < 0.01) {
        event->ignore();
        return;
    }

    const double oldZoom = horizontalZoomLevel;
    const double oldVisibleFraction = 1.0 / oldZoom;
    const double oldStartFraction = visibleImageStartFraction();
#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
    const double cursorX = static_cast<double>(event->pos().x());
#else
    const double cursorX = event->position().x();
#endif
    const double cursorFraction = std::clamp(cursorX / static_cast<double>(width() - 1), 0.0, 1.0);
    const double fixedFrequencyFraction = oldStartFraction + cursorFraction * oldVisibleFraction;
    const double zoomFactor = std::pow(1.25, steps);
    const double newZoom = clampedHorizontalZoom(oldZoom * zoomFactor);
    const double newVisibleFraction = 1.0 / newZoom;
    const double newStartFraction = std::clamp(
        fixedFrequencyFraction - cursorFraction * newVisibleFraction,
        0.0,
        1.0 - newVisibleFraction);
    const double newPan = newZoom <= 1.0 + MinimumPanEpsilon
        ? 0.5
        : newStartFraction / (1.0 - newVisibleFraction);

    setHorizontalView(newZoom, newPan, true);
    event->accept();
}

void WaterfallWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && horizontalZoomLevel > 1.0 + MinimumPanEpsilon) {
        panning = true;
        lastPanPoint = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

void WaterfallWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (!panning || width() <= 1 || horizontalZoomLevel <= 1.0 + MinimumPanEpsilon) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    const int deltaX = event->pos().x() - lastPanPoint.x();
    lastPanPoint = event->pos();

    const double visibleFraction = 1.0 / horizontalZoomLevel;
    const double panDenominator = 1.0 - visibleFraction;
    if (panDenominator > MinimumPanEpsilon) {
        const double panDelta = -static_cast<double>(deltaX)
            * visibleFraction
            / (static_cast<double>(width()) * panDenominator);
        setHorizontalView(horizontalZoomLevel, horizontalPan + panDelta, true);
    }

    event->accept();
}

void WaterfallWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && panning) {
        panning = false;
        unsetCursor();
        event->accept();
        return;
    }

    QWidget::mouseReleaseEvent(event);
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

    const double startFrequency = visibleFrequencyStartHz();
    const double offset = static_cast<double>(frequencyHz) - startFrequency;
    const double normalized = offset / visibleFrequencySpanHz();
    if (normalized < 0.0 || normalized > 1.0) {
        return -1;
    }

    return static_cast<int>(std::lround(normalized * static_cast<double>(width - 1)));
}

bool WaterfallWidget::setHorizontalView(double zoom, double panRatio, bool notify)
{
    const double nextZoom = clampedHorizontalZoom(zoom);
    const double nextPan = clampedHorizontalPanRatio(panRatio, nextZoom);
    if (std::abs(horizontalZoomLevel - nextZoom) < 0.001
        && std::abs(horizontalPan - nextPan) < 0.001) {
        return false;
    }

    horizontalZoomLevel = nextZoom;
    horizontalPan = nextPan;
    update();

    if (notify) {
        emit horizontalViewChanged(horizontalZoomLevel, horizontalPan);
    }
    return true;
}

double WaterfallWidget::clampedHorizontalZoom(double zoom) const
{
    double maximumZoom = MaximumHorizontalZoom;
    if (hasFrame && !latestFrame.powerDbfs.isEmpty()) {
        maximumZoom = std::max(1.0, std::min(maximumZoom, static_cast<double>(latestFrame.powerDbfs.size()) / 4.0));
    }
    return std::clamp(zoom, 1.0, maximumZoom);
}

double WaterfallWidget::clampedHorizontalPanRatio(double ratio, double zoom) const
{
    if (zoom <= 1.0 + MinimumPanEpsilon) {
        return 0.5;
    }
    return std::clamp(ratio, 0.0, 1.0);
}

double WaterfallWidget::visibleFrequencySpanHz() const
{
    if (!hasFrame || latestFrame.sampleRateHz <= 0) {
        return 1.0;
    }
    return static_cast<double>(latestFrame.sampleRateHz) / horizontalZoomLevel;
}

double WaterfallWidget::visibleFrequencyStartHz() const
{
    if (!hasFrame || latestFrame.sampleRateHz <= 0) {
        return 0.0;
    }

    const double fullStartFrequency = static_cast<double>(latestFrame.centerFrequencyHz)
        - (static_cast<double>(latestFrame.sampleRateHz) / 2.0);
    const double hiddenSpan = static_cast<double>(latestFrame.sampleRateHz) - visibleFrequencySpanHz();
    return fullStartFrequency + hiddenSpan * horizontalPan;
}

double WaterfallWidget::visibleImageStartFraction() const
{
    if (horizontalZoomLevel <= 1.0 + MinimumPanEpsilon) {
        return 0.0;
    }
    return horizontalPan * (1.0 - (1.0 / horizontalZoomLevel));
}
