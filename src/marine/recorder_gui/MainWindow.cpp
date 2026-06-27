#include "MainWindow.h"

#include "MarineCore.h"

#include <QAbstractItemView>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    buildUi();
    loadChannels();
}

void MainWindow::buildUi()
{
    auto *root = new QWidget(this);
    auto *layout = new QVBoxLayout(root);

    auto *toolbar = new QWidget(root);
    auto *toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(0, 0, 0, 0);

    deviceStateLabel = new QLabel(tr("Device: disconnected"), toolbar);
    centerFrequencyLabel = new QLabel(tr("Center: 156.800 MHz"), toolbar);
    sampleRateLabel = new QLabel(tr("Sample rate: 2.000 MS/s"), toolbar);
    channelCatalogLabel = new QLabel(tr("Channels: loading"), toolbar);

    auto *connectButton = new QPushButton(tr("Connect"), toolbar);
    connectButton->setEnabled(false);
    auto *startButton = new QPushButton(tr("Start"), toolbar);
    startButton->setEnabled(false);
    auto *recordButton = new QPushButton(tr("Record"), toolbar);
    recordButton->setEnabled(false);

    toolbarLayout->addWidget(deviceStateLabel);
    toolbarLayout->addSpacing(16);
    toolbarLayout->addWidget(centerFrequencyLabel);
    toolbarLayout->addSpacing(16);
    toolbarLayout->addWidget(sampleRateLabel);
    toolbarLayout->addSpacing(16);
    toolbarLayout->addWidget(channelCatalogLabel);
    toolbarLayout->addStretch();
    toolbarLayout->addWidget(connectButton);
    toolbarLayout->addWidget(startButton);
    toolbarLayout->addWidget(recordButton);

    channelTable = new QTableWidget(root);
    channelTable->setColumnCount(7);
    channelTable->setHorizontalHeaderLabels({
        tr("Channel"),
        tr("Frequency"),
        tr("Mode"),
        tr("Bandwidth"),
        tr("Signal"),
        tr("Squelch"),
        tr("Recording"),
    });
    channelTable->horizontalHeader()->setStretchLastSection(true);
    channelTable->verticalHeader()->setVisible(false);
    channelTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    channelTable->setSelectionMode(QAbstractItemView::NoSelection);

    layout->addWidget(toolbar);
    layout->addWidget(channelTable);

    setCentralWidget(root);
    setWindowTitle(tr("Zapiska Marine Recorder"));
    resize(900, 420);
}

void MainWindow::loadChannels()
{
    const QString path = marine::defaultChannelConfigPath();
    QString errorMessage;
    const auto allChannels = marine::loadChannelsFromFile(path, &errorMessage);
    QVector<marine::ChannelConfig> visibleChannels;

    for (const auto &channel : allChannels) {
        if (channel.enabledByDefault) {
            visibleChannels.append(channel);
        }
    }

    if (allChannels.isEmpty()) {
        const auto fallbackChannels = marine::defaultChannels();
        channelCatalogLabel->setText(tr("Channels: fallback (%1)").arg(errorMessage));
        populateChannels(fallbackChannels);
        return;
    }

    if (visibleChannels.isEmpty()) {
        visibleChannels = allChannels;
    }

    channelCatalogLabel->setText(tr("Channels: %1 loaded, %2 enabled")
        .arg(allChannels.size())
        .arg(visibleChannels.size()));
    populateChannels(visibleChannels);
}

void MainWindow::populateChannels(const QVector<marine::ChannelConfig> &channels)
{
    channelTable->setRowCount(channels.size());

    for (int row = 0; row < channels.size(); ++row) {
        const auto &channel = channels.at(row);
        channelTable->setItem(row, 0, new QTableWidgetItem(channel.name));
        channelTable->setItem(row, 1, new QTableWidgetItem(marine::formatFrequencyMHz(channel.frequencyHz)));
        channelTable->setItem(row, 2, new QTableWidgetItem(channel.mode.toUpper()));
        channelTable->setItem(row, 3, new QTableWidgetItem(QString::number(channel.bandwidthHz) + tr(" Hz")));

        auto *signalMeter = new QProgressBar(channelTable);
        signalMeter->setRange(0, 100);
        signalMeter->setValue(0);
        signalMeter->setTextVisible(false);
        channelTable->setCellWidget(row, 4, signalMeter);

        channelTable->setItem(row, 5, new QTableWidgetItem(tr("closed")));
        channelTable->setItem(row, 6, new QTableWidgetItem(channel.recordByDefault ? tr("armed") : tr("off")));
    }
}
