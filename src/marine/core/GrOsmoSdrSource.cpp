#include "GrOsmoSdrSource.h"

#include "ChannelReceiver.h"
#include "IqPowerSink.h"

#include <gnuradio/top_block.h>
#include <osmosdr/device.h>
#include <osmosdr/source.h>

#include <algorithm>
#include <exception>
#include <mutex>
#include <string>
#include <vector>

namespace marine {

namespace {

constexpr const char *DefaultDeviceArgs = "hackrf=0";
constexpr int PowerReportsPerSecond = 20;

QString valueOrEmpty(const osmosdr::device_t &device, const std::string &key)
{
    const auto it = device.find(key);
    if (it == device.end()) {
        return {};
    }

    return QString::fromStdString(it->second);
}

SdrDeviceInfo deviceInfoFromOsmosdr(const osmosdr::device_t &device)
{
    SdrDeviceInfo info;
    info.driver = valueOrEmpty(device, "driver");
    info.serial = valueOrEmpty(device, "serial");
    info.deviceArgs = QString::fromStdString(device.to_string());

    info.displayName = valueOrEmpty(device, "label");
    if (info.displayName.isEmpty()) {
        info.displayName = valueOrEmpty(device, "name");
    }
    if (info.displayName.isEmpty()) {
        info.displayName = valueOrEmpty(device, "type");
    }
    if (info.displayName.isEmpty()) {
        info.displayName = QString::fromStdString(device.to_pp_string());
    }
    if (info.displayName.isEmpty()) {
        info.displayName = info.deviceArgs;
    }

    return info;
}

QString errorText(const std::exception &error)
{
    return QString::fromUtf8(error.what());
}

quint64 powerReportIntervalSamples(int sampleRateHz)
{
    return static_cast<quint64>(std::max(sampleRateHz / PowerReportsPerSecond, 1));
}

QVector<SdrChannelConfig> defaultChannelConfigs()
{
    SdrChannelConfig channel;
    channel.id = QStringLiteral("16");
    channel.name = QStringLiteral("Marine Channel 16");
    channel.frequencyHz = 156800000;
    channel.bandwidthHz = 10000;
    channel.enabled = true;
    return { channel };
}

QVector<SdrChannelConfig> enabledChannelConfigs(const SdrSourceConfig &config)
{
    const auto configuredChannels = config.channels.isEmpty()
        ? defaultChannelConfigs()
        : config.channels;

    QVector<SdrChannelConfig> enabledChannels;
    enabledChannels.reserve(configuredChannels.size());
    for (const auto &channel : configuredChannels) {
        if (channel.enabled) {
            enabledChannels.append(channel);
        }
    }

    if (enabledChannels.isEmpty()) {
        return defaultChannelConfigs();
    }

    return enabledChannels;
}

} // namespace

struct GrOsMoBlocks
{
    gr::top_block_sptr topBlock;
    osmosdr::source::sptr source;
    IqPowerSink::sptr powerSink;
    std::vector<ChannelReceiver::sptr> channelReceivers;
};

struct GrOsmoSdrSource::Impl
{
    GrOsmoSdrSource *owner = nullptr;
    GrOsMoBlocks blocks;
    SdrSourceConfig activeConfig;
    SdrStreamStats activeStats;
    SdrSourceState activeState = SdrSourceState::Closed;
    mutable std::mutex mutex;

    explicit Impl(GrOsmoSdrSource *source)
        : owner(source)
    {
    }

    ~Impl()
    {
        stop();
        close();
    }

    void setState(SdrSourceState newState)
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            activeState = newState;
        }
        emit owner->stateChanged(newState);
    }

    void setError(const QString &message)
    {
        SdrStreamStats statsSnapshot;
        {
            std::lock_guard<std::mutex> lock(mutex);
            activeStats.running = false;
            activeStats.lastError = message;
            activeState = SdrSourceState::Error;
            statsSnapshot = activeStats;
        }
        emit owner->statsUpdated(statsSnapshot);
        emit owner->stateChanged(SdrSourceState::Error);
        emit owner->errorOccurred(message);
    }

    void setStatsRunning(bool running)
    {
        SdrStreamStats statsSnapshot;
        {
            std::lock_guard<std::mutex> lock(mutex);
            activeStats.running = running;
            statsSnapshot = activeStats;
        }
        emit owner->statsUpdated(statsSnapshot);
    }

    void updatePower(const IqPowerUpdate &update)
    {
        SdrStreamStats statsSnapshot;
        {
            std::lock_guard<std::mutex> lock(mutex);
            activeStats.samplesRead = update.samplesRead;
            activeStats.hasWidebandPower = true;
            activeStats.widebandPowerDbfs = update.powerDbfs;
            statsSnapshot = activeStats;
        }
        emit owner->statsUpdated(statsSnapshot);
    }

    void updateChannelPower(const ChannelPowerUpdate &update)
    {
        SdrStreamStats statsSnapshot;
        {
            std::lock_guard<std::mutex> lock(mutex);
            bool updated = false;
            for (auto &channelStats : activeStats.channelStats) {
                if (channelStats.id == update.stats.id) {
                    channelStats = update.stats;
                    updated = true;
                    break;
                }
            }
            if (!updated) {
                activeStats.channelStats.append(update.stats);
            }
            statsSnapshot = activeStats;
        }
        emit owner->statsUpdated(statsSnapshot);
    }

    void stop()
    {
        bool hadRunningFlowgraph = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            hadRunningFlowgraph = activeStats.running;
        }

        if (blocks.topBlock && hadRunningFlowgraph) {
            try {
                blocks.topBlock->stop();
                blocks.topBlock->wait();
            } catch (const std::exception &error) {
                emit owner->errorOccurred(errorText(error));
            }
        }

        setStatsRunning(false);

        if (blocks.topBlock) {
            setState(SdrSourceState::Open);
        }
    }

    void close()
    {
        blocks = {};
        {
            std::lock_guard<std::mutex> lock(mutex);
            activeConfig = {};
            activeStats = {};
        }
        setState(SdrSourceState::Closed);
    }

    SdrStreamStats stats() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return activeStats;
    }
};

GrOsmoSdrSource::GrOsmoSdrSource(QObject *parent)
    : SdrSource(parent)
    , impl(std::make_unique<Impl>(this))
{
    registerSdrSourceMetaTypes();
}

GrOsmoSdrSource::~GrOsmoSdrSource() = default;

QString GrOsmoSdrSource::backendName() const
{
    return QStringLiteral("GNU Radio gr-osmosdr");
}

QVector<SdrDeviceInfo> GrOsmoSdrSource::discoverDevices(QString *errorMessage)
{
    if (errorMessage) {
        errorMessage->clear();
    }

    try {
        const auto devices = osmosdr::device::find();
        QVector<SdrDeviceInfo> result;
        result.reserve(static_cast<int>(devices.size()));

        for (const auto &device : devices) {
            result.append(deviceInfoFromOsmosdr(device));
        }

        return result;
    } catch (const std::exception &error) {
        if (errorMessage) {
            *errorMessage = errorText(error);
        }
        return {};
    }
}

bool GrOsmoSdrSource::open(const SdrSourceConfig &config, QString *errorMessage)
{
    if (errorMessage) {
        errorMessage->clear();
    }

    close();

    try {
        const QString deviceArgs = config.deviceArgs.isEmpty()
            ? QString::fromLatin1(DefaultDeviceArgs)
            : config.deviceArgs;

        GrOsMoBlocks blocks;
        blocks.topBlock = gr::make_top_block("zapiska-gr-osmosdr-source");
        blocks.source = osmosdr::source::make(deviceArgs.toStdString());

        blocks.source->set_sample_rate(config.sampleRateHz);
        blocks.source->set_center_freq(static_cast<double>(config.centerFrequencyHz));
        if (config.gainDb > 0.0) {
            blocks.source->set_gain(config.gainDb);
        }

        const int actualSampleRateHz = static_cast<int>(blocks.source->get_sample_rate());
        blocks.powerSink = IqPowerSink::make(
            powerReportIntervalSamples(actualSampleRateHz),
            [sourceImpl = impl.get()](const IqPowerUpdate &update) {
                sourceImpl->updatePower(update);
            });
        blocks.topBlock->connect(blocks.source, 0, blocks.powerSink, 0);

        QVector<SdrChannelStats> initialChannelStats;
        const auto channels = enabledChannelConfigs(config);
        initialChannelStats.reserve(channels.size());
        for (const auto &channel : channels) {
            ChannelReceiverConfig receiverConfig;
            receiverConfig.channel = channel;
            receiverConfig.centerFrequencyHz = static_cast<qint64>(blocks.source->get_center_freq());
            receiverConfig.inputSampleRateHz = actualSampleRateHz;

            auto channelReceiver = ChannelReceiver::make(
                receiverConfig,
                [sourceImpl = impl.get()](const ChannelPowerUpdate &update) {
                    sourceImpl->updateChannelPower(update);
                });
            channelReceiver->connectInput(blocks.topBlock, blocks.source);
            initialChannelStats.append(channelReceiver->initialStats());
            blocks.channelReceivers.push_back(std::move(channelReceiver));
        }

        {
            std::lock_guard<std::mutex> lock(impl->mutex);
            impl->blocks = blocks;
            impl->activeConfig = config;
            impl->activeConfig.deviceArgs = deviceArgs;
            impl->activeConfig.sampleRateHz = actualSampleRateHz;
            impl->activeConfig.centerFrequencyHz = static_cast<qint64>(blocks.source->get_center_freq());
            impl->activeConfig.channels = channels;
            impl->activeStats = {};
            impl->activeStats.channelStats = initialChannelStats;
        }

        impl->setState(SdrSourceState::Open);
        return true;
    } catch (const std::exception &error) {
        const QString message = errorText(error);
        if (errorMessage) {
            *errorMessage = message;
        }
        impl->setError(message);
        return false;
    }
}

void GrOsmoSdrSource::close()
{
    impl->stop();
    impl->close();
}

bool GrOsmoSdrSource::start(QString *errorMessage)
{
    if (errorMessage) {
        errorMessage->clear();
    }

    gr::top_block_sptr topBlock;
    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        topBlock = impl->blocks.topBlock;
        if (impl->activeStats.running) {
            return true;
        }
    }

    if (!topBlock) {
        const QString message = QStringLiteral("GNU Radio gr-osmosdr source is not open");
        if (errorMessage) {
            *errorMessage = message;
        }
        impl->setError(message);
        return false;
    }

    try {
        topBlock->start();
        impl->setStatsRunning(true);
        impl->setState(SdrSourceState::Streaming);
        return true;
    } catch (const std::exception &error) {
        const QString message = errorText(error);
        if (errorMessage) {
            *errorMessage = message;
        }
        impl->setError(message);
        return false;
    }
}

void GrOsmoSdrSource::stop()
{
    impl->stop();
}

SdrSourceState GrOsmoSdrSource::state() const
{
    std::lock_guard<std::mutex> lock(impl->mutex);
    return impl->activeState;
}

SdrSourceConfig GrOsmoSdrSource::config() const
{
    std::lock_guard<std::mutex> lock(impl->mutex);
    return impl->activeConfig;
}

SdrStreamStats GrOsmoSdrSource::stats() const
{
    return impl->stats();
}

} // namespace marine
