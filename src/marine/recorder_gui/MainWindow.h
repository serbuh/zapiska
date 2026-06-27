#pragma once

#include "MarineCore.h"

#include <QMainWindow>
#include <QVector>

class QLabel;
class QProgressBar;
class QTableWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    void buildUi();
    void loadChannels();
    void populateChannels(const QVector<marine::ChannelConfig> &channels);

    QLabel *deviceStateLabel = nullptr;
    QLabel *centerFrequencyLabel = nullptr;
    QLabel *sampleRateLabel = nullptr;
    QLabel *channelCatalogLabel = nullptr;
    QTableWidget *channelTable = nullptr;
};
