#pragma once

#include "GrOsmoSdrSource.h"
#include "ChannelCatalog.h"

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
    void handleSdrStateChanged(zapiska::SdrSourceState state);
    void handleSdrStatsUpdated(const zapiska::SdrStreamStats &stats);
    void handleSpectrumUpdated(const zapiska::SdrSpectrumFrame &frame);
    void handleSdrError(const QString &message);
    void refreshSdrControls();
    void updateSdrTuningControls(const zapiska::SdrSourceConfig &config);
    zapiska::SdrSourceConfig buildSdrConfig() const;
    void updateChannelMeters(const zapiska::SdrStreamStats &stats);
    void resetChannelDisplay(int row);
    int channelRow(const QString &id) const;
    bool channelHasRecordableAudio(const zapiska::SdrStreamStats &stats, const QString &id) const;
    QString nextRecordingPath() const;
    zapiska::SdrSquelchMode squelchModeForChannel(const QString &id) const;
    double squelchThresholdForChannel(const QString &id) const;
    void applyChannelSquelch(const QString &id);

    void handleChannelItemChanged(QTableWidgetItem *item);
    void toggleChannelMonitor(const QString &id);
    void toggleFftVisible();
    void toggleShowSelectedOnly(bool enabled);

    zapiska::GrOsmoSdrSource sdrSource;

    QDoubleSpinBox *centerFrequencySpin = nullptr;
    QComboBox *sampleRateCombo = nullptr;
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

    QHash<QString, zapiska::SdrSquelchMode> channelSquelchModes;
    QHash<QString, double> channelSquelchThresholds;
    QSet<QString> selectedChannelIds;
    QSet<QString> mutedMonitorChannelIds;
    QVector<zapiska::ChannelConfig> channelCatalog;
    bool showSelectedOnly = true;
    bool fftVisible = true;
    bool liveAudioDesired = true;
};
