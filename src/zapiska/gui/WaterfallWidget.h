#pragma once

#include "SdrSource.h"

#include <QImage>
#include <QString>
#include <QVector>
#include <QWidget>

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
public:
    explicit WaterfallWidget(QWidget *parent = nullptr);

    QSize minimumSizeHint() const override;
    void setChannelMarkers(const QVector<WaterfallChannelMarker> &markers);
    void setSpectrumFrame(const zapiska::SdrSpectrumFrame &frame);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QRgb colorForPower(float powerDbfs) const;
    int xForFrequency(qint64 frequencyHz, int width) const;

    QVector<WaterfallChannelMarker> channelMarkers;
    zapiska::SdrSpectrumFrame latestFrame;
    QImage waterfallImage;
    bool hasFrame = false;
};
