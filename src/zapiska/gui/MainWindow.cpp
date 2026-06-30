#include "MainWindow.h"

#include "ChannelCatalog.h"
#include "WaterfallWidget.h"

#include <QAbstractItemView>
#include <QBrush>
#include <QCollator>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QLocale>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace {

constexpr double MinimumMeterPowerDbfs = -100.0;
constexpr double MaximumMeterPowerDbfs = -20.0;
constexpr double ResetSquelchThresholdDbfs = -150.0;
constexpr double DefaultSquelchThresholdDbfs = ResetSquelchThresholdDbfs;
constexpr double AutoSquelchOffsetDb = 3.0;
constexpr double MaximumAutoSquelchThresholdDbfs = -10.0;
constexpr int FftZoomSliderMinimum = 0;
constexpr int FftZoomSliderMaximum = 60;
constexpr int FftScrollMaximum = 1000;
constexpr int VolumeSliderMinimum = 0;
constexpr int VolumeSliderMaximum = 100;

constexpr int SelectedColumn = 0;
constexpr int ChannelNameColumn = 1;
constexpr int FrequencyColumn = 2;
constexpr int SignalColumn = 3;
constexpr int MonitorColumn = 4;
constexpr int ThresholdColumn = 5;
constexpr int StateColumn = 6;
constexpr int RecordingColumn = 7;
constexpr QRgb UnsquelchedStateColor = qRgba(255, 135, 30, 90);

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

int normalizedVolumePercent(int volumePercent)
{
    return std::clamp(volumePercent, VolumeSliderMinimum, VolumeSliderMaximum);
}

double liveAudioVolumeFromPercent(int volumePercent)
{
    return static_cast<double>(normalizedVolumePercent(volumePercent))
        / static_cast<double>(VolumeSliderMaximum);
}

QString formatVolumePercent(int volumePercent)
{
    return QStringLiteral("%1%").arg(normalizedVolumePercent(volumePercent));
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

double fftZoomFromSliderValue(int value)
{
    return std::pow(2.0, static_cast<double>(value) / 10.0);
}

int sliderValueFromFftZoom(double zoom)
{
    return std::clamp(
        static_cast<int>(std::lround(std::log2(std::max(1.0, zoom)) * 10.0)),
        FftZoomSliderMinimum,
        FftZoomSliderMaximum);
}

QString formatFftZoom(double zoom)
{
    return QLocale::c().toString(zoom, 'f', zoom < 10.0 ? 1 : 0) + QStringLiteral("x");
}

QString rawIqMetadataPathFor(const QString &rawIqPath)
{
    return rawIqPath + QStringLiteral(".json");
}

QString projectRootPath()
{
    const auto findRoot = [](const QString &startPath) {
        QDir dir(startPath);
        while (true) {
            if (QFileInfo::exists(dir.filePath(QStringLiteral("data/presets/marine-vhf.json")))) {
                return dir.absolutePath();
            }
            if (!dir.cdUp()) {
                return QString();
            }
        }
    };

    const QStringList candidates = {
        QDir::currentPath(),
        QCoreApplication::applicationDirPath(),
    };
    for (const auto &candidate : candidates) {
        const QString rootPath = findRoot(candidate);
        if (!rootPath.isEmpty()) {
            return rootPath;
        }
    }

    return QDir::currentPath();
}

QString recordsDirectoryPath()
{
    return QDir(projectRootPath()).filePath(QStringLiteral("records"));
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

MainWindow::~MainWindow()
{
    QObject::disconnect(&sdrSource, nullptr, this, nullptr);
    sdrSource.blockSignals(true);
    sdrSource.close();
}

void MainWindow::buildUi()
{
    auto *root = new QWidget(this);
    auto *layout = new QVBoxLayout(root);

    auto *topControls = new QWidget(root);
    auto *topControlsLayout = new QHBoxLayout(topControls);
    topControlsLayout->setContentsMargins(0, 0, 0, 0);

    auto *sdrControls = new QGroupBox(tr("SDR"), topControls);
    auto *sdrControlsLayout = new QVBoxLayout(sdrControls);
    auto *sdrConnectionLayout = new QHBoxLayout;
    auto *sdrMetricsLayout = new QHBoxLayout;

    auto *recordsControls = new QGroupBox(tr("Records"), topControls);
    auto *recordsControlsLayout = new QVBoxLayout(recordsControls);

    auto *playbackControls = new QWidget(root);
    auto *playbackControlsLayout = new QHBoxLayout(playbackControls);
    playbackControlsLayout->setContentsMargins(0, 0, 0, 0);

    auto *channelControls = new QWidget(root);
    auto *channelControlsLayout = new QHBoxLayout(channelControls);
    channelControlsLayout->setContentsMargins(0, 0, 0, 0);

    auto *channelFilterControls = new QWidget(root);
    auto *channelFilterControlsLayout = new QHBoxLayout(channelFilterControls);
    channelFilterControlsLayout->setContentsMargins(0, 0, 0, 0);

    centerFrequencySpin = new QDoubleSpinBox(sdrControls);
    centerFrequencySpin->setRange(1.000, 6000.000);
    centerFrequencySpin->setDecimals(3);
    centerFrequencySpin->setSingleStep(0.025);
    centerFrequencySpin->setSuffix(tr(" MHz"));
    centerFrequencySpin->setKeyboardTracking(false);
    centerFrequencySpin->setValue(static_cast<double>(zapiska::DefaultSdrCenterFrequencyHz) / 1000000.0);

    sampleRateCombo = new QComboBox(sdrControls);
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

    sampleCountLabel = new QLabel(tr("Samples: 0"), sdrControls);
    widebandPowerLabel = new QLabel(tr("Power: waiting"), sdrControls);
    sdrStatusLabel = new QLabel(tr("SDR: ready"), statusBar());
    sdrStatusLabel->setWordWrap(false);

    connectButton = new QPushButton(tr("Connect"), sdrControls);
    connectButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    startButton = new QPushButton(tr("Start"), sdrControls);
    startButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    startButton->setEnabled(false);
    volumeSlider = new QSlider(Qt::Horizontal, playbackControls);
    volumeSlider->setRange(VolumeSliderMinimum, VolumeSliderMaximum);
    volumeSlider->setSingleStep(5);
    volumeSlider->setPageStep(10);
    volumeSlider->setFixedWidth(160);
    volumeSlider->setToolTip(tr("Playback volume"));
    volumeSlider->setValue(liveAudioVolumePercent);
    volumeSlider->setEnabled(false);
    volumeLabel = new QLabel(formatVolumePercent(liveAudioVolumePercent), playbackControls);
    volumeLabel->setMinimumWidth(44);
    volumeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    volumeLabel->setEnabled(false);
    recordButton = new QPushButton(tr("Record"), recordsControls);
    recordButton->setEnabled(false);
    rawIqRecordButton = new QPushButton(tr("Record IQ"), recordsControls);
    rawIqRecordButton->setEnabled(false);
    auto *openRecordsButton = new QPushButton(tr("Open Records"), recordsControls);
    waterfallWidget = new WaterfallWidget(root);

    sourceModeCombo = new QComboBox(sdrControls);
    sourceModeCombo->addItem(tr("HackRF"), QStringLiteral("hackrf"));
    sourceModeCombo->addItem(tr("IQ File"), QStringLiteral("iq-file"));
    sourceModeCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    sourceModeCombo->setFixedWidth(sourceModeCombo->sizeHint().width());

    rawIqFileButton = new QPushButton(tr("Open IQ"), sdrControls);
    rawIqFileButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    rawIqFileEdit = new QLineEdit(sdrControls);
    rawIqFileEdit->setReadOnly(true);
    rawIqFileEdit->setPlaceholderText(tr("No raw IQ file selected"));
    rawIqFileEdit->setMinimumWidth(220);
    rawIqFileEdit->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    fftButton = new QPushButton(tr("FFT Hide"), playbackControls);
    showSelectedOnlyButton = new QPushButton(tr("Show All Channels"), channelFilterControls);
    fftZoomTitleLabel = new QLabel(tr("Zoom:"), channelControls);
    fftZoomLabel = new QLabel(formatFftZoom(waterfallWidget->horizontalZoom()), channelControls);
    fftZoomLabel->setMinimumWidth(44);

    fftZoomSlider = new QSlider(Qt::Horizontal, channelControls);
    fftZoomSlider->setRange(FftZoomSliderMinimum, FftZoomSliderMaximum);
    fftZoomSlider->setSingleStep(1);
    fftZoomSlider->setPageStep(10);
    fftZoomSlider->setFixedWidth(140);
    fftZoomSlider->setToolTip(tr("FFT horizontal zoom"));

    fftScrollBar = new QScrollBar(Qt::Horizontal, channelControls);
    fftScrollBar->setRange(0, FftScrollMaximum);
    fftScrollBar->setSingleStep(10);
    fftScrollBar->setPageStep(FftScrollMaximum);
    fftScrollBar->setFixedWidth(180);
    fftScrollBar->setToolTip(tr("FFT horizontal scroll"));

    sdrConnectionLayout->addWidget(connectButton);
    sdrConnectionLayout->addWidget(startButton);
    sdrConnectionLayout->addSpacing(12);
    sdrConnectionLayout->addWidget(new QLabel(tr("Source:"), sdrControls));
    sdrConnectionLayout->addWidget(sourceModeCombo);
    sdrConnectionLayout->addSpacing(12);
    sdrConnectionLayout->addWidget(rawIqFileButton);
    sdrConnectionLayout->addWidget(rawIqFileEdit);
    sdrConnectionLayout->addStretch();

    sdrMetricsLayout->addWidget(new QLabel(tr("Center:"), sdrControls));
    sdrMetricsLayout->addWidget(centerFrequencySpin);
    sdrMetricsLayout->addSpacing(16);
    sdrMetricsLayout->addWidget(new QLabel(tr("Sample rate:"), sdrControls));
    sdrMetricsLayout->addWidget(sampleRateCombo);
    sdrMetricsLayout->addSpacing(16);
    sdrMetricsLayout->addWidget(sampleCountLabel);
    sdrMetricsLayout->addSpacing(16);
    sdrMetricsLayout->addWidget(widebandPowerLabel);
    sdrMetricsLayout->addStretch();

    sdrControlsLayout->addLayout(sdrConnectionLayout);
    sdrControlsLayout->addLayout(sdrMetricsLayout);
    statusBar()->addWidget(sdrStatusLabel, 1);

    recordsControlsLayout->addWidget(rawIqRecordButton);
    recordsControlsLayout->addWidget(recordButton);
    recordsControlsLayout->addWidget(openRecordsButton);
    recordsControlsLayout->addStretch();

    topControlsLayout->addWidget(sdrControls, 1);
    topControlsLayout->addWidget(recordsControls);
    topControls->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    sdrControls->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    recordsControls->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    playbackControlsLayout->addWidget(new QLabel(tr("Volume:"), playbackControls));
    playbackControlsLayout->addWidget(volumeSlider);
    playbackControlsLayout->addWidget(volumeLabel);
    playbackControlsLayout->addSpacing(12);
    playbackControlsLayout->addWidget(fftButton);
    playbackControlsLayout->addStretch();

    channelControlsLayout->addWidget(fftZoomTitleLabel);
    channelControlsLayout->addWidget(fftZoomSlider);
    channelControlsLayout->addWidget(fftZoomLabel);
    channelControlsLayout->addWidget(fftScrollBar);
    channelControlsLayout->addStretch();

    channelFilterControlsLayout->addWidget(showSelectedOnlyButton);
    channelFilterControlsLayout->addStretch();

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
    channelTable->horizontalHeader()->setSectionsClickable(true);
    channelTable->verticalHeader()->setVisible(false);
    channelTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    channelTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    channelTable->setSelectionMode(QAbstractItemView::SingleSelection);

    connect(connectButton, &QPushButton::clicked, this, &MainWindow::toggleSdrConnection);
    connect(startButton, &QPushButton::clicked, this, &MainWindow::toggleSdrStreaming);
    connect(volumeSlider, &QSlider::valueChanged, this, &MainWindow::handleLiveAudioVolumeChanged);
    connect(recordButton, &QPushButton::clicked, this, &MainWindow::toggleRecording);
    connect(rawIqRecordButton, &QPushButton::clicked, this, &MainWindow::toggleRawIqRecording);
    connect(openRecordsButton, &QPushButton::clicked, this, &MainWindow::openRecordsDirectory);
    connect(fftButton, &QPushButton::clicked, this, &MainWindow::toggleFftVisible);
    connect(
        sourceModeCombo,
        static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
        this,
        &MainWindow::handleSourceModeChanged);
    connect(rawIqFileButton, &QPushButton::clicked, this, &MainWindow::selectRawIqReplayFile);
    connect(fftZoomSlider, &QSlider::valueChanged, this, [this](int value) {
        waterfallWidget->setHorizontalZoom(fftZoomFromSliderValue(value));
    });
    connect(fftScrollBar, &QScrollBar::valueChanged, this, [this](int value) {
        waterfallWidget->setHorizontalPanRatio(
            static_cast<double>(value) / static_cast<double>(FftScrollMaximum));
    });
    connect(waterfallWidget, &WaterfallWidget::horizontalViewChanged, this, [this]() {
        refreshFftViewControls();
    });
    connect(showSelectedOnlyButton, &QPushButton::clicked, this, [this]() {
        toggleShowSelectedOnly(!showSelectedOnly);
    });
    connect(
        channelTable->horizontalHeader(),
        &QHeaderView::sectionClicked,
        this,
        &MainWindow::handleChannelHeaderClicked);
    connect(channelTable, &QTableWidget::itemChanged, this, &MainWindow::handleChannelItemChanged);

    layout->addWidget(topControls);
    layout->addWidget(playbackControls);
    layout->addWidget(waterfallWidget);
    layout->addWidget(channelControls);
    layout->addWidget(channelFilterControls);
    layout->addWidget(channelTable);

    setCentralWidget(root);
    setWindowTitle(tr("Zapiska"));
    resize(900, 420);
    refreshSourceModeControls();
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
    if (channelSortColumn >= 0) {
        channelTable->horizontalHeader()->setSortIndicator(channelSortColumn, channelSortOrder);
        channelTable->horizontalHeader()->setSortIndicatorShown(true);
    }
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

void MainWindow::handleChannelHeaderClicked(int section)
{
    if (section != ChannelNameColumn && section != FrequencyColumn) {
        return;
    }

    channelSortOrder = channelSortColumn == section && channelSortOrder == Qt::AscendingOrder
        ? Qt::DescendingOrder
        : Qt::AscendingOrder;
    channelSortColumn = section;

    sortChannelCatalog();
    refreshChannelTable();
    refreshWaterfallChannels();
}

void MainWindow::sortChannelCatalog()
{
    if (channelSortColumn != ChannelNameColumn && channelSortColumn != FrequencyColumn) {
        return;
    }

    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);

    const int sortColumn = channelSortColumn;
    const Qt::SortOrder sortOrder = channelSortOrder;
    std::stable_sort(channelCatalog.begin(), channelCatalog.end(), [&](const auto &left, const auto &right) {
        int result = 0;
        if (sortColumn == FrequencyColumn) {
            if (left.frequencyHz < right.frequencyHz) {
                result = -1;
            } else if (left.frequencyHz > right.frequencyHz) {
                result = 1;
            }
        } else {
            result = collator.compare(left.name, right.name);
        }

        if (result == 0) {
            result = collator.compare(left.name, right.name);
        }
        if (result == 0) {
            if (left.frequencyHz < right.frequencyHz) {
                result = -1;
            } else if (left.frequencyHz > right.frequencyHz) {
                result = 1;
            }
        }
        if (result == 0) {
            result = collator.compare(left.id, right.id);
        }

        return sortOrder == Qt::AscendingOrder ? result < 0 : result > 0;
    });
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
            selected && channelUnsquelched(streamStats, channel.id),
        });
    }

    waterfallWidget->setChannelMarkers(markers);
}

void MainWindow::refreshFftControls()
{
    waterfallWidget->setVisible(fftVisible);
    fftButton->setText(fftVisible ? tr("FFT Hide") : tr("FFT Show"));
    fftZoomTitleLabel->setVisible(fftVisible);
    fftZoomLabel->setVisible(fftVisible);
    fftZoomSlider->setVisible(fftVisible);
    fftScrollBar->setVisible(fftVisible);
    refreshFftViewControls();
}

void MainWindow::refreshFftViewControls()
{
    const double zoom = waterfallWidget->horizontalZoom();
    const bool zoomed = zoom > 1.01;

    {
        const QSignalBlocker blocker(fftZoomSlider);
        fftZoomSlider->setValue(sliderValueFromFftZoom(zoom));
    }

    {
        const QSignalBlocker blocker(fftScrollBar);
        fftScrollBar->setEnabled(zoomed && fftVisible);
        fftScrollBar->setPageStep(std::max(1, static_cast<int>(std::lround(
            static_cast<double>(FftScrollMaximum) / zoom))));
        fftScrollBar->setValue(static_cast<int>(std::lround(
            waterfallWidget->horizontalPanRatio() * static_cast<double>(FftScrollMaximum))));
    }

    fftZoomLabel->setText(formatFftZoom(zoom));
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
    if (rawIqReplayModeEnabled() && rawIqDeviceArgs(&errorMessage).isEmpty()) {
        handleSdrError(errorMessage);
        refreshSdrControls();
        return;
    }
    const zapiska::SdrSourceConfig config = buildSdrConfig();
    if (!sdrSource.open(config, &errorMessage)) {
        handleSdrError(errorMessage);
        refreshSdrControls();
        return;
    }

    updateSdrTuningControls(sdrSource.config());
    sdrStatusLabel->setText(tr("SDR: connected"));
    statusBar()->showMessage(tr("Connected to %1").arg(sdrSource.backendName()), 3000);
    if (!applyLiveAudioVolume()) {
        liveAudioVolumePercent = 0;
        refreshLiveAudioVolumeControls();
        applyLiveAudioVolume();
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

void MainWindow::handleLiveAudioVolumeChanged(int volumePercent)
{
    const int previousVolumePercent = liveAudioVolumePercent;
    liveAudioVolumePercent = normalizedVolumePercent(volumePercent);
    refreshLiveAudioVolumeControls();

    if (!applyLiveAudioVolume()) {
        liveAudioVolumePercent = previousVolumePercent;
        refreshLiveAudioVolumeControls();
        applyLiveAudioVolume();
        refreshWaterfallChannels();
        refreshSdrControls();
        return;
    }

    const auto state = sdrSource.state();
    if (state == zapiska::SdrSourceState::Open || state == zapiska::SdrSourceState::Streaming) {
        sdrStatusLabel->setText(tr("SDR: playback volume %1").arg(formatVolumePercent(liveAudioVolumePercent)));
        statusBar()->showMessage(
            tr("Playback volume %1").arg(formatVolumePercent(liveAudioVolumePercent)),
            1500);
    }

    refreshWaterfallChannels();
    refreshSdrControls();
}

bool MainWindow::applyLiveAudioVolume()
{
    const auto state = sdrSource.state();
    if (state != zapiska::SdrSourceState::Open && state != zapiska::SdrSourceState::Streaming) {
        return true;
    }

    QString errorMessage;
    if (!sdrSource.setLiveAudioVolume(liveAudioVolumeFromPercent(liveAudioVolumePercent), &errorMessage)) {
        handleSdrError(errorMessage);
        return false;
    }
    if (!sdrSource.setLiveAudioEnabled(liveAudioVolumePercent > 0, &errorMessage)) {
        handleSdrError(errorMessage);
        return false;
    }

    return true;
}

void MainWindow::refreshLiveAudioVolumeControls()
{
    if (!volumeSlider || !volumeLabel) {
        return;
    }

    liveAudioVolumePercent = normalizedVolumePercent(liveAudioVolumePercent);

    const QSignalBlocker blocker(volumeSlider);
    volumeSlider->setValue(liveAudioVolumePercent);
    volumeLabel->setText(formatVolumePercent(liveAudioVolumePercent));
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

void MainWindow::toggleRawIqRecording()
{
    if (sdrSource.rawIqRecording()) {
        const QString recordingPath = sdrSource.stats().rawIqRecordingPath;
        const QString metadataPath = sdrSource.stats().rawIqMetadataPath;
        sdrSource.stopRawIqRecording();
        sdrStatusLabel->setText(tr("SDR: raw IQ recording stopped"));
        statusBar()->showMessage(
            tr("Raw IQ saved to %1 (%2)").arg(recordingPath, metadataPath),
            6000);
        refreshSdrControls();
        return;
    }

    const QString recordingPath = nextRawIqRecordingPath();
    QString errorMessage;
    if (!sdrSource.startRawIqRecording(recordingPath, &errorMessage)) {
        handleSdrError(errorMessage);
        refreshSdrControls();
        return;
    }

    sdrStatusLabel->setText(tr("SDR: recording raw IQ"));
    statusBar()->showMessage(tr("Recording raw IQ to %1").arg(recordingPath), 6000);
    refreshSdrControls();
}

void MainWindow::openRecordsDirectory()
{
    const QString path = recordsDirectoryPath();
    if (!QDir(path).exists() && !QDir().mkpath(path)) {
        handleSdrError(tr("Could not create records directory %1").arg(path));
        return;
    }

    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
        handleSdrError(tr("Could not open records directory %1").arg(path));
        return;
    }

    statusBar()->showMessage(tr("Opened records directory %1").arg(path), 3000);
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
    const bool rawIqRecording = stats.rawIqRecording;

    connectButton->setText(isOpen ? tr("Disconnect") : tr("Connect"));
    connectButton->setEnabled(true);
    startButton->setText(isStreaming ? tr("Stop") : tr("Start"));
    startButton->setEnabled(state == zapiska::SdrSourceState::Open || isStreaming);
    refreshLiveAudioVolumeControls();
    volumeSlider->setEnabled(isOpen);
    volumeLabel->setEnabled(isOpen);
    recordButton->setText(recording ? tr("Stop Rec") : tr("Record"));
    recordButton->setEnabled(recording
        || (isStreaming && channelHasRecordableAudio(stats, QStringLiteral("16"))));
    rawIqRecordButton->setText(rawIqRecording ? tr("Stop IQ") : tr("Record IQ"));
    rawIqRecordButton->setEnabled(rawIqRecording || isStreaming);
    centerFrequencySpin->setEnabled(!isOpen);
    sampleRateCombo->setEnabled(!isOpen);
    sourceModeCombo->setEnabled(!isOpen);
    refreshSourceModeControls();
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
    if (rawIqReplayModeEnabled()) {
        config.deviceArgs = rawIqDeviceArgs();
    }
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

        updateChannelStateHighlight(row, stats.hasSquelch && stats.squelchOpen);
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

    updateChannelStateHighlight(row, false);
    updateChannelMonitorButton(row);
}

void MainWindow::updateChannelStateHighlight(int row, bool unsquelched)
{
    if (row < 0 || row >= channelTable->rowCount()) {
        return;
    }

    auto *stateItem = channelTable->item(row, StateColumn);
    if (!stateItem) {
        return;
    }

    stateItem->setBackground(unsquelched
            ? QBrush(QColor::fromRgba(UnsquelchedStateColor))
            : QBrush());
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
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    return QDir(recordsDirectoryPath()).filePath(
        QStringLiteral("WAV/recording_%1.wav").arg(timestamp));
}

QString MainWindow::nextRawIqRecordingPath() const
{
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    return QDir(recordsDirectoryPath()).filePath(
        QStringLiteral("IQ/raw_iq_%1.cfile").arg(timestamp));
}

void MainWindow::handleSourceModeChanged()
{
    refreshSourceModeControls();
}

void MainWindow::selectRawIqReplayFile()
{
    QString basePath = rawIqReplayPath;
    if (basePath.isEmpty()) {
        basePath = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    }
    if (basePath.isEmpty()) {
        basePath = QDir::currentPath();
    }

    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("Open Raw IQ File"),
        basePath,
        tr("Raw IQ files (*.cfile *.iq *.raw);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }

    rawIqReplayPath = QFileInfo(path).absoluteFilePath();
    rawIqFileEdit->setText(rawIqReplayPath);

    qint64 centerFrequencyHz = 0;
    int sampleRateHz = 0;
    QString errorMessage;
    if (loadRawIqMetadata(rawIqReplayPath, &centerFrequencyHz, &sampleRateHz, &errorMessage)) {
        centerFrequencySpin->setValue(static_cast<double>(centerFrequencyHz) / 1000000.0);
        int sampleRateIndex = sampleRateCombo->findData(sampleRateHz);
        if (sampleRateIndex < 0) {
            const QString label = QLocale::c().toString(
                    static_cast<double>(sampleRateHz) / 1000000.0,
                    'f',
                    3)
                + tr("M");
            sampleRateCombo->addItem(label, sampleRateHz);
            sampleRateIndex = sampleRateCombo->findData(sampleRateHz);
        }
        sampleRateCombo->setCurrentIndex(sampleRateIndex);
        statusBar()->showMessage(tr("Loaded raw IQ metadata"), 3000);
    } else if (!errorMessage.isEmpty()) {
        statusBar()->showMessage(errorMessage, 4000);
    }

    refreshSourceModeControls();
}

void MainWindow::refreshSourceModeControls()
{
    if (!sourceModeCombo || !rawIqFileButton || !rawIqFileEdit) {
        return;
    }

    const auto state = sdrSource.state();
    const bool isOpen = state == zapiska::SdrSourceState::Open
        || state == zapiska::SdrSourceState::Streaming;
    const bool replayMode = rawIqReplayModeEnabled();

    rawIqFileButton->setEnabled(replayMode && !isOpen);
    rawIqFileEdit->setEnabled(replayMode);
    rawIqFileButton->setVisible(replayMode);
    rawIqFileEdit->setVisible(replayMode);
}

bool MainWindow::rawIqReplayModeEnabled() const
{
    return sourceModeCombo
        && sourceModeCombo->currentData().toString() == QStringLiteral("iq-file");
}

QString MainWindow::rawIqDeviceArgs(QString *errorMessage) const
{
    if (errorMessage) {
        errorMessage->clear();
    }
    if (rawIqReplayPath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = tr("Select a raw IQ file before connecting");
        }
        return {};
    }

    const QFileInfo fileInfo(rawIqReplayPath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        if (errorMessage) {
            *errorMessage = tr("Raw IQ file does not exist: %1").arg(rawIqReplayPath);
        }
        return {};
    }

    const qint64 centerFrequencyHz = static_cast<qint64>(
        std::llround(centerFrequencySpin->value() * 1000000.0));
    const int sampleRateHz = sampleRateCombo->currentData().isValid()
        ? sampleRateCombo->currentData().toInt()
        : zapiska::DefaultSdrSampleRateHz;

    return QStringLiteral("file=%1,rate=%2,freq=%3,repeat=true,throttle=true")
        .arg(fileInfo.absoluteFilePath())
        .arg(sampleRateHz)
        .arg(centerFrequencyHz);
}

bool MainWindow::loadRawIqMetadata(const QString &path,
    qint64 *centerFrequencyHz,
    int *sampleRateHz,
    QString *errorMessage) const
{
    if (errorMessage) {
        errorMessage->clear();
    }

    QFile metadataFile(rawIqMetadataPathFor(path));
    if (!metadataFile.exists()) {
        if (errorMessage) {
            *errorMessage = tr("No raw IQ metadata found; using current tuning controls");
        }
        return false;
    }
    if (!metadataFile.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = tr("Could not open raw IQ metadata: %1").arg(metadataFile.errorString());
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(metadataFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (errorMessage) {
            *errorMessage = tr("Could not parse raw IQ metadata: %1").arg(parseError.errorString());
        }
        return false;
    }

    const QJsonObject object = document.object();
    const auto centerValue = object.value(QStringLiteral("center_frequency_hz"));
    const auto sampleRateValue = object.value(QStringLiteral("sample_rate_hz"));
    if (!centerValue.isDouble() || !sampleRateValue.isDouble()) {
        if (errorMessage) {
            *errorMessage = tr("Raw IQ metadata is missing center frequency or sample rate");
        }
        return false;
    }

    if (centerFrequencyHz) {
        *centerFrequencyHz = static_cast<qint64>(std::llround(centerValue.toDouble()));
    }
    if (sampleRateHz) {
        *sampleRateHz = static_cast<int>(std::lround(sampleRateValue.toDouble()));
    }
    return true;
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
