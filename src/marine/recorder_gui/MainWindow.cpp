#include "MainWindow.h"

#include "MarineCore.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QDir>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace {

constexpr double MinimumMeterPowerDbfs = -100.0;
constexpr double MaximumMeterPowerDbfs = -20.0;
constexpr double MinimumAudioLevelDbfs = -80.0;
constexpr double MaximumAudioLevelDbfs = 0.0;
constexpr double DefaultSquelchThresholdDbfs = -45.0;

constexpr int SelectedColumn = 0;
constexpr int ChannelNameColumn = 1;
constexpr int FrequencyColumn = 2;
constexpr int ModeColumn = 3;
constexpr int BandwidthColumn = 4;
constexpr int SignalColumn = 5;
constexpr int AudioColumn = 6;
constexpr int SquelchColumn = 7;
constexpr int ThresholdColumn = 8;
constexpr int StateColumn = 9;
constexpr int RecordingColumn = 10;

QString sdrStateText(marine::SdrSourceState state)
{
    switch (state) {
    case marine::SdrSourceState::Closed:
        return QStringLiteral("closed");
    case marine::SdrSourceState::Open:
        return QStringLiteral("open");
    case marine::SdrSourceState::Streaming:
        return QStringLiteral("streaming");
    case marine::SdrSourceState::Error:
        return QStringLiteral("error");
    }

    return QStringLiteral("unknown");
}

QString formatSampleRate(int sampleRateHz)
{
    if (sampleRateHz >= 1000000) {
        return QLocale::c().toString(static_cast<double>(sampleRateHz) / 1000000.0, 'f', 3)
            + QStringLiteral(" MS/s");
    }
    if (sampleRateHz >= 1000) {
        return QLocale::c().toString(static_cast<double>(sampleRateHz) / 1000.0, 'f', 1)
            + QStringLiteral(" kS/s");
    }

    return QLocale::c().toString(sampleRateHz) + QStringLiteral(" S/s");
}

QString formatSampleCount(quint64 samplesRead)
{
    return QLocale::c().toString(samplesRead);
}

QString formatWidebandPower(const marine::SdrStreamStats &stats)
{
    if (!stats.hasWidebandPower) {
        return QStringLiteral("waiting");
    }

    return QLocale::c().toString(stats.widebandPowerDbfs, 'f', 1) + QStringLiteral(" dBFS");
}

QString formatChannelPower(const marine::SdrChannelStats &stats)
{
    if (!stats.hasPower) {
        return QStringLiteral("waiting");
    }

    return QLocale::c().toString(stats.powerDbfs, 'f', 1) + QStringLiteral(" dBFS");
}

QString formatAudioLevel(const marine::SdrChannelStats &stats)
{
    if (!stats.hasAudioLevel) {
        return QStringLiteral("waiting");
    }

    return QLocale::c().toString(stats.audioLevelDbfs, 'f', 1) + QStringLiteral(" dBFS");
}

QString formatSquelchState(const marine::SdrChannelStats &stats)
{
    if (stats.squelchMode == marine::SdrSquelchMode::ForcedOpen) {
        return QStringLiteral("open");
    }
    if (stats.squelchMode == marine::SdrSquelchMode::ForcedClosed) {
        return QStringLiteral("muted");
    }
    if (!stats.hasSquelch) {
        return QStringLiteral("waiting");
    }

    return stats.squelchOpen ? QStringLiteral("open") : QStringLiteral("squelched");
}

marine::SdrSquelchMode squelchModeFromCombo(const QComboBox *combo)
{
    return static_cast<marine::SdrSquelchMode>(combo->currentData().toInt());
}

int meterValue(double value, double minimum, double maximum)
{
    const double clampedValue = std::clamp(value, minimum, maximum);
    const double normalized = (clampedValue - minimum) / (maximum - minimum);
    return static_cast<int>(std::lround(normalized * 100.0));
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , sdrSource(this)
{
    buildUi();
    connect(&sdrSource, &marine::SdrSource::stateChanged, this, &MainWindow::handleSdrStateChanged);
    connect(&sdrSource, &marine::SdrSource::statsUpdated, this, &MainWindow::handleSdrStatsUpdated);
    connect(&sdrSource, &marine::SdrSource::errorOccurred, this, &MainWindow::handleSdrError);
    handleSdrStateChanged(sdrSource.state());
    handleSdrStatsUpdated(sdrSource.stats());
    loadChannels();
}

void MainWindow::buildUi()
{
    auto *root = new QWidget(this);
    auto *layout = new QVBoxLayout(root);

    auto *toolbar = new QWidget(root);
    auto *toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(0, 0, 0, 0);

    auto *sdrMetrics = new QWidget(root);
    auto *sdrMetricsLayout = new QHBoxLayout(sdrMetrics);
    sdrMetricsLayout->setContentsMargins(0, 0, 0, 0);

    auto *channelControls = new QWidget(root);
    auto *channelControlsLayout = new QHBoxLayout(channelControls);
    channelControlsLayout->setContentsMargins(0, 0, 0, 0);

    deviceStateLabel = new QLabel(tr("Backend: closed"), toolbar);
    channelCatalogLabel = new QLabel(tr("Channels: loading"), toolbar);
    centerFrequencyLabel = new QLabel(tr("Center: 156.800 MHz"), sdrMetrics);
    sampleRateLabel = new QLabel(tr("Sample rate: 2.000 MS/s"), sdrMetrics);
    sampleCountLabel = new QLabel(tr("Samples: 0"), sdrMetrics);
    widebandPowerLabel = new QLabel(tr("Power: waiting"), sdrMetrics);
    sdrStatusLabel = new QLabel(tr("SDR: ready"), root);
    sdrStatusLabel->setWordWrap(true);

    connectButton = new QPushButton(tr("Connect"), toolbar);
    startButton = new QPushButton(tr("Start"), toolbar);
    startButton->setEnabled(false);
    stopButton = new QPushButton(tr("Stop"), toolbar);
    stopButton->setEnabled(false);
    monitorButton = new QPushButton(tr("Monitor"), toolbar);
    monitorButton->setEnabled(false);
    recordButton = new QPushButton(tr("Record"), toolbar);
    recordButton->setEnabled(false);

    toolbarLayout->addWidget(deviceStateLabel);
    toolbarLayout->addSpacing(16);
    toolbarLayout->addWidget(channelCatalogLabel);
    toolbarLayout->addStretch();
    toolbarLayout->addWidget(connectButton);
    toolbarLayout->addWidget(startButton);
    toolbarLayout->addWidget(stopButton);
    toolbarLayout->addWidget(monitorButton);
    toolbarLayout->addWidget(recordButton);

    sdrMetricsLayout->addWidget(centerFrequencyLabel);
    sdrMetricsLayout->addSpacing(16);
    sdrMetricsLayout->addWidget(sampleRateLabel);
    sdrMetricsLayout->addSpacing(16);
    sdrMetricsLayout->addWidget(sampleCountLabel);
    sdrMetricsLayout->addSpacing(16);
    sdrMetricsLayout->addWidget(widebandPowerLabel);
    sdrMetricsLayout->addStretch();

    showSelectedOnlyButton = new QPushButton(tr("Show Selected Only"), channelControls);
    showSelectedOnlyButton->setCheckable(true);

    channelControlsLayout->addWidget(new QLabel(tr("Channel view:"), channelControls));
    channelControlsLayout->addWidget(showSelectedOnlyButton);
    channelControlsLayout->addStretch();

    channelTable = new QTableWidget(root);
    channelTable->setColumnCount(11);
    channelTable->setHorizontalHeaderLabels({
        tr("Selected"),
        tr("Channel"),
        tr("Frequency"),
        tr("Mode"),
        tr("Bandwidth"),
        tr("Signal"),
        tr("Audio"),
        tr("Squelch"),
        tr("Threshold"),
        tr("State"),
        tr("Recording"),
    });
    channelTable->horizontalHeader()->setStretchLastSection(true);
    channelTable->verticalHeader()->setVisible(false);
    channelTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    channelTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    channelTable->setSelectionMode(QAbstractItemView::SingleSelection);

    connect(connectButton, &QPushButton::clicked, this, &MainWindow::toggleSdrConnection);
    connect(startButton, &QPushButton::clicked, this, &MainWindow::startSdr);
    connect(stopButton, &QPushButton::clicked, this, &MainWindow::stopSdr);
    connect(monitorButton, &QPushButton::clicked, this, &MainWindow::toggleLiveAudio);
    connect(recordButton, &QPushButton::clicked, this, &MainWindow::toggleRecording);
    connect(showSelectedOnlyButton, &QPushButton::toggled, this, &MainWindow::toggleShowSelectedOnly);
    connect(channelTable, &QTableWidget::itemChanged, this, &MainWindow::handleChannelItemChanged);

    layout->addWidget(toolbar);
    layout->addWidget(sdrMetrics);
    layout->addWidget(sdrStatusLabel);
    layout->addWidget(channelControls);
    layout->addWidget(channelTable);

    setCentralWidget(root);
    setWindowTitle(tr("Zapiska Marine Recorder"));
    resize(900, 420);
}

void MainWindow::loadChannels()
{
    const QString path = marine::defaultChannelConfigPath();
    QString errorMessage;
    channelCatalog = marine::loadChannelsFromFile(path, &errorMessage);
    selectedChannelIds.clear();

    if (channelCatalog.isEmpty()) {
        channelCatalog = marine::defaultChannels();
        channelCatalogLabel->setText(tr("Channels: fallback (%1)").arg(errorMessage));
    }

    initializeSelectedChannels();
    refreshChannelTable();
    updateChannelCatalogLabel();
}

void MainWindow::initializeSelectedChannels()
{
    if (loadSelectedChannelsFromSettings()) {
        return;
    }

    for (const auto &channel : channelCatalog) {
        if (channel.enabledByDefault || channel.id == QStringLiteral("16")) {
            selectedChannelIds.insert(channel.id);
        }
    }

    if (selectedChannelIds.isEmpty() && !channelCatalog.isEmpty()) {
        selectedChannelIds.insert(channelCatalog.first().id);
    }

    saveSelectedChannelsToSettings();
}

bool MainWindow::loadSelectedChannelsFromSettings()
{
    QSettings settings;
    if (!settings.contains(QStringLiteral("recorder/selectedChannelIds"))) {
        return false;
    }

    QSet<QString> catalogIds;
    for (const auto &channel : channelCatalog) {
        catalogIds.insert(channel.id);
    }

    const QStringList storedIds = settings.value(QStringLiteral("recorder/selectedChannelIds")).toStringList();
    for (const auto &id : storedIds) {
        if (catalogIds.contains(id)) {
            selectedChannelIds.insert(id);
        }
    }

    return !selectedChannelIds.isEmpty();
}

void MainWindow::saveSelectedChannelsToSettings() const
{
    QStringList selectedIds;
    for (const auto &channel : channelCatalog) {
        if (isChannelSelected(channel.id)) {
            selectedIds.append(channel.id);
        }
    }

    QSettings settings;
    settings.setValue(QStringLiteral("recorder/selectedChannelIds"), selectedIds);
}

void MainWindow::refreshChannelTable()
{
    const QSignalBlocker blocker(channelTable);
    channelTable->clearContents();
    channelTable->setRowCount(channelCatalog.size());

    for (int row = 0; row < channelCatalog.size(); ++row) {
        const auto &channel = channelCatalog.at(row);

        auto *selectedItem = new QTableWidgetItem();
        selectedItem->setData(Qt::UserRole, channel.id);
        selectedItem->setCheckState(isChannelSelected(channel.id) ? Qt::Checked : Qt::Unchecked);
        selectedItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        channelTable->setItem(row, SelectedColumn, selectedItem);

        channelTable->setItem(row, ChannelNameColumn, new QTableWidgetItem(channel.name));
        channelTable->setItem(row, FrequencyColumn, new QTableWidgetItem(marine::formatFrequencyMHz(channel.frequencyHz)));
        channelTable->setItem(row, ModeColumn, new QTableWidgetItem(channel.mode.toUpper()));
        channelTable->setItem(row, BandwidthColumn, new QTableWidgetItem(QString::number(channel.bandwidthHz) + tr(" Hz")));

        auto *signalMeter = new QProgressBar(channelTable);
        signalMeter->setRange(0, 100);
        signalMeter->setValue(0);
        signalMeter->setFormat(tr("waiting"));
        signalMeter->setTextVisible(true);
        channelTable->setCellWidget(row, SignalColumn, signalMeter);

        auto *audioMeter = new QProgressBar(channelTable);
        audioMeter->setRange(0, 100);
        audioMeter->setValue(0);
        audioMeter->setFormat(tr("waiting"));
        audioMeter->setTextVisible(true);
        channelTable->setCellWidget(row, AudioColumn, audioMeter);

        auto *squelchMode = new QComboBox(channelTable);
        squelchMode->addItem(tr("Auto"), static_cast<int>(marine::SdrSquelchMode::Automatic));
        squelchMode->addItem(tr("Open"), static_cast<int>(marine::SdrSquelchMode::ForcedOpen));
        squelchMode->addItem(tr("Muted"), static_cast<int>(marine::SdrSquelchMode::ForcedClosed));
        squelchMode->setCurrentIndex(static_cast<int>(squelchModeForChannel(channel.id)));
        connect(
            squelchMode,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this,
            [this, id = channel.id]() {
                applyChannelSquelch(id);
            });
        channelTable->setCellWidget(row, SquelchColumn, squelchMode);

        auto *threshold = new QDoubleSpinBox(channelTable);
        threshold->setRange(-120.0, 0.0);
        threshold->setDecimals(1);
        threshold->setSingleStep(1.0);
        threshold->setSuffix(tr(" dBFS"));
        threshold->setValue(squelchThresholdForChannel(channel.id));
        connect(
            threshold,
            static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
            this,
            [this, id = channel.id]() {
                applyChannelSquelch(id);
            });
        channelTable->setCellWidget(row, ThresholdColumn, threshold);

        channelTable->setItem(row, StateColumn, new QTableWidgetItem());
        channelTable->setItem(row, RecordingColumn, new QTableWidgetItem());
        resetChannelDisplay(row);
    }

    channelTable->resizeColumnsToContents();
    refreshChannelVisibility();
    updateChannelSelectionControls();
}

void MainWindow::updateChannelCatalogLabel()
{
    channelCatalogLabel->setText(tr("Channels: %1 loaded, %2 selected, %3 visible")
        .arg(channelCatalog.size())
        .arg(selectedChannelCount())
        .arg(visibleChannelCount()));
}

void MainWindow::refreshChannelVisibility()
{
    for (int row = 0; row < channelCatalog.size(); ++row) {
        const auto &channel = channelCatalog.at(row);
        channelTable->setRowHidden(row, showSelectedOnly && !isChannelSelected(channel.id));
    }
}

void MainWindow::updateChannelSelectionControls()
{
    const QSignalBlocker blocker(channelTable);
    const auto state = sdrSource.state();
    const bool canChangeSelection = state == marine::SdrSourceState::Closed
        || state == marine::SdrSourceState::Error;

    for (int row = 0; row < channelTable->rowCount(); ++row) {
        auto *selectedItem = channelTable->item(row, SelectedColumn);
        if (!selectedItem) {
            continue;
        }

        Qt::ItemFlags flags = Qt::ItemIsSelectable | Qt::ItemIsUserCheckable;
        if (canChangeSelection) {
            flags |= Qt::ItemIsEnabled;
        }
        selectedItem->setFlags(flags);
    }
}

bool MainWindow::isChannelSelected(const QString &id) const
{
    return selectedChannelIds.contains(id);
}

int MainWindow::selectedChannelCount() const
{
    return selectedChannelIds.size();
}

int MainWindow::visibleChannelCount() const
{
    int visibleRows = 0;
    for (int row = 0; row < channelCatalog.size(); ++row) {
        if (!showSelectedOnly || isChannelSelected(channelCatalog.at(row).id)) {
            ++visibleRows;
        }
    }

    return visibleRows;
}

void MainWindow::toggleSdrConnection()
{
    const auto state = sdrSource.state();
    if (state == marine::SdrSourceState::Open || state == marine::SdrSourceState::Streaming) {
        sdrSource.close();
        sdrStatusLabel->setText(tr("SDR: disconnected"));
        statusBar()->showMessage(tr("SDR disconnected"), 3000);
        refreshSdrControls();
        return;
    }

    sdrStatusLabel->setText(tr("SDR: connecting"));
    QString errorMessage;
    const marine::SdrSourceConfig config = buildSdrConfig();
    if (!sdrSource.open(config, &errorMessage)) {
        handleSdrError(errorMessage);
        refreshSdrControls();
        return;
    }

    updateSdrConfigLabels(sdrSource.config());
    sdrStatusLabel->setText(tr("SDR: connected"));
    statusBar()->showMessage(tr("Connected to %1").arg(sdrSource.backendName()), 3000);
    refreshSdrControls();
}

void MainWindow::startSdr()
{
    sdrStatusLabel->setText(tr("SDR: starting"));
    QString errorMessage;
    if (!sdrSource.start(&errorMessage)) {
        handleSdrError(errorMessage);
        refreshSdrControls();
        return;
    }

    sdrStatusLabel->setText(tr("SDR: streaming"));
    statusBar()->showMessage(tr("SDR streaming"), 3000);
    refreshSdrControls();
}

void MainWindow::stopSdr()
{
    sdrSource.stop();
    sdrStatusLabel->setText(tr("SDR: stopped"));
    statusBar()->showMessage(tr("SDR stopped"), 3000);
    refreshSdrControls();
}

void MainWindow::toggleLiveAudio()
{
    const bool enableLiveAudio = !sdrSource.liveAudioEnabled();
    QString errorMessage;
    if (!sdrSource.setLiveAudioEnabled(enableLiveAudio, &errorMessage)) {
        handleSdrError(errorMessage);
        refreshSdrControls();
        return;
    }

    sdrStatusLabel->setText(enableLiveAudio ? tr("SDR: live monitor enabled") : tr("SDR: live monitor muted"));
    statusBar()->showMessage(enableLiveAudio ? tr("Live monitor enabled") : tr("Live monitor muted"), 3000);
    refreshSdrControls();
}

void MainWindow::toggleRecording()
{
    if (sdrSource.recording()) {
        const QString recordingPath = sdrSource.stats().recordingPath;
        sdrSource.stopRecording();
        sdrStatusLabel->setText(tr("SDR: recording stopped"));
        statusBar()->showMessage(tr("Recording saved to %1").arg(recordingPath), 6000);
        refreshSdrControls();
        return;
    }

    const QString recordingPath = nextRecordingPath();
    QString errorMessage;
    if (!sdrSource.startRecording(QStringLiteral("16"), recordingPath, &errorMessage)) {
        handleSdrError(errorMessage);
        refreshSdrControls();
        return;
    }

    sdrStatusLabel->setText(tr("SDR: recording Channel 16"));
    statusBar()->showMessage(tr("Recording to %1").arg(recordingPath), 6000);
    refreshSdrControls();
}

void MainWindow::handleSdrStateChanged(marine::SdrSourceState state)
{
    deviceStateLabel->setText(tr("Backend: %1 (%2)")
        .arg(sdrSource.backendName(), sdrStateText(state)));
    updateSdrConfigLabels(sdrSource.config());
    refreshSdrControls();
}

void MainWindow::handleSdrStatsUpdated(const marine::SdrStreamStats &stats)
{
    sampleCountLabel->setText(tr("Samples: %1").arg(formatSampleCount(stats.samplesRead)));
    widebandPowerLabel->setText(tr("Power: %1").arg(formatWidebandPower(stats)));
    updateChannelMeters(stats);
    refreshSdrControls();

    if (!stats.lastError.isEmpty()) {
        handleSdrError(stats.lastError);
    }
}

void MainWindow::handleSdrError(const QString &message)
{
    if (message.isEmpty()) {
        return;
    }

    sdrStatusLabel->setText(tr("SDR error: %1").arg(message));
    statusBar()->showMessage(message, 6000);
}

void MainWindow::refreshSdrControls()
{
    const auto state = sdrSource.state();
    const bool isOpen = state == marine::SdrSourceState::Open
        || state == marine::SdrSourceState::Streaming;
    const bool isStreaming = state == marine::SdrSourceState::Streaming;
    const bool liveAudioEnabled = sdrSource.liveAudioEnabled();
    const marine::SdrStreamStats stats = sdrSource.stats();
    const bool recording = stats.recording;

    connectButton->setText(isOpen ? tr("Disconnect") : tr("Connect"));
    connectButton->setEnabled(true);
    startButton->setEnabled(state == marine::SdrSourceState::Open);
    stopButton->setEnabled(isStreaming);
    monitorButton->setText(liveAudioEnabled ? tr("Mute") : tr("Monitor"));
    monitorButton->setEnabled(isOpen);
    recordButton->setText(recording ? tr("Stop Rec") : tr("Record"));
    recordButton->setEnabled(recording
        || (isStreaming && channelHasRecordableAudio(stats, QStringLiteral("16"))));
    updateChannelSelectionControls();
}

void MainWindow::updateSdrConfigLabels(const marine::SdrSourceConfig &config)
{
    centerFrequencyLabel->setText(tr("Center: %1")
        .arg(marine::formatFrequencyMHz(config.centerFrequencyHz)));
    sampleRateLabel->setText(tr("Sample rate: %1")
        .arg(formatSampleRate(config.sampleRateHz)));
}

marine::SdrSourceConfig MainWindow::buildSdrConfig() const
{
    marine::SdrSourceConfig config;
    config.channels.reserve(selectedChannelCount());

    for (const auto &channel : channelCatalog) {
        if (!isChannelSelected(channel.id)) {
            continue;
        }

        marine::SdrChannelConfig sdrChannel;
        sdrChannel.id = channel.id;
        sdrChannel.name = channel.name;
        sdrChannel.frequencyHz = channel.frequencyHz;
        sdrChannel.bandwidthHz = channel.bandwidthHz;
        sdrChannel.squelchMode = squelchModeForChannel(channel.id);
        sdrChannel.squelchThresholdDbfs = squelchThresholdForChannel(channel.id);
        sdrChannel.enabled = true;
        config.channels.append(sdrChannel);
    }

    return config;
}

void MainWindow::updateChannelMeters(const marine::SdrStreamStats &streamStats)
{
    for (const auto &stats : streamStats.channelStats) {
        if (!isChannelSelected(stats.id)) {
            continue;
        }

        const int row = channelRow(stats.id);
        if (row < 0) {
            continue;
        }

        auto *signalMeter = qobject_cast<QProgressBar *>(channelTable->cellWidget(row, SignalColumn));
        if (!signalMeter) {
            continue;
        }

        signalMeter->setValue(stats.hasPower
                ? meterValue(stats.powerDbfs, MinimumMeterPowerDbfs, MaximumMeterPowerDbfs)
                : 0);
        signalMeter->setFormat(formatChannelPower(stats));

        auto *audioMeter = qobject_cast<QProgressBar *>(channelTable->cellWidget(row, AudioColumn));
        if (!audioMeter) {
            continue;
        }

        audioMeter->setValue(stats.hasAudioLevel
                ? meterValue(stats.audioLevelDbfs, MinimumAudioLevelDbfs, MaximumAudioLevelDbfs)
                : 0);
        audioMeter->setFormat(formatAudioLevel(stats));

        auto *squelchItem = channelTable->item(row, StateColumn);
        if (squelchItem) {
            squelchItem->setText(formatSquelchState(stats));
        }

        auto *recordingItem = channelTable->item(row, RecordingColumn);
        if (recordingItem) {
            const bool channelRecording = streamStats.recording
                && streamStats.recordingChannelId == stats.id;
            const bool channelArmed = channelCatalog.at(row).recordByDefault;
            recordingItem->setText(channelRecording
                    ? tr("recording")
                    : (channelArmed ? tr("armed") : tr("off")));
        }
    }
}

void MainWindow::resetChannelDisplay(int row)
{
    if (row < 0 || row >= channelCatalog.size()) {
        return;
    }

    const auto &channel = channelCatalog.at(row);
    const bool selected = isChannelSelected(channel.id);
    const QString idleText = selected ? tr("waiting") : tr("inactive");

    auto *signalMeter = qobject_cast<QProgressBar *>(channelTable->cellWidget(row, SignalColumn));
    if (signalMeter) {
        signalMeter->setValue(0);
        signalMeter->setFormat(idleText);
    }

    auto *audioMeter = qobject_cast<QProgressBar *>(channelTable->cellWidget(row, AudioColumn));
    if (audioMeter) {
        audioMeter->setValue(0);
        audioMeter->setFormat(idleText);
    }

    auto *squelchItem = channelTable->item(row, StateColumn);
    if (squelchItem) {
        squelchItem->setText(idleText);
    }

    auto *recordingItem = channelTable->item(row, RecordingColumn);
    if (recordingItem) {
        recordingItem->setText(selected && channel.recordByDefault ? tr("armed") : tr("off"));
    }
}

int MainWindow::channelRow(const QString &id) const
{
    for (int row = 0; row < channelCatalog.size(); ++row) {
        if (channelCatalog.at(row).id == id) {
            return row;
        }
    }

    return -1;
}

bool MainWindow::channelHasRecordableAudio(const marine::SdrStreamStats &stats, const QString &id) const
{
    for (const auto &channelStats : stats.channelStats) {
        if (channelStats.id == id && channelStats.audioSampleRateHz > 0) {
            return true;
        }
    }

    return false;
}

QString MainWindow::nextRecordingPath() const
{
    QString basePath = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    if (basePath.isEmpty()) {
        basePath = QDir::currentPath();
    }

    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    return QDir(basePath).filePath(QStringLiteral("Zapiska/marine_ch16_%1.wav").arg(timestamp));
}

marine::SdrSquelchMode MainWindow::squelchModeForChannel(const QString &id) const
{
    return channelSquelchModes.value(id, marine::SdrSquelchMode::Automatic);
}

double MainWindow::squelchThresholdForChannel(const QString &id) const
{
    return channelSquelchThresholds.value(id, DefaultSquelchThresholdDbfs);
}

void MainWindow::applyChannelSquelch(const QString &id)
{
    const int row = channelRow(id);
    if (row < 0) {
        return;
    }

    const auto *modeCombo = qobject_cast<QComboBox *>(channelTable->cellWidget(row, SquelchColumn));
    const auto *thresholdSpin = qobject_cast<QDoubleSpinBox *>(channelTable->cellWidget(row, ThresholdColumn));
    if (!modeCombo || !thresholdSpin) {
        return;
    }

    const auto mode = squelchModeFromCombo(modeCombo);
    const double threshold = thresholdSpin->value();
    channelSquelchModes.insert(id, mode);
    channelSquelchThresholds.insert(id, threshold);

    const auto state = sdrSource.state();
    if (state != marine::SdrSourceState::Open && state != marine::SdrSourceState::Streaming) {
        return;
    }

    QString errorMessage;
    if (!sdrSource.setChannelSquelch(id, mode, threshold, &errorMessage)) {
        handleSdrError(errorMessage);
    }
}

void MainWindow::handleChannelItemChanged(QTableWidgetItem *item)
{
    if (!item || item->column() != SelectedColumn) {
        return;
    }

    const QString id = item->data(Qt::UserRole).toString();
    if (id.isEmpty()) {
        return;
    }

    const bool checked = item->checkState() == Qt::Checked;
    const bool currentlySelected = selectedChannelIds.contains(id);
    if (checked == currentlySelected) {
        return;
    }

    const auto state = sdrSource.state();
    if (state != marine::SdrSourceState::Closed && state != marine::SdrSourceState::Error) {
        const QSignalBlocker blocker(channelTable);
        item->setCheckState(currentlySelected ? Qt::Checked : Qt::Unchecked);
        statusBar()->showMessage(tr("Channel selection is locked while the SDR is connected"), 3000);
        return;
    }

    if (!checked && selectedChannelIds.size() == 1 && currentlySelected) {
        const QSignalBlocker blocker(channelTable);
        item->setCheckState(Qt::Checked);
        statusBar()->showMessage(tr("At least one channel must stay selected"), 3000);
        return;
    }

    if (checked) {
        selectedChannelIds.insert(id);
    } else {
        selectedChannelIds.remove(id);
    }

    saveSelectedChannelsToSettings();
    resetChannelDisplay(item->row());
    refreshChannelVisibility();
    updateChannelCatalogLabel();
}

void MainWindow::toggleShowSelectedOnly(bool enabled)
{
    showSelectedOnly = enabled;
    refreshChannelVisibility();
    updateChannelCatalogLabel();
}
