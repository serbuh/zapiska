#pragma once

#include "GrOsmoSdrSource.h"
#include "MarineCore.h"

#include <QHash>
#include <QMainWindow>
#include <QSet>
#include <QVector>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;
class WaterfallWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    void buildUi();
    void loadChannels();
    void refreshChannelTable();
    void refreshChannelVisibility();
    void refreshWaterfallChannels();
    void refreshFftControls();
    void initializeSelectedChannels();
    bool loadSelectedChannelsFromSettings();
    void saveSelectedChannelsToSettings() const;
    void loadChannelMonitorSettings();
    void saveChannelMonitorSettings() const;
    void updateChannelSelectionControls();
    bool isChannelSelected(const QString &id) const;
    bool channelMonitorEnabledForChannel(const QString &id) const;
    void updateChannelMonitorButton(int row);
    int selectedChannelCount() const;

    void toggleSdrConnection();
    void toggleSdrStreaming();
    void startSdr();
    void stopSdr();
    void toggleLiveAudio();
    bool applyLiveAudioDesiredState();
    void toggleRecording();
    void handleSdrStateChanged(marine::SdrSourceState state);
    void handleSdrStatsUpdated(const marine::SdrStreamStats &stats);
    void handleSpectrumUpdated(const marine::SdrSpectrumFrame &frame);
    void handleSdrError(const QString &message);
    void refreshSdrControls();
    void updateSdrConfigLabels(const marine::SdrSourceConfig &config);
    marine::SdrSourceConfig buildSdrConfig() const;
    void updateChannelMeters(const marine::SdrStreamStats &stats);
    void resetChannelDisplay(int row);
    int channelRow(const QString &id) const;
    bool channelHasRecordableAudio(const marine::SdrStreamStats &stats, const QString &id) const;
    QString nextRecordingPath() const;
    marine::SdrSquelchMode squelchModeForChannel(const QString &id) const;
    double squelchThresholdForChannel(const QString &id) const;
    void applyChannelSquelch(const QString &id);

    void handleChannelItemChanged(QTableWidgetItem *item);
    void toggleChannelMonitor(const QString &id);
    void toggleFftVisible();
    void toggleShowSelectedOnly(bool enabled);

    marine::GrOsmoSdrSource sdrSource;

    QLabel *centerFrequencyLabel = nullptr;
    QLabel *sampleRateLabel = nullptr;
    QLabel *sampleCountLabel = nullptr;
    QLabel *widebandPowerLabel = nullptr;
    QLabel *sdrStatusLabel = nullptr;
    QPushButton *connectButton = nullptr;
    QPushButton *startButton = nullptr;
    QPushButton *monitorButton = nullptr;
    QPushButton *recordButton = nullptr;
    QPushButton *fftButton = nullptr;
    QPushButton *showSelectedOnlyButton = nullptr;
    WaterfallWidget *waterfallWidget = nullptr;
    QTableWidget *channelTable = nullptr;

    QHash<QString, marine::SdrSquelchMode> channelSquelchModes;
    QHash<QString, double> channelSquelchThresholds;
    QSet<QString> selectedChannelIds;
    QSet<QString> mutedMonitorChannelIds;
    QVector<marine::ChannelConfig> channelCatalog;
    bool showSelectedOnly = true;
    bool fftVisible = true;
    bool liveAudioDesired = true;
};
