#pragma once

#include "SdrSource.h"

#include <QImage>
#include <QPoint>
#include <QString>
#include <QVector>
#include <QWidget>

class QMouseEvent;
class QWheelEvent;

struct WaterfallChannelMarker
{
    QString name;
    qint64 frequencyHz = 0;
    bool selected = false;
    bool playing = false;
    bool unsquelched = false;
};

class WaterfallWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit WaterfallWidget(QWidget *parent = nullptr);

    QSize minimumSizeHint() const override;
    void setChannelMarkers(const QVector<WaterfallChannelMarker> &markers);
    void setSpectrumFrame(const zapiska::SdrSpectrumFrame &frame);
    void setHorizontalZoom(double zoom);
    double horizontalZoom() const;
    void setHorizontalPanRatio(double ratio);
    double horizontalPanRatio() const;

signals:
    void horizontalViewChanged(double zoom, double panRatio);

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QRgb colorForPower(float powerDbfs) const;
    int xForFrequency(qint64 frequencyHz, int width) const;
    bool setHorizontalView(double zoom, double panRatio, bool notify);
    double clampedHorizontalZoom(double zoom) const;
    double clampedHorizontalPanRatio(double ratio, double zoom) const;
    double visibleFrequencySpanHz() const;
    double visibleFrequencyStartHz() const;
    double visibleImageStartFraction() const;

    QVector<WaterfallChannelMarker> channelMarkers;
    zapiska::SdrSpectrumFrame latestFrame;
    QImage waterfallImage;
    QPoint lastPanPoint;
    double horizontalZoomLevel = 1.0;
    double horizontalPan = 0.5;
    bool hasFrame = false;
    bool panning = false;
};
