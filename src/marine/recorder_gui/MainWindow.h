#pragma once

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

    void addSelectedChannel();
    void removeSelectedChannel();
    void updateRemoveButtonState();

    QLabel *deviceStateLabel = nullptr;
    QLabel *centerFrequencyLabel = nullptr;
    QLabel *sampleRateLabel = nullptr;
    QLabel *channelCatalogLabel = nullptr;
    QComboBox *channelSelector = nullptr;
    QPushButton *addChannelButton = nullptr;
    QPushButton *removeChannelButton = nullptr;
    QTableWidget *channelTable = nullptr;

    QVector<marine::ChannelConfig> channelCatalog;
    QVector<marine::ChannelConfig> visibleChannels;
};
