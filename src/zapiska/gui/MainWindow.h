#pragma once

#include "GrOsmoSdrSource.h"
#include "ChannelCatalog.h"

#include <QHash>
#include <QMainWindow>
#include <QSet>
#include <Qt>
#include <QVector>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollBar;
class QSlider;
class QTableWidget;
class QTableWidgetItem;
class WaterfallWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    void buildUi();
    void loadChannels();
    void refreshChannelTable();
    void refreshChannelVisibility();
    void handleChannelHeaderClicked(int section);
    void sortChannelCatalog();
    void refreshWaterfallChannels(const zapiska::SdrStreamStats *stats = nullptr);
    void refreshFftControls();
    void refreshFftViewControls();
    void initializeSelectedChannels();
    bool loadSelectedChannelsFromSettings();
    void saveSelectedChannelsToSettings() const;
    void loadChannelMonitorSettings();
    void saveChannelMonitorSettings() const;
    void loadChannelSquelchSettings();
    void saveChannelSquelchSettings() const;
    void updateChannelSelectionControls();
    bool isChannelSelected(const QString &id) const;
    bool channelMonitorEnabledForChannel(const QString &id) const;
    void updateChannelMonitorButton(int row);
    int selectedChannelCount() const;

    void toggleSdrConnection();
    void toggleSdrStreaming();
    void startSdr();
    void stopSdr();
    void handleLiveAudioVolumeChanged(int volumePercent);
    bool applyLiveAudioVolume();
    void refreshLiveAudioVolumeControls();
    void toggleRecording();
    void toggleRawIqRecording();
    void openRecordsDirectory();
    void handleSdrStateChanged(zapiska::SdrSourceState state);
    void handleSdrStatsUpdated(const zapiska::SdrStreamStats &stats);
    void handleSpectrumUpdated(const zapiska::SdrSpectrumFrame &frame);
    void handleSdrError(const QString &message);
    void refreshSdrControls();
    void updateSdrTuningControls(const zapiska::SdrSourceConfig &config);
    zapiska::SdrSourceConfig buildSdrConfig() const;
    void updateChannelMeters(const zapiska::SdrStreamStats &stats);
    void resetChannelDisplay(int row);
    void updateChannelStateHighlight(int row, bool unsquelched);
    int channelRow(const QString &id) const;
    bool channelHasRecordableAudio(const zapiska::SdrStreamStats &stats, const QString &id) const;
    QString nextRecordingPath() const;
    QString nextRawIqRecordingPath() const;
    double squelchThresholdForChannel(const QString &id) const;
    QDoubleSpinBox *squelchThresholdSpinForRow(int row) const;
    void applyChannelSquelch(const QString &id);
    void setChannelSquelchThreshold(const QString &id, double thresholdDbfs);
    void autoSetChannelSquelch(const QString &id);
    void resetChannelSquelch(const QString &id);
    bool currentChannelPowerDbfs(const QString &id, double *powerDbfs) const;
    void handleSourceModeChanged();
    void selectRawIqReplayFile();
    void refreshSourceModeControls();
    bool rawIqReplayModeEnabled() const;
    QString rawIqDeviceArgs(QString *errorMessage = nullptr) const;
    bool loadRawIqMetadata(const QString &path,
        qint64 *centerFrequencyHz,
        int *sampleRateHz,
        QString *errorMessage = nullptr) const;

    void handleChannelItemChanged(QTableWidgetItem *item);
    void toggleChannelMonitor(const QString &id);
    void toggleFftVisible();
    void toggleShowSelectedOnly(bool enabled);

    zapiska::GrOsmoSdrSource sdrSource;

    QComboBox *sourceModeCombo = nullptr;
    QPushButton *rawIqFileButton = nullptr;
    QLineEdit *rawIqFileEdit = nullptr;
    QDoubleSpinBox *centerFrequencySpin = nullptr;
    QComboBox *sampleRateCombo = nullptr;
    QLabel *sampleCountLabel = nullptr;
    QLabel *widebandPowerLabel = nullptr;
    QLabel *sdrStatusLabel = nullptr;
    QPushButton *connectButton = nullptr;
    QPushButton *startButton = nullptr;
    QLabel *volumeLabel = nullptr;
    QSlider *volumeSlider = nullptr;
    QPushButton *recordButton = nullptr;
    QPushButton *rawIqRecordButton = nullptr;
    QPushButton *fftButton = nullptr;
    QPushButton *showSelectedOnlyButton = nullptr;
    QLabel *fftZoomTitleLabel = nullptr;
    QLabel *fftZoomLabel = nullptr;
    QSlider *fftZoomSlider = nullptr;
    QScrollBar *fftScrollBar = nullptr;
    WaterfallWidget *waterfallWidget = nullptr;
    QTableWidget *channelTable = nullptr;

    QHash<QString, double> channelSquelchThresholds;
    QSet<QString> selectedChannelIds;
    QSet<QString> mutedMonitorChannelIds;
    QVector<zapiska::ChannelConfig> channelCatalog;
    QString rawIqReplayPath;
    int channelSortColumn = -1;
    Qt::SortOrder channelSortOrder = Qt::AscendingOrder;
    bool showSelectedOnly = true;
    bool fftVisible = true;
    int liveAudioVolumePercent = 100;
};
