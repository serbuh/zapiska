#pragma once

#include "GrOsmoSdrSource.h"
#include "MarineCore.h"

#include <QMainWindow>
#include <QVector>

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QTableWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    void buildUi();
    void loadChannels();
    void populateChannelSelector();
    void refreshChannelTable();
    void updateChannelCatalogLabel();
    bool isChannelVisible(const QString &id) const;

    void toggleSdrConnection();
    void startSdr();
    void stopSdr();
    void handleSdrStateChanged(marine::SdrSourceState state);
    void handleSdrStatsUpdated(const marine::SdrStreamStats &stats);
    void handleSdrError(const QString &message);
    void refreshSdrControls();
    void updateSdrConfigLabels(const marine::SdrSourceConfig &config);

    void addSelectedChannel();
    void removeSelectedChannel();
    void updateRemoveButtonState();

    marine::GrOsmoSdrSource sdrSource;

    QLabel *deviceStateLabel = nullptr;
    QLabel *centerFrequencyLabel = nullptr;
    QLabel *sampleRateLabel = nullptr;
    QLabel *sampleCountLabel = nullptr;
    QLabel *widebandPowerLabel = nullptr;
    QLabel *channelCatalogLabel = nullptr;
    QLabel *sdrStatusLabel = nullptr;
    QPushButton *connectButton = nullptr;
    QPushButton *startButton = nullptr;
    QPushButton *stopButton = nullptr;
    QPushButton *recordButton = nullptr;
    QComboBox *channelSelector = nullptr;
    QPushButton *addChannelButton = nullptr;
    QPushButton *removeChannelButton = nullptr;
    QTableWidget *channelTable = nullptr;

    QVector<marine::ChannelConfig> channelCatalog;
    QVector<marine::ChannelConfig> visibleChannels;
};
