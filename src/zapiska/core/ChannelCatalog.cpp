#include "ChannelCatalog.h"

#include "SdrSource.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLocale>
#include <QSaveFile>

namespace zapiska {

namespace {

QString stringValue(const QJsonObject &object, const char *key)
{
    return object.value(QLatin1String(key)).toString();
}

qint64 integerValue(const QJsonObject &object, const char *key)
{
    return static_cast<qint64>(object.value(QLatin1String(key)).toDouble());
}

bool boolValue(const QJsonObject &object, const char *key)
{
    return object.value(QLatin1String(key)).toBool();
}

QString fallbackMode(const QString &mode, const QString &modulation)
{
    if (!mode.isEmpty()) {
        return mode;
    }

    if (modulation == QLatin1String("Narrow FM")) {
        return QStringLiteral("nfm");
    }

    QString normalized = modulation.toLower();
    normalized.replace(QLatin1Char(' '), QLatin1Char('_'));
    return normalized;
}

} // namespace

QVector<ChannelConfig> defaultChannelCatalog()
{
    return {
        {
            QStringLiteral("16"),
            QStringLiteral("Marine Channel 16"),
            DefaultChannelFrequencyHz,
            QStringLiteral("nfm"),
            10000,
            true,
            true,
        },
    };
}

QVector<ChannelConfig> loadChannelCatalogFromFile(const QString &filePath, QString *errorMessage)
{
    if (errorMessage) {
        errorMessage->clear();
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to open %1: %2")
                .arg(filePath, file.errorString());
        }
        return {};
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (document.isNull()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to parse %1: %2")
                .arg(filePath, parseError.errorString());
        }
        return {};
    }

    if (!document.isObject()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Expected a JSON object in %1").arg(filePath);
        }
        return {};
    }

    const auto root = document.object();
    const auto channelValues = root.value(QStringLiteral("channels")).toArray();
    QVector<ChannelConfig> channels;
    channels.reserve(channelValues.size());

    for (const auto &value : channelValues) {
        if (!value.isObject()) {
            continue;
        }

        const auto object = value.toObject();
        const auto id = stringValue(object, "id");
        const auto name = stringValue(object, "name");
        const auto frequencyHz = integerValue(object, "frequency_hz");

        if (id.isEmpty() || name.isEmpty() || frequencyHz <= 0) {
            continue;
        }

        const auto modulation = stringValue(object, "modulation");

        ChannelConfig channel;
        channel.id = id;
        channel.name = name;
        channel.frequencyHz = frequencyHz;
        channel.mode = fallbackMode(stringValue(object, "mode"), modulation);
        channel.bandwidthHz = static_cast<int>(integerValue(object, "bandwidth_hz"));
        channel.enabledByDefault = boolValue(object, "enabled_by_default");
        channel.recordByDefault = boolValue(object, "record_by_default");
        channel.notes = stringValue(object, "notes");
        channels.append(channel);
    }

    if (channels.isEmpty() && errorMessage) {
        *errorMessage = QStringLiteral("No valid channels found in %1").arg(filePath);
    }

    return channels;
}

bool saveChannelCatalogNotesToFile(
    const QString &filePath,
    const QVector<ChannelConfig> &channels,
    QString *errorMessage)
{
    if (errorMessage) {
        errorMessage->clear();
    }

    if (filePath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Channel catalog path is empty");
        }
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to open %1: %2")
                .arg(filePath, file.errorString());
        }
        return false;
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to parse %1: %2")
                .arg(filePath, parseError.errorString());
        }
        return false;
    }

    QHash<QString, QString> notesById;
    for (const auto &channel : channels) {
        notesById.insert(channel.id, channel.notes);
    }

    QJsonObject root = document.object();
    QJsonArray channelValues = root.value(QStringLiteral("channels")).toArray();
    for (int index = 0; index < channelValues.size(); ++index) {
        if (!channelValues.at(index).isObject()) {
            continue;
        }

        QJsonObject channelObject = channelValues.at(index).toObject();
        const QString id = stringValue(channelObject, "id");
        if (!notesById.contains(id)) {
            continue;
        }

        const QString notes = notesById.value(id);
        if (notes.isEmpty()) {
            channelObject.remove(QStringLiteral("notes"));
        } else {
            channelObject.insert(QStringLiteral("notes"), notes);
        }
        channelValues.replace(index, channelObject);
    }
    root.insert(QStringLiteral("channels"), channelValues);

    QSaveFile saveFile(filePath);
    if (!saveFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to write %1: %2")
                .arg(filePath, saveFile.errorString());
        }
        return false;
    }

    const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (saveFile.write(bytes) != bytes.size()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to write %1: %2")
                .arg(filePath, saveFile.errorString());
        }
        return false;
    }

    if (!saveFile.commit()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Unable to save %1: %2")
                .arg(filePath, saveFile.errorString());
        }
        return false;
    }

    return true;
}

QString defaultChannelCatalogPath()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir::current().filePath(QStringLiteral("data/presets/marine-vhf.json")),
        QDir(appDir).filePath(QStringLiteral("data/presets/marine-vhf.json")),
    };

    for (const auto &candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }

    return candidates.first();
}

QString formatFrequencyMHz(qint64 frequencyHz)
{
    const double mhz = static_cast<double>(frequencyHz) / 1000000.0;
    return QLocale::c().toString(mhz, 'f', 3) + QStringLiteral(" MHz");
}

} // namespace zapiska
