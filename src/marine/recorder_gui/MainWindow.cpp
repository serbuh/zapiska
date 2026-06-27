#include "MainWindow.h"

#include "MarineCore.h"

#include <QAbstractItemView>
#include <QComboBox>
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

    auto *channelControls = new QWidget(root);
    auto *channelControlsLayout = new QHBoxLayout(channelControls);
    channelControlsLayout->setContentsMargins(0, 0, 0, 0);

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
    channelTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    channelTable->setSelectionMode(QAbstractItemView::SingleSelection);

    connect(addChannelButton, &QPushButton::clicked, this, &MainWindow::addSelectedChannel);
    connect(removeChannelButton, &QPushButton::clicked, this, &MainWindow::removeSelectedChannel);
    connect(channelTable, &QTableWidget::itemSelectionChanged, this, &MainWindow::updateRemoveButtonState);

    layout->addWidget(toolbar);
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
        signalMeter->setTextVisible(false);
        channelTable->setCellWidget(row, 4, signalMeter);

        channelTable->setItem(row, 5, new QTableWidgetItem(tr("closed")));
        channelTable->setItem(row, 6, new QTableWidgetItem(channel.recordByDefault ? tr("armed") : tr("off")));
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
