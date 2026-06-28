#include "GrOsmoSdrSource.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QThread>

namespace {

QString valueAfter(const QStringList &args, const QString &name)
{
    const int index = args.indexOf(name);
    if (index < 0 || index + 1 >= args.size()) {
        return {};
    }

    return args.at(index + 1);
}

QString optionalValueAfter(const QStringList &args, const QString &name)
{
    const QString value = valueAfter(args, name);
    return value.startsWith(QStringLiteral("--")) ? QString() : value;
}

int intValueAfter(const QStringList &args, const QString &name, int fallback)
{
    bool ok = false;
    const int value = valueAfter(args, name).toInt(&ok);
    return ok ? value : fallback;
}

bool writeZeroIqFile(QTemporaryFile &file)
{
    if (!file.open()) {
        return false;
    }

    const QByteArray zeros(1024 * 1024, '\0');
    return file.write(zeros) == zeros.size() && file.flush();
}

QString defaultFileDeviceArgs(QTemporaryFile &file)
{
    return QStringLiteral("file=%1,rate=96000,freq=156800000,repeat=true,throttle=true")
        .arg(file.fileName());
}

quint32 littleEndianUInt32(const QByteArray &bytes, int offset)
{
    return static_cast<quint32>(static_cast<unsigned char>(bytes.at(offset)))
        | (static_cast<quint32>(static_cast<unsigned char>(bytes.at(offset + 1))) << 8)
        | (static_cast<quint32>(static_cast<unsigned char>(bytes.at(offset + 2))) << 16)
        | (static_cast<quint32>(static_cast<unsigned char>(bytes.at(offset + 3))) << 24);
}

quint32 wavDataBytes(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return 0;
    }

    const QByteArray bytes = file.readAll();
    if (bytes.size() < 44
        || bytes.mid(0, 4) != QByteArrayLiteral("RIFF")
        || bytes.mid(8, 4) != QByteArrayLiteral("WAVE")) {
        return 0;
    }

    int offset = 12;
    while (offset + 8 <= bytes.size()) {
        const QByteArray chunkId = bytes.mid(offset, 4);
        const quint32 chunkSize = littleEndianUInt32(bytes, offset + 4);
        if (chunkId == QByteArrayLiteral("data")) {
            return chunkSize;
        }
        offset += 8 + static_cast<int>(chunkSize) + static_cast<int>(chunkSize % 2);
    }

    return 0;
}

bool runManualSquelchCheck(marine::GrOsmoSdrSource &source, QString *error)
{
    if (!source.setChannelSquelch(
            QStringLiteral("16"),
            marine::SdrSquelchMode::ForcedOpen,
            -70.0,
            error)) {
        qCritical() << "manual squelch check failed: force-open rejected:" << *error;
        return false;
    }

    auto stats = source.stats();
    if (stats.channelStats.isEmpty()
        || stats.channelStats.first().squelchMode != marine::SdrSquelchMode::ForcedOpen
        || !stats.channelStats.first().hasSquelch
        || !stats.channelStats.first().squelchOpen
        || stats.channelStats.first().squelchThresholdDbfs != -70.0) {
        qCritical() << "manual squelch check failed: force-open state was not applied";
        return false;
    }

    if (!source.setChannelSquelch(
            QStringLiteral("16"),
            marine::SdrSquelchMode::ForcedClosed,
            -55.5,
            error)) {
        qCritical() << "manual squelch check failed: force-muted rejected:" << *error;
        return false;
    }

    stats = source.stats();
    if (stats.channelStats.isEmpty()
        || stats.channelStats.first().squelchMode != marine::SdrSquelchMode::ForcedClosed
        || !stats.channelStats.first().hasSquelch
        || stats.channelStats.first().squelchOpen
        || stats.channelStats.first().squelchThresholdDbfs != -55.5) {
        qCritical() << "manual squelch check failed: force-muted state was not applied";
        return false;
    }

    if (!source.setChannelSquelch(
            QStringLiteral("16"),
            marine::SdrSquelchMode::Automatic,
            -45.0,
            error)) {
        qCritical() << "manual squelch check failed: auto restore rejected:" << *error;
        return false;
    }

    qInfo() << "manual squelch check completed";
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();

    QTemporaryFile fileSource;
    QTemporaryDir recordingDir;
    QString deviceArgs = valueAfter(args, QStringLiteral("--device-args"));
    if (deviceArgs.isEmpty()) {
        if (!writeZeroIqFile(fileSource)) {
            qCritical() << "failed to create temporary IQ file source";
            return 2;
        }
        deviceArgs = defaultFileDeviceArgs(fileSource);
    }

    const bool recordWav = args.contains(QStringLiteral("--record-wav"));
    QString recordingPath = optionalValueAfter(args, QStringLiteral("--record-wav"));
    if (recordWav && recordingPath.isEmpty()) {
        if (!recordingDir.isValid()) {
            qCritical() << "failed to create temporary recording directory";
            return 2;
        }
        recordingPath = recordingDir.filePath(QStringLiteral("channel16.wav"));
    }

    marine::GrOsmoSdrSource source;

    QString error;
    if (args.contains(QStringLiteral("--discover"))) {
        const auto devices = source.discoverDevices(&error);
        if (!error.isEmpty()) {
            qWarning() << "discovery warning:" << error;
        }
        qInfo() << "discovered devices:" << devices.size();
    }

    marine::SdrSourceConfig config;
    config.deviceArgs = deviceArgs;
    config.centerFrequencyHz = 156800000;
    config.sampleRateHz = intValueAfter(args, QStringLiteral("--sample-rate"), 96000);
    config.gainDb = 0.0;

    if (!source.open(config, &error)) {
        qCritical() << "open failed:" << error;
        return 3;
    }

    if (args.contains(QStringLiteral("--manual-squelch-check"))
        && !runManualSquelchCheck(source, &error)) {
        return 13;
    }

    if (args.contains(QStringLiteral("--live-audio"))) {
        if (!source.setLiveAudioEnabled(true, &error)) {
            qCritical() << "live audio failed:" << error;
            return 7;
        }
        qInfo() << "live audio enabled";
    }

    if (!source.start(&error)) {
        qCritical() << "start failed:" << error;
        return 4;
    }

    if (recordWav) {
        if (!source.startRecording(QStringLiteral("16"), recordingPath, &error)) {
            qCritical() << "recording failed:" << error;
            return 14;
        }
        qInfo() << "recording enabled:" << recordingPath;
    }

    const int durationMs = intValueAfter(args, QStringLiteral("--duration-ms"), 200);
    QThread::msleep(static_cast<unsigned long>(durationMs));

    if (recordWav) {
        source.stopRecording();
        const quint32 dataBytes = wavDataBytes(recordingPath);
        qInfo() << "recording data bytes:" << dataBytes;
        if (dataBytes == 0) {
            qCritical() << "smoke failed: recording WAV has no audio frames";
            return 15;
        }
    }

    source.stop();
    const marine::SdrStreamStats finalStats = source.stats();
    source.close();

    qInfo() << "samples read:" << finalStats.samplesRead
            << "wideband power dBFS:" << finalStats.widebandPowerDbfs;
    for (const auto &channelStats : finalStats.channelStats) {
        qInfo() << "channel:" << channelStats.id
                << "samples read:" << channelStats.samplesRead
                << "power dBFS:" << channelStats.powerDbfs
                << "audio samples read:" << channelStats.audioSamplesRead
                << "audio level dBFS:" << channelStats.audioLevelDbfs
                << "squelch:" << (channelStats.squelchOpen ? "open" : "squelched")
                << "threshold dBFS:" << channelStats.squelchThresholdDbfs;
    }
    if (finalStats.samplesRead == 0) {
        qCritical() << "smoke failed: no samples were read";
        return 5;
    }
    if (!finalStats.hasWidebandPower) {
        qCritical() << "smoke failed: no wideband power update was produced";
        return 6;
    }
    if (finalStats.channelStats.isEmpty()) {
        qCritical() << "smoke failed: no channel receivers were configured";
        return 8;
    }
    if (!finalStats.channelStats.first().hasPower) {
        qCritical() << "smoke failed: no channel power update was produced";
        return 9;
    }
    if (finalStats.channelStats.first().audioSamplesRead == 0) {
        qCritical() << "smoke failed: no demodulated audio samples were read";
        return 10;
    }
    if (!finalStats.channelStats.first().hasAudioLevel) {
        qCritical() << "smoke failed: no audio level update was produced";
        return 11;
    }
    if (!finalStats.channelStats.first().hasSquelch) {
        qCritical() << "smoke failed: no squelch state was produced";
        return 12;
    }

    qInfo() << "sdr smoke completed";
    return 0;
}
