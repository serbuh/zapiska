#pragma once

#include <QMainWindow>

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
    void populateChannels();

    QLabel *deviceStateLabel = nullptr;
    QLabel *centerFrequencyLabel = nullptr;
    QLabel *sampleRateLabel = nullptr;
    QTableWidget *channelTable = nullptr;
};

