#pragma once

#include <QtGlobal>
#include <QString>
#include <QVector>

namespace zapiska {

struct ChannelConfig
{
    QString id;
    QString name;
    qint64 frequencyHz = 0;
    QString mode;
    int bandwidthHz = 0;
    bool enabledByDefault = false;
    bool recordByDefault = false;
};

QVector<ChannelConfig> defaultChannelCatalog();

QVector<ChannelConfig> loadChannelCatalogFromFile(const QString &filePath, QString *errorMessage = nullptr);

QString defaultChannelCatalogPath();

QString formatFrequencyMHz(qint64 frequencyHz);

} // namespace zapiska
