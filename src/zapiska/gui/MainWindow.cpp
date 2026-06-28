#include "MainWindow.h"

#include "ChannelCatalog.h"
#include "WaterfallWidget.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QDir>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPainter>
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
constexpr double DefaultSquelchThresholdDbfs = -45.0;
constexpr double ResetSquelchThresholdDbfs = -150.0;
constexpr double AutoSquelchOffsetDb = 3.0;
constexpr double MaximumAutoSquelchThresholdDbfs = -10.0;

constexpr int SelectedColumn = 0;
constexpr int ChannelNameColumn = 1;
constexpr int FrequencyColumn = 2;
constexpr int SignalColumn = 3;
constexpr int MonitorColumn = 4;
constexpr int ThresholdColumn = 5;
constexpr int StateColumn = 6;
constexpr int RecordingColumn = 7;

QString formatSampleCount(quint64 samplesRead)
{
    return QLocale::c().toString(samplesRead);
}

QString formatWidebandPower(const zapiska::SdrStreamStats &stats)
{
    if (!stats.hasWidebandPower) {
        return QStringLiteral("waiting");
    }

    return QLocale::c().toString(stats.widebandPowerDbfs, 'f', 1) + QStringLiteral(" dBFS");
}

QString formatChannelPower(const zapiska::SdrChannelStats &stats)
{
    if (!stats.hasPower) {
        return QStringLiteral("waiting");
    }

    return QLocale::c().toString(stats.powerDbfs, 'f', 1) + QStringLiteral(" dBFS");
}

QString formatSquelchState(const zapiska::SdrChannelStats &stats)
{
    if (stats.squelchMode == zapiska::SdrSquelchMode::ForcedOpen) {
        return QStringLiteral("open");
    }
    if (stats.squelchMode == zapiska::SdrSquelchMode::ForcedClosed) {
        return QStringLiteral("muted");
    }
    if (!stats.hasSquelch) {
        return QStringLiteral("waiting");
    }

    return stats.squelchOpen ? QStringLiteral("open") : QStringLiteral("squelched");
}

int meterValue(double value, double minimum, double maximum)
{
    const double clampedValue = std::clamp(value, minimum, maximum);
    const double normalized = (clampedValue - minimum) / (maximum - minimum);
    return static_cast<int>(std::lround(normalized * 100.0));
}

bool channelUnsquelched(const zapiska::SdrStreamStats &stats, const QString &id)
{
    for (const auto &channelStats : stats.channelStats) {
        if (channelStats.id == id) {
            return channelStats.hasSquelch && channelStats.squelchOpen;
        }
    }

    return false;
}

class SquelchSignalMeter : public QProgressBar
{
public:
    explicit SquelchSignalMeter(QWidget *parent = nullptr)
        : QProgressBar(parent)
    {
    }

    void setSquelchThresholdDbfs(double thresholdDbfs)
    {
        if (std::abs(squelchThresholdDbfs - thresholdDbfs) < 0.05) {
            return;
        }

        squelchThresholdDbfs = thresholdDbfs;
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QProgressBar::paintEvent(event);

        const QRect markerRect = rect().adjusted(2, 2, -2, -2);
        if (!markerRect.isValid()) {
            return;
        }

        const double clampedThreshold = std::clamp(
            squelchThresholdDbfs,
            MinimumMeterPowerDbfs,
            MaximumMeterPowerDbfs);
        const double normalized = (clampedThreshold - MinimumMeterPowerDbfs)
            / (MaximumMeterPowerDbfs - MinimumMeterPowerDbfs);
        const int markerSpan = std::max(markerRect.width() - 1, 0);
        const int x = markerRect.left()
            + static_cast<int>(std::lround(normalized * static_cast<double>(markerSpan)));

        QPainter painter(this);
        QPen pen(QColor(210, 0, 0));
        pen.setWidth(2);
        painter.setPen(pen);
        painter.drawLine(x, markerRect.top(), x, markerRect.bottom());
    }

private:
    double squelchThresholdDbfs = DefaultSquelchThresholdDbfs;
};

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , sdrSource(this)
{
    buildUi();
    connect(&sdrSource, &zapiska::SdrSource::stateChanged, this, &MainWindow::handleSdrStateChanged);
    connect(&sdrSource, &zapiska::SdrSource::spectrumUpdated, this, &MainWindow::handleSpectrumUpdated);
    connect(&sdrSource, &zapiska::SdrSource::statsUpdated, this, &MainWindow::handleSdrStatsUpdated);
    connect(&sdrSource, &zapiska::SdrSource::errorOccurred, this, &MainWindow::handleSdrError);
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

    centerFrequencySpin = new QDoubleSpinBox(sdrMetrics);
    centerFrequencySpin->setRange(1.000, 6000.000);
    centerFrequencySpin->setDecimals(3);
    centerFrequencySpin->setSingleStep(0.025);
    centerFrequencySpin->setSuffix(tr(" MHz"));
    centerFrequencySpin->setKeyboardTracking(false);
    centerFrequencySpin->setValue(static_cast<double>(zapiska::DefaultSdrCenterFrequencyHz) / 1000000.0);

    sampleRateCombo = new QComboBox(sdrMetrics);
    sampleRateCombo->addItem(tr("2M"), 2000000);
    sampleRateCombo->addItem(tr("4M"), 4000000);
    sampleRateCombo->addItem(tr("8M"), 8000000);
    sampleRateCombo->addItem(tr("10M"), 10000000);
    sampleRateCombo->addItem(tr("12.5M"), 12500000);
    sampleRateCombo->addItem(tr("20M"), 20000000);
    const int defaultSampleRateIndex = sampleRateCombo->findData(zapiska::DefaultSdrSampleRateHz);
    if (defaultSampleRateIndex >= 0) {
        sampleRateCombo->setCurrentIndex(defaultSampleRateIndex);
    }

    sampleCountLabel = new QLabel(tr("Samples: 0"), sdrMetrics);
    widebandPowerLabel = new QLabel(tr("Power: waiting"), sdrMetrics);
    sdrStatusLabel = new QLabel(tr("SDR: ready"), root);
    sdrStatusLabel->setWordWrap(true);

    connectButton = new QPushButton(tr("Connect"), toolbar);
    startButton = new QPushButton(tr("Start"), toolbar);
    startButton->setEnabled(false);
    monitorButton = new QPushButton(tr("Playing"), toolbar);
    monitorButton->setEnabled(false);
    recordButton = new QPushButton(tr("Record"), toolbar);
    recordButton->setEnabled(false);
    waterfallWidget = new WaterfallWidget(root);

    toolbarLayout->addWidget(connectButton);
    toolbarLayout->addWidget(startButton);
    toolbarLayout->addWidget(monitorButton);
    toolbarLayout->addWidget(recordButton);
    toolbarLayout->addStretch();

    sdrMetricsLayout->addWidget(new QLabel(tr("Center:"), sdrMetrics));
    sdrMetricsLayout->addWidget(centerFrequencySpin);
    sdrMetricsLayout->addSpacing(16);
    sdrMetricsLayout->addWidget(new QLabel(tr("Sample rate:"), sdrMetrics));
    sdrMetricsLayout->addWidget(sampleRateCombo);
    sdrMetricsLayout->addSpacing(16);
    sdrMetricsLayout->addWidget(sampleCountLabel);
    sdrMetricsLayout->addSpacing(16);
    sdrMetricsLayout->addWidget(widebandPowerLabel);
    sdrMetricsLayout->addStretch();

    fftButton = new QPushButton(tr("FFT Off"), channelControls);
    showSelectedOnlyButton = new QPushButton(tr("Show All Channels"), channelControls);

    channelControlsLayout->addWidget(new QLabel(tr("Display:"), channelControls));
    channelControlsLayout->addWidget(fftButton);
    channelControlsLayout->addWidget(showSelectedOnlyButton);
    channelControlsLayout->addStretch();

    channelTable = new QTableWidget(root);
    channelTable->setColumnCount(8);
    channelTable->setHorizontalHeaderLabels({
        tr("Selected"),
        tr("Ch"),
        tr("Freq (MHz)"),
        tr("Signal"),
        tr("Playback"),
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
    connect(startButton, &QPushButton::clicked, this, &MainWindow::toggleSdrStreaming);
    connect(monitorButton, &QPushButton::clicked, this, &MainWindow::toggleLiveAudio);
    connect(recordButton, &QPushButton::clicked, this, &MainWindow::toggleRecording);
    connect(fftButton, &QPushButton::clicked, this, &MainWindow::toggleFftVisible);
    connect(showSelectedOnlyButton, &QPushButton::clicked, this, [this]() {
        toggleShowSelectedOnly(!showSelectedOnly);
    });
    connect(channelTable, &QTableWidget::itemChanged, this, &MainWindow::handleChannelItemChanged);

    layout->addWidget(toolbar);
    layout->addWidget(sdrMetrics);
    layout->addWidget(sdrStatusLabel);
    layout->addWidget(waterfallWidget);
    layout->addWidget(channelControls);
    layout->addWidget(channelTable);

    setCentralWidget(root);
    setWindowTitle(tr("Zapiska"));
    resize(900, 420);
    refreshFftControls();
}

void MainWindow::loadChannels()
{
    const QString path = zapiska::defaultChannelCatalogPath();
    QString errorMessage;
    channelCatalog = zapiska::loadChannelCatalogFromFile(path, &errorMessage);
    selectedChannelIds.clear();

    if (channelCatalog.isEmpty()) {
        channelCatalog = zapiska::defaultChannelCatalog();
        sdrStatusLabel->setText(tr("Channels: default catalog (%1)").arg(errorMessage));
    }

    initializeSelectedChannels();
    loadChannelMonitorSettings();
    loadChannelSquelchSettings();
    refreshChannelTable();
    refreshWaterfallChannels();
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

void MainWindow::loadChannelMonitorSettings()
{
    mutedMonitorChannelIds.clear();

    QSettings settings;
    const QStringList storedIds = settings.value(QStringLiteral("recorder/mutedMonitorChannelIds")).toStringList();
    if (storedIds.isEmpty()) {
        return;
    }

    QSet<QString> catalogIds;
    for (const auto &channel : channelCatalog) {
        catalogIds.insert(channel.id);
    }

    for (const auto &id : storedIds) {
        if (catalogIds.contains(id)) {
            mutedMonitorChannelIds.insert(id);
        }
    }
}

void MainWindow::saveChannelMonitorSettings() const
{
    QStringList mutedIds;
    for (const auto &channel : channelCatalog) {
        if (mutedMonitorChannelIds.contains(channel.id)) {
            mutedIds.append(channel.id);
        }
    }

    QSettings settings;
    settings.setValue(QStringLiteral("recorder/mutedMonitorChannelIds"), mutedIds);
}

void MainWindow::loadChannelSquelchSettings()
{
    channelSquelchThresholds.clear();

    QSet<QString> catalogIds;
    for (const auto &channel : channelCatalog) {
        catalogIds.insert(channel.id);
    }

    QSettings settings;
    const int size = settings.beginReadArray(QStringLiteral("recorder/channelSquelch"));
    for (int index = 0; index < size; ++index) {
        settings.setArrayIndex(index);
        const QString id = settings.value(QStringLiteral("id")).toString();
        if (!catalogIds.contains(id)) {
            continue;
        }

        bool ok = false;
        const double threshold = settings.value(
                QStringLiteral("threshold"),
                DefaultSquelchThresholdDbfs)
            .toDouble(&ok);
        if (ok && threshold >= ResetSquelchThresholdDbfs && threshold <= 0.0) {
            channelSquelchThresholds.insert(id, threshold);
        }
    }
    settings.endArray();
}

void MainWindow::saveChannelSquelchSettings() const
{
    QSettings settings;
    settings.beginWriteArray(QStringLiteral("recorder/channelSquelch"));

    int index = 0;
    for (const auto &channel : channelCatalog) {
        if (!channelSquelchThresholds.contains(channel.id)) {
            continue;
        }

        settings.setArrayIndex(index++);
        settings.setValue(QStringLiteral("id"), channel.id);
        settings.setValue(QStringLiteral("threshold"), squelchThresholdForChannel(channel.id));
    }

    settings.endArray();
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
        channelTable->setItem(row, FrequencyColumn, new QTableWidgetItem(zapiska::formatFrequencyMHz(channel.frequencyHz)));

        auto *signalMeter = new SquelchSignalMeter(channelTable);
        signalMeter->setRange(0, 100);
        signalMeter->setValue(0);
        signalMeter->setFormat(tr("waiting"));
        signalMeter->setTextVisible(true);
        signalMeter->setSquelchThresholdDbfs(squelchThresholdForChannel(channel.id));
        channelTable->setCellWidget(row, SignalColumn, signalMeter);

        auto *rowMonitorButton = new QPushButton(channelTable);
        connect(rowMonitorButton, &QPushButton::clicked, this, [this, id = channel.id]() {
            toggleChannelMonitor(id);
        });
        channelTable->setCellWidget(row, MonitorColumn, rowMonitorButton);

        auto *thresholdContainer = new QWidget(channelTable);
        auto *thresholdLayout = new QHBoxLayout(thresholdContainer);
        thresholdLayout->setContentsMargins(0, 0, 0, 0);
        thresholdLayout->setSpacing(4);

        auto *threshold = new QDoubleSpinBox(thresholdContainer);
        threshold->setRange(ResetSquelchThresholdDbfs, 0.0);
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

        auto *autoSquelchButton = new QPushButton(tr("A"), thresholdContainer);
        autoSquelchButton->setToolTip(tr("Set squelch from current signal power"));
        autoSquelchButton->setFixedWidth(28);
        connect(autoSquelchButton, &QPushButton::clicked, this, [this, id = channel.id]() {
            autoSetChannelSquelch(id);
        });

        auto *resetSquelchButton = new QPushButton(tr("R"), thresholdContainer);
        resetSquelchButton->setToolTip(tr("Reset squelch to -150 dBFS"));
        resetSquelchButton->setFixedWidth(28);
        connect(resetSquelchButton, &QPushButton::clicked, this, [this, id = channel.id]() {
            resetChannelSquelch(id);
        });

        thresholdLayout->addWidget(threshold);
        thresholdLayout->addWidget(autoSquelchButton);
        thresholdLayout->addWidget(resetSquelchButton);
        channelTable->setCellWidget(row, ThresholdColumn, thresholdContainer);

        channelTable->setItem(row, StateColumn, new QTableWidgetItem());
        channelTable->setItem(row, RecordingColumn, new QTableWidgetItem());
        resetChannelDisplay(row);
    }

    channelTable->resizeColumnsToContents();
    refreshChannelVisibility();
    updateChannelSelectionControls();
}

void MainWindow::refreshChannelVisibility()
{
    for (int row = 0; row < channelCatalog.size(); ++row) {
        const auto &channel = channelCatalog.at(row);
        channelTable->setRowHidden(row, showSelectedOnly && !isChannelSelected(channel.id));
    }

    showSelectedOnlyButton->setText(showSelectedOnly ? tr("Show All Channels") : tr("Show Selected Channels"));
}

void MainWindow::refreshWaterfallChannels(const zapiska::SdrStreamStats *stats)
{
    const zapiska::SdrStreamStats streamStats = stats ? *stats : sdrSource.stats();

    QVector<WaterfallChannelMarker> markers;
    markers.reserve(channelCatalog.size());
    for (const auto &channel : channelCatalog) {
        const bool selected = isChannelSelected(channel.id);
        markers.append(WaterfallChannelMarker {
            channel.name,
            channel.frequencyHz,
            selected,
            selected && liveAudioDesired && channelMonitorEnabledForChannel(channel.id),
            selected && channelUnsquelched(streamStats, channel.id),
        });
    }

    waterfallWidget->setChannelMarkers(markers);
}

void MainWindow::refreshFftControls()
{
    waterfallWidget->setVisible(fftVisible);
    fftButton->setText(fftVisible ? tr("FFT Off") : tr("FFT On"));
}

void MainWindow::updateChannelSelectionControls()
{
    const QSignalBlocker blocker(channelTable);
    const auto state = sdrSource.state();
    const bool canChangeSelection = state == zapiska::SdrSourceState::Closed
        || state == zapiska::SdrSourceState::Error;

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

bool MainWindow::channelMonitorEnabledForChannel(const QString &id) const
{
    return !mutedMonitorChannelIds.contains(id);
}

void MainWindow::updateChannelMonitorButton(int row)
{
    if (row < 0 || row >= channelCatalog.size()) {
        return;
    }

    auto *rowMonitorButton = qobject_cast<QPushButton *>(channelTable->cellWidget(row, MonitorColumn));
    if (!rowMonitorButton) {
        return;
    }

    const auto &channel = channelCatalog.at(row);
    const bool selected = isChannelSelected(channel.id);
    const bool monitorEnabled = channelMonitorEnabledForChannel(channel.id);
    rowMonitorButton->setText(monitorEnabled ? tr("Playing") : tr("Muted"));
    rowMonitorButton->setEnabled(selected);
}

int MainWindow::selectedChannelCount() const
{
    return selectedChannelIds.size();
}

void MainWindow::toggleSdrConnection()
{
    const auto state = sdrSource.state();
    if (state == zapiska::SdrSourceState::Open || state == zapiska::SdrSourceState::Streaming) {
        sdrSource.close();
        sdrStatusLabel->setText(tr("SDR: disconnected"));
        statusBar()->showMessage(tr("SDR disconnected"), 3000);
        refreshSdrControls();
        return;
    }

    sdrStatusLabel->setText(tr("SDR: connecting"));
    QString errorMessage;
    const zapiska::SdrSourceConfig config = buildSdrConfig();
    if (!sdrSource.open(config, &errorMessage)) {
        handleSdrError(errorMessage);
        refreshSdrControls();
        return;
    }

    updateSdrTuningControls(sdrSource.config());
    sdrStatusLabel->setText(tr("SDR: connected"));
    statusBar()->showMessage(tr("Connected to %1").arg(sdrSource.backendName()), 3000);
    if (!applyLiveAudioDesiredState()) {
        liveAudioDesired = false;
        applyLiveAudioDesiredState();
    }
    refreshWaterfallChannels();
    refreshSdrControls();
}

void MainWindow::toggleSdrStreaming()
{
    if (sdrSource.state() == zapiska::SdrSourceState::Streaming) {
        stopSdr();
        return;
    }

    startSdr();
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
    const bool previousDesired = liveAudioDesired;
    liveAudioDesired = !liveAudioDesired;
    if (!applyLiveAudioDesiredState()) {
        liveAudioDesired = previousDesired;
        applyLiveAudioDesiredState();
        refreshWaterfallChannels();
        refreshSdrControls();
        return;
    }

    sdrStatusLabel->setText(liveAudioDesired ? tr("SDR: playback enabled") : tr("SDR: playback muted"));
    statusBar()->showMessage(liveAudioDesired ? tr("Playback enabled") : tr("Playback muted"), 3000);
    refreshWaterfallChannels();
    refreshSdrControls();
}

bool MainWindow::applyLiveAudioDesiredState()
{
    const auto state = sdrSource.state();
    if (state != zapiska::SdrSourceState::Open && state != zapiska::SdrSourceState::Streaming) {
        return true;
    }

    QString errorMessage;
    if (!sdrSource.setLiveAudioEnabled(liveAudioDesired, &errorMessage)) {
        handleSdrError(errorMessage);
        return false;
    }

    return true;
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

void MainWindow::handleSdrStateChanged(zapiska::SdrSourceState state)
{
    if (state == zapiska::SdrSourceState::Open || state == zapiska::SdrSourceState::Streaming) {
        updateSdrTuningControls(sdrSource.config());
    }
    refreshSdrControls();
}

void MainWindow::handleSdrStatsUpdated(const zapiska::SdrStreamStats &stats)
{
    sampleCountLabel->setText(tr("Samples: %1").arg(formatSampleCount(stats.samplesRead)));
    widebandPowerLabel->setText(tr("Power: %1").arg(formatWidebandPower(stats)));
    updateChannelMeters(stats);
    refreshWaterfallChannels(&stats);
    refreshSdrControls();

    if (!stats.lastError.isEmpty()) {
        handleSdrError(stats.lastError);
    }
}

void MainWindow::handleSpectrumUpdated(const zapiska::SdrSpectrumFrame &frame)
{
    if (!fftVisible) {
        return;
    }

    waterfallWidget->setSpectrumFrame(frame);
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
    const bool isOpen = state == zapiska::SdrSourceState::Open
        || state == zapiska::SdrSourceState::Streaming;
    const bool isStreaming = state == zapiska::SdrSourceState::Streaming;
    const zapiska::SdrStreamStats stats = sdrSource.stats();
    const bool recording = stats.recording;

    connectButton->setText(isOpen ? tr("Disconnect") : tr("Connect"));
    connectButton->setEnabled(true);
    startButton->setText(isStreaming ? tr("Stop") : tr("Start"));
    startButton->setEnabled(state == zapiska::SdrSourceState::Open || isStreaming);
    monitorButton->setText(liveAudioDesired ? tr("Playing") : tr("Muted"));
    monitorButton->setEnabled(isOpen);
    recordButton->setText(recording ? tr("Stop Rec") : tr("Record"));
    recordButton->setEnabled(recording
        || (isStreaming && channelHasRecordableAudio(stats, QStringLiteral("16"))));
    centerFrequencySpin->setEnabled(!isOpen);
    sampleRateCombo->setEnabled(!isOpen);
    updateChannelSelectionControls();
}

void MainWindow::updateSdrTuningControls(const zapiska::SdrSourceConfig &config)
{
    const QSignalBlocker centerBlocker(centerFrequencySpin);
    centerFrequencySpin->setValue(static_cast<double>(config.centerFrequencyHz) / 1000000.0);

    const QSignalBlocker sampleRateBlocker(sampleRateCombo);
    const int sampleRateIndex = sampleRateCombo->findData(config.sampleRateHz);
    if (sampleRateIndex >= 0) {
        sampleRateCombo->setCurrentIndex(sampleRateIndex);
    }
}

zapiska::SdrSourceConfig MainWindow::buildSdrConfig() const
{
    zapiska::SdrSourceConfig config;
    config.centerFrequencyHz = static_cast<qint64>(std::llround(centerFrequencySpin->value() * 1000000.0));
    config.sampleRateHz = sampleRateCombo->currentData().isValid()
        ? sampleRateCombo->currentData().toInt()
        : zapiska::DefaultSdrSampleRateHz;
    config.channels.reserve(selectedChannelCount());

    for (const auto &channel : channelCatalog) {
        if (!isChannelSelected(channel.id)) {
            continue;
        }

        zapiska::SdrChannelConfig sdrChannel;
        sdrChannel.id = channel.id;
        sdrChannel.name = channel.name;
        sdrChannel.frequencyHz = channel.frequencyHz;
        sdrChannel.bandwidthHz = channel.bandwidthHz;
        sdrChannel.squelchMode = zapiska::SdrSquelchMode::Automatic;
        sdrChannel.squelchThresholdDbfs = squelchThresholdForChannel(channel.id);
        sdrChannel.monitorEnabled = channelMonitorEnabledForChannel(channel.id);
        sdrChannel.enabled = true;
        config.channels.append(sdrChannel);
    }

    return config;
}

void MainWindow::updateChannelMeters(const zapiska::SdrStreamStats &streamStats)
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

    auto *squelchItem = channelTable->item(row, StateColumn);
    if (squelchItem) {
        squelchItem->setText(idleText);
    }

    auto *recordingItem = channelTable->item(row, RecordingColumn);
    if (recordingItem) {
        recordingItem->setText(selected && channel.recordByDefault ? tr("armed") : tr("off"));
    }

    updateChannelMonitorButton(row);
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

bool MainWindow::channelHasRecordableAudio(const zapiska::SdrStreamStats &stats, const QString &id) const
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
    return QDir(basePath).filePath(QStringLiteral("Zapiska/recording_%1.wav").arg(timestamp));
}

double MainWindow::squelchThresholdForChannel(const QString &id) const
{
    return channelSquelchThresholds.value(id, DefaultSquelchThresholdDbfs);
}

QDoubleSpinBox *MainWindow::squelchThresholdSpinForRow(int row) const
{
    if (row < 0 || row >= channelCatalog.size()) {
        return nullptr;
    }

    auto *widget = channelTable->cellWidget(row, ThresholdColumn);
    if (!widget) {
        return nullptr;
    }

    if (auto *threshold = qobject_cast<QDoubleSpinBox *>(widget)) {
        return threshold;
    }

    return widget->findChild<QDoubleSpinBox *>();
}

void MainWindow::applyChannelSquelch(const QString &id)
{
    const int row = channelRow(id);
    if (row < 0) {
        return;
    }

    const auto *thresholdSpin = squelchThresholdSpinForRow(row);
    if (!thresholdSpin) {
        return;
    }

    const double threshold = thresholdSpin->value();
    channelSquelchThresholds.insert(id, threshold);
    saveChannelSquelchSettings();

    if (auto *signalMeter = dynamic_cast<SquelchSignalMeter *>(channelTable->cellWidget(row, SignalColumn))) {
        signalMeter->setSquelchThresholdDbfs(threshold);
    }

    if (!isChannelSelected(id)) {
        return;
    }

    const auto state = sdrSource.state();
    if (state != zapiska::SdrSourceState::Open && state != zapiska::SdrSourceState::Streaming) {
        return;
    }

    QString errorMessage;
    if (!sdrSource.setChannelSquelch(
            id,
            zapiska::SdrSquelchMode::Automatic,
            threshold,
            &errorMessage)) {
        handleSdrError(errorMessage);
    }
}

void MainWindow::setChannelSquelchThreshold(const QString &id, double thresholdDbfs)
{
    const int row = channelRow(id);
    if (row < 0) {
        return;
    }

    auto *thresholdSpin = squelchThresholdSpinForRow(row);
    if (!thresholdSpin) {
        return;
    }

    {
        const QSignalBlocker thresholdBlocker(thresholdSpin);
        thresholdSpin->setValue(thresholdDbfs);
    }

    applyChannelSquelch(id);
}

void MainWindow::autoSetChannelSquelch(const QString &id)
{
    double powerDbfs = 0.0;
    if (!currentChannelPowerDbfs(id, &powerDbfs)) {
        statusBar()->showMessage(tr("No signal power reading for this channel yet"), 3000);
        return;
    }

    const int row = channelRow(id);
    const auto *thresholdSpin = squelchThresholdSpinForRow(row);
    const double currentThreshold = thresholdSpin
        ? thresholdSpin->value()
        : squelchThresholdForChannel(id);

    double threshold = powerDbfs + AutoSquelchOffsetDb;
    if (threshold > MaximumAutoSquelchThresholdDbfs) {
        threshold = currentThreshold;
    }

    threshold = std::clamp(threshold, ResetSquelchThresholdDbfs, 0.0);
    setChannelSquelchThreshold(id, threshold);
    statusBar()->showMessage(
        tr("Auto squelch set to %1 dBFS").arg(QLocale::c().toString(threshold, 'f', 1)),
        3000);
}

void MainWindow::resetChannelSquelch(const QString &id)
{
    setChannelSquelchThreshold(id, ResetSquelchThresholdDbfs);
    statusBar()->showMessage(tr("Squelch reset to -150.0 dBFS"), 3000);
}

bool MainWindow::currentChannelPowerDbfs(const QString &id, double *powerDbfs) const
{
    if (!powerDbfs) {
        return false;
    }

    const auto stats = sdrSource.stats();
    for (const auto &channelStats : stats.channelStats) {
        if (channelStats.id == id && channelStats.hasPower) {
            *powerDbfs = channelStats.powerDbfs;
            return true;
        }
    }

    return false;
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
    if (state != zapiska::SdrSourceState::Closed && state != zapiska::SdrSourceState::Error) {
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
    refreshWaterfallChannels();
}

void MainWindow::toggleChannelMonitor(const QString &id)
{
    const int row = channelRow(id);
    if (row < 0 || !isChannelSelected(id)) {
        return;
    }

    const bool previousEnabled = channelMonitorEnabledForChannel(id);
    const bool nextEnabled = !previousEnabled;
    if (nextEnabled) {
        mutedMonitorChannelIds.remove(id);
    } else {
        mutedMonitorChannelIds.insert(id);
    }

    const auto state = sdrSource.state();
    if (state == zapiska::SdrSourceState::Open || state == zapiska::SdrSourceState::Streaming) {
        QString errorMessage;
        if (!sdrSource.setChannelMonitorEnabled(id, nextEnabled, &errorMessage)) {
            if (previousEnabled) {
                mutedMonitorChannelIds.remove(id);
            } else {
                mutedMonitorChannelIds.insert(id);
            }
            updateChannelMonitorButton(row);
            handleSdrError(errorMessage);
            return;
        }
    }

    saveChannelMonitorSettings();
    updateChannelMonitorButton(row);
    refreshWaterfallChannels();
    statusBar()->showMessage(
        nextEnabled ? tr("Channel playback enabled") : tr("Channel playback muted"),
        3000);
}

void MainWindow::toggleFftVisible()
{
    fftVisible = !fftVisible;
    refreshFftControls();
}

void MainWindow::toggleShowSelectedOnly(bool enabled)
{
    showSelectedOnly = enabled;
    refreshChannelVisibility();
}
