#include "MainWindow.h"

#include "MarineCore.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QStatusBar>
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

    channelSelector = new QComboBox(channelControls);
    channelSelector->setMinimumWidth(260);
    addChannelButton = new QPushButton(tr("Add Channel"), channelControls);
    addChannelButton->setEnabled(false);
    removeChannelButton = new QPushButton(tr("Remove Selected"), channelControls);
    removeChannelButton->setEnabled(false);

    channelControlsLayout->addWidget(new QLabel(tr("Catalog:"), channelControls));
    channelControlsLayout->addWidget(channelSelector, 1);
    channelControlsLayout->addWidget(addChannelButton);
    channelControlsLayout->addWidget(removeChannelButton);

    channelTable = new QTableWidget(root);
    channelTable->setColumnCount(10);
    channelTable->setHorizontalHeaderLabels({
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
    connect(addChannelButton, &QPushButton::clicked, this, &MainWindow::addSelectedChannel);
    connect(removeChannelButton, &QPushButton::clicked, this, &MainWindow::removeSelectedChannel);
    connect(channelTable, &QTableWidget::itemSelectionChanged, this, &MainWindow::updateRemoveButtonState);

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
    visibleChannels.clear();

    for (const auto &channel : channelCatalog) {
        if (channel.enabledByDefault) {
            visibleChannels.append(channel);
        }
    }

    if (channelCatalog.isEmpty()) {
        channelCatalog = marine::defaultChannels();
        visibleChannels = channelCatalog;
        channelCatalogLabel->setText(tr("Channels: fallback (%1)").arg(errorMessage));
    }

    if (visibleChannels.isEmpty()) {
        visibleChannels = channelCatalog;
    }

    populateChannelSelector();
    refreshChannelTable();
    updateChannelCatalogLabel();
    addChannelButton->setEnabled(!channelCatalog.isEmpty());
}

void MainWindow::populateChannelSelector()
{
    channelSelector->clear();

    for (int index = 0; index < channelCatalog.size(); ++index) {
        const auto &channel = channelCatalog.at(index);
        const QString label = tr("%1 - %2 (%3)")
            .arg(channel.name,
                 marine::formatFrequencyMHz(channel.frequencyHz),
                 channel.mode.toUpper());
        channelSelector->addItem(label, index);
    }
}

void MainWindow::refreshChannelTable()
{
    channelTable->clearContents();
    channelTable->setRowCount(visibleChannels.size());

    for (int row = 0; row < visibleChannels.size(); ++row) {
        const auto &channel = visibleChannels.at(row);
        channelTable->setItem(row, 0, new QTableWidgetItem(channel.name));
        channelTable->setItem(row, 1, new QTableWidgetItem(marine::formatFrequencyMHz(channel.frequencyHz)));
        channelTable->setItem(row, 2, new QTableWidgetItem(channel.mode.toUpper()));
        channelTable->setItem(row, 3, new QTableWidgetItem(QString::number(channel.bandwidthHz) + tr(" Hz")));

        auto *signalMeter = new QProgressBar(channelTable);
        signalMeter->setRange(0, 100);
        signalMeter->setValue(0);
        signalMeter->setFormat(tr("waiting"));
        signalMeter->setTextVisible(true);
        channelTable->setCellWidget(row, 4, signalMeter);

        auto *audioMeter = new QProgressBar(channelTable);
        audioMeter->setRange(0, 100);
        audioMeter->setValue(0);
        audioMeter->setFormat(tr("waiting"));
        audioMeter->setTextVisible(true);
        channelTable->setCellWidget(row, 5, audioMeter);

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
        channelTable->setCellWidget(row, 6, squelchMode);

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
        channelTable->setCellWidget(row, 7, threshold);

        channelTable->setItem(row, 8, new QTableWidgetItem(tr("waiting")));
        channelTable->setItem(row, 9, new QTableWidgetItem(channel.recordByDefault ? tr("armed") : tr("off")));
    }

    channelTable->resizeColumnsToContents();
    updateRemoveButtonState();
}

void MainWindow::updateChannelCatalogLabel()
{
    channelCatalogLabel->setText(tr("Channels: %1 loaded, %2 visible")
        .arg(channelCatalog.size())
        .arg(visibleChannels.size()));
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
    updateChannelMeters(stats.channelStats);

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

    connectButton->setText(isOpen ? tr("Disconnect") : tr("Connect"));
    connectButton->setEnabled(true);
    startButton->setEnabled(state == marine::SdrSourceState::Open);
    stopButton->setEnabled(isStreaming);
    monitorButton->setText(liveAudioEnabled ? tr("Mute") : tr("Monitor"));
    monitorButton->setEnabled(isOpen);
    recordButton->setEnabled(false);
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
    config.channels.reserve(visibleChannels.size());

    for (const auto &channel : visibleChannels) {
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

void MainWindow::updateChannelMeters(const QVector<marine::SdrChannelStats> &channelStats)
{
    for (const auto &stats : channelStats) {
        const int row = visibleChannelRow(stats.id);
        if (row < 0) {
            continue;
        }

        auto *signalMeter = qobject_cast<QProgressBar *>(channelTable->cellWidget(row, 4));
        if (!signalMeter) {
            continue;
        }

        signalMeter->setValue(stats.hasPower
                ? meterValue(stats.powerDbfs, MinimumMeterPowerDbfs, MaximumMeterPowerDbfs)
                : 0);
        signalMeter->setFormat(formatChannelPower(stats));

        auto *audioMeter = qobject_cast<QProgressBar *>(channelTable->cellWidget(row, 5));
        if (!audioMeter) {
            continue;
        }

        audioMeter->setValue(stats.hasAudioLevel
                ? meterValue(stats.audioLevelDbfs, MinimumAudioLevelDbfs, MaximumAudioLevelDbfs)
                : 0);
        audioMeter->setFormat(formatAudioLevel(stats));

        auto *squelchItem = channelTable->item(row, 8);
        if (squelchItem) {
            squelchItem->setText(formatSquelchState(stats));
        }
    }
}

int MainWindow::visibleChannelRow(const QString &id) const
{
    for (int row = 0; row < visibleChannels.size(); ++row) {
        if (visibleChannels.at(row).id == id) {
            return row;
        }
    }

    return -1;
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
    const int row = visibleChannelRow(id);
    if (row < 0) {
        return;
    }

    const auto *modeCombo = qobject_cast<QComboBox *>(channelTable->cellWidget(row, 6));
    const auto *thresholdSpin = qobject_cast<QDoubleSpinBox *>(channelTable->cellWidget(row, 7));
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

bool MainWindow::isChannelVisible(const QString &id) const
{
    for (const auto &channel : visibleChannels) {
        if (channel.id == id) {
            return true;
        }
    }

    return false;
}

void MainWindow::addSelectedChannel()
{
    const int catalogIndex = channelSelector->currentData().toInt();
    if (catalogIndex < 0 || catalogIndex >= channelCatalog.size()) {
        return;
    }

    const auto channel = channelCatalog.at(catalogIndex);
    if (isChannelVisible(channel.id)) {
        return;
    }

    visibleChannels.append(channel);
    refreshChannelTable();
    updateChannelCatalogLabel();
}

void MainWindow::removeSelectedChannel()
{
    const int row = channelTable->currentRow();
    if (row < 0 || row >= visibleChannels.size()) {
        return;
    }

    visibleChannels.removeAt(row);
    refreshChannelTable();
    updateChannelCatalogLabel();
}

void MainWindow::updateRemoveButtonState()
{
    removeChannelButton->setEnabled(channelTable->currentRow() >= 0 && !visibleChannels.isEmpty());
}
