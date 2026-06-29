#pragma once

#include <QObject>
#include <QString>
#include <QVector>

#include <complex>

namespace zapiska {

inline constexpr qint64 DefaultSdrCenterFrequencyHz = 158900000;
inline constexpr int DefaultSdrSampleRateHz = 8000000;
inline constexpr qint64 DefaultChannelFrequencyHz = 156800000;

enum class SdrSourceState
{
    Closed,
    Open,
    Streaming,
    Error,
};

enum class SdrSquelchMode
{
    Automatic,
    ForcedOpen,
    ForcedClosed,
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
    qint64 frequencyHz = DefaultChannelFrequencyHz;
    int bandwidthHz = 10000;
    double squelchThresholdDbfs = -150.0;
    SdrSquelchMode squelchMode = SdrSquelchMode::Automatic;
    bool monitorEnabled = true;
    bool enabled = true;
};

struct SdrSourceConfig
{
    QString deviceArgs;
    qint64 centerFrequencyHz = DefaultSdrCenterFrequencyHz;
    int sampleRateHz = DefaultSdrSampleRateHz;
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
    int audioSampleRateHz = 0;
    quint64 audioSamplesRead = 0;
    bool hasAudioLevel = false;
    double audioLevelDbfs = -200.0;
    bool hasSquelch = false;
    bool squelchOpen = false;
    double squelchThresholdDbfs = -150.0;
    SdrSquelchMode squelchMode = SdrSquelchMode::Automatic;
    bool monitorEnabled = true;
};

struct SdrStreamStats
{
    bool running = false;
    bool liveAudioEnabled = false;
    bool recording = false;
    QString recordingChannelId;
    QString recordingPath;
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

struct SdrSpectrumFrame
{
    qint64 centerFrequencyHz = 0;
    int sampleRateHz = 0;
    quint64 samplesRead = 0;
    QVector<float> powerDbfs;
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
    virtual bool setLiveAudioEnabled(bool enabled, QString *errorMessage = nullptr) = 0;
    virtual bool liveAudioEnabled() const = 0;
    virtual bool startRecording(const QString &channelId,
        const QString &filePath,
        QString *errorMessage = nullptr) = 0;
    virtual void stopRecording() = 0;
    virtual bool recording() const = 0;
    virtual bool setChannelSquelch(const QString &channelId,
        SdrSquelchMode mode,
        double thresholdDbfs,
        QString *errorMessage = nullptr) = 0;
    virtual bool setChannelMonitorEnabled(const QString &channelId,
        bool enabled,
        QString *errorMessage = nullptr) = 0;
    virtual SdrSourceState state() const = 0;
    virtual SdrSourceConfig config() const = 0;
    virtual SdrStreamStats stats() const = 0;

signals:
    void stateChanged(zapiska::SdrSourceState state);
    void iqBlockReady(const zapiska::IqBlock &block);
    void spectrumUpdated(const zapiska::SdrSpectrumFrame &frame);
    void statsUpdated(const zapiska::SdrStreamStats &stats);
    void errorOccurred(const QString &message);
};

void registerSdrSourceMetaTypes();

} // namespace zapiska

Q_DECLARE_METATYPE(zapiska::SdrSourceState)
Q_DECLARE_METATYPE(zapiska::SdrSquelchMode)
Q_DECLARE_METATYPE(zapiska::SdrDeviceInfo)
Q_DECLARE_METATYPE(zapiska::SdrChannelConfig)
Q_DECLARE_METATYPE(zapiska::SdrSourceConfig)
Q_DECLARE_METATYPE(zapiska::SdrChannelStats)
Q_DECLARE_METATYPE(zapiska::SdrStreamStats)
Q_DECLARE_METATYPE(zapiska::IqBlock)
Q_DECLARE_METATYPE(zapiska::SdrSpectrumFrame)
