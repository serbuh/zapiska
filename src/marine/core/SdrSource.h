#pragma once

#include <QObject>
#include <QString>
#include <QVector>

#include <complex>

namespace marine {

enum class SdrSourceState
{
    Closed,
    Open,
    Streaming,
    Error,
};

struct SdrDeviceInfo
{
    QString displayName;
    QString driver;
    QString serial;
    QString deviceArgs;
};

struct SdrChannelConfig
{
    QString id;
    QString name;
    qint64 frequencyHz = 156800000;
    int bandwidthHz = 10000;
    bool enabled = true;
};

struct SdrSourceConfig
{
    QString deviceArgs;
    qint64 centerFrequencyHz = 156800000;
    int sampleRateHz = 2000000;
    double gainDb = 0.0;
    int blockSize = 16384;
    QVector<SdrChannelConfig> channels;
};

struct SdrChannelStats
{
    QString id;
    QString name;
    qint64 frequencyHz = 0;
    qint64 offsetHz = 0;
    int bandwidthHz = 0;
    int sampleRateHz = 0;
    quint64 samplesRead = 0;
    bool hasPower = false;
    double powerDbfs = -200.0;
};

struct SdrStreamStats
{
    bool running = false;
    quint64 samplesRead = 0;
    quint64 droppedSamples = 0;
    bool hasWidebandPower = false;
    double widebandPowerDbfs = -200.0;
    QVector<SdrChannelStats> channelStats;
    QString lastError;
};

struct IqBlock
{
    QVector<std::complex<float>> samples;
    qint64 centerFrequencyHz = 0;
    int sampleRateHz = 0;
    quint64 firstSampleIndex = 0;
    qint64 timestampUsec = 0;
};

class SdrSource : public QObject
{
    Q_OBJECT

public:
    explicit SdrSource(QObject *parent = nullptr);
    ~SdrSource() override;

    virtual QString backendName() const = 0;
    virtual QVector<SdrDeviceInfo> discoverDevices(QString *errorMessage = nullptr) = 0;
    virtual bool open(const SdrSourceConfig &config, QString *errorMessage = nullptr) = 0;
    virtual void close() = 0;
    virtual bool start(QString *errorMessage = nullptr) = 0;
    virtual void stop() = 0;
    virtual SdrSourceState state() const = 0;
    virtual SdrSourceConfig config() const = 0;
    virtual SdrStreamStats stats() const = 0;

signals:
    void stateChanged(marine::SdrSourceState state);
    void iqBlockReady(const marine::IqBlock &block);
    void statsUpdated(const marine::SdrStreamStats &stats);
    void errorOccurred(const QString &message);
};

void registerSdrSourceMetaTypes();

} // namespace marine

Q_DECLARE_METATYPE(marine::SdrSourceState)
Q_DECLARE_METATYPE(marine::SdrDeviceInfo)
Q_DECLARE_METATYPE(marine::SdrChannelConfig)
Q_DECLARE_METATYPE(marine::SdrSourceConfig)
Q_DECLARE_METATYPE(marine::SdrChannelStats)
Q_DECLARE_METATYPE(marine::SdrStreamStats)
Q_DECLARE_METATYPE(marine::IqBlock)
