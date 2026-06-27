#include "GrOsmoSdrSource.h"

#include "ChannelReceiver.h"
#include "IqPowerSink.h"

#include <gnuradio/audio/sink.h>
#include <gnuradio/blocks/add_blk.h>
#include <gnuradio/blocks/multiply_const.h>
#include <gnuradio/top_block.h>
#include <osmosdr/device.h>
#include <osmosdr/source.h>

#include <algorithm>
#include <exception>
#include <iterator>
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
    std::vector<QString> channelIds;
    std::vector<gr::blocks::multiply_const_ff::sptr> audioGains;
    gr::blocks::add_ff::sptr audioMixer;
    gr::audio::sink::sptr audioSink;
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

    void refreshLiveAudioGains()
    {
        SdrStreamStats statsSnapshot;
        {
            std::lock_guard<std::mutex> lock(mutex);
            statsSnapshot = activeStats;
        }

        const bool liveAudioEnabled = statsSnapshot.liveAudioEnabled;
        const float openChannelGain = !blocks.audioGains.empty()
            ? 1.0F / static_cast<float>(blocks.audioGains.size())
            : 0.0F;

        for (std::size_t index = 0; index < blocks.audioGains.size(); ++index) {
            bool channelOpen = false;
            if (index < blocks.channelIds.size()) {
                const QString &channelId = blocks.channelIds.at(index);
                for (const auto &channelStats : statsSnapshot.channelStats) {
                    if (channelStats.id == channelId) {
                        channelOpen = channelStats.hasSquelch && channelStats.squelchOpen;
                        break;
                    }
                }
            }

            blocks.audioGains.at(index)->set_k(liveAudioEnabled && channelOpen ? openChannelGain : 0.0F);
        }
    }

    void updateChannelStats(const ChannelStatsUpdate &update)
    {
        SdrStreamStats statsSnapshot;
        {
            std::lock_guard<std::mutex> lock(mutex);
            bool updated = false;
            for (auto &channelStats : activeStats.channelStats) {
                if (channelStats.id == update.stats.id) {
                    channelStats.name = update.stats.name;
                    channelStats.frequencyHz = update.stats.frequencyHz;
                    channelStats.offsetHz = update.stats.offsetHz;
                    channelStats.bandwidthHz = update.stats.bandwidthHz;
                    channelStats.sampleRateHz = update.stats.sampleRateHz;
                    channelStats.audioSampleRateHz = update.stats.audioSampleRateHz;
                    channelStats.squelchThresholdDbfs = update.stats.squelchThresholdDbfs;
                    channelStats.squelchMode = update.stats.squelchMode;
                    if (update.stats.hasPower) {
                        channelStats.samplesRead = update.stats.samplesRead;
                        channelStats.hasPower = true;
                        channelStats.powerDbfs = update.stats.powerDbfs;
                    }
                    if (update.stats.hasAudioLevel) {
                        channelStats.audioSamplesRead = update.stats.audioSamplesRead;
                        channelStats.hasAudioLevel = true;
                        channelStats.audioLevelDbfs = update.stats.audioLevelDbfs;
                    }
                    if (update.stats.hasSquelch) {
                        channelStats.hasSquelch = true;
                        channelStats.squelchOpen = update.stats.squelchOpen;
                        channelStats.squelchThresholdDbfs = update.stats.squelchThresholdDbfs;
                    }
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
        refreshLiveAudioGains();
    }

    void emitStatsSnapshot()
    {
        SdrStreamStats statsSnapshot;
        {
            std::lock_guard<std::mutex> lock(mutex);
            statsSnapshot = activeStats;
        }
        emit owner->statsUpdated(statsSnapshot);
    }

    bool ensureLiveAudioBranch(QString *errorMessage)
    {
        if (!blocks.topBlock) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("GNU Radio gr-osmosdr source is not open");
            }
            return false;
        }
        if (blocks.channelReceivers.empty()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("No demodulated channels are available for live audio");
            }
            return false;
        }
        if (blocks.audioSink) {
            return true;
        }

        try {
            const auto firstStats = blocks.channelReceivers.front()->initialStats();
            const int audioSampleRateHz = std::max(firstStats.audioSampleRateHz, 1);
            blocks.audioMixer = gr::blocks::add_ff::make();
            blocks.audioSink = gr::audio::sink::make(audioSampleRateHz);
            blocks.audioGains.clear();
            blocks.audioGains.reserve(blocks.channelReceivers.size());

            const bool wasRunning = stats().running;
            if (wasRunning) {
                blocks.topBlock->lock();
            }
            try {
                for (std::size_t index = 0; index < blocks.channelReceivers.size(); ++index) {
                    auto gainBlock = gr::blocks::multiply_const_ff::make(0.0F);
                    blocks.channelReceivers.at(index)->connectAudioOutput(blocks.topBlock, gainBlock, 0);
                    blocks.topBlock->connect(gainBlock, 0, blocks.audioMixer, static_cast<int>(index));
                    blocks.audioGains.push_back(std::move(gainBlock));
                }
                blocks.topBlock->connect(blocks.audioMixer, 0, blocks.audioSink, 0);
            } catch (...) {
                if (wasRunning) {
                    blocks.topBlock->unlock();
                }
                throw;
            }
            if (wasRunning) {
                blocks.topBlock->unlock();
            }

            refreshLiveAudioGains();
            return true;
        } catch (const std::exception &error) {
            if (errorMessage) {
                *errorMessage = errorText(error);
            }
            return false;
        }
    }

    bool setLiveAudioEnabled(bool enabled, QString *errorMessage)
    {
        if (errorMessage) {
            errorMessage->clear();
        }

        if (enabled && !ensureLiveAudioBranch(errorMessage)) {
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            activeStats.liveAudioEnabled = enabled;
        }
        refreshLiveAudioGains();
        emitStatsSnapshot();
        return true;
    }

    bool setChannelSquelch(const QString &channelId,
        SdrSquelchMode mode,
        double thresholdDbfs,
        QString *errorMessage)
    {
        if (errorMessage) {
            errorMessage->clear();
        }

        const auto receiverIt = std::find(blocks.channelIds.begin(), blocks.channelIds.end(), channelId);
        if (receiverIt == blocks.channelIds.end()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Channel %1 is not active").arg(channelId);
            }
            return false;
        }

        const auto receiverIndex = static_cast<std::size_t>(
            std::distance(blocks.channelIds.begin(), receiverIt));
        if (receiverIndex >= blocks.channelReceivers.size()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Channel %1 receiver is unavailable").arg(channelId);
            }
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            for (auto &channel : activeConfig.channels) {
                if (channel.id == channelId) {
                    channel.squelchMode = mode;
                    channel.squelchThresholdDbfs = thresholdDbfs;
                    break;
                }
            }
        }

        blocks.channelReceivers.at(receiverIndex)->setSquelch(mode, thresholdDbfs);
        refreshLiveAudioGains();
        return true;
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
                [sourceImpl = impl.get()](const ChannelStatsUpdate &update) {
                    sourceImpl->updateChannelStats(update);
                });
            channelReceiver->connectInput(blocks.topBlock, blocks.source);
            initialChannelStats.append(channelReceiver->initialStats());
            blocks.channelIds.push_back(channel.id);
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

bool GrOsmoSdrSource::setLiveAudioEnabled(bool enabled, QString *errorMessage)
{
    return impl->setLiveAudioEnabled(enabled, errorMessage);
}

bool GrOsmoSdrSource::liveAudioEnabled() const
{
    return stats().liveAudioEnabled;
}

bool GrOsmoSdrSource::setChannelSquelch(const QString &channelId,
    SdrSquelchMode mode,
    double thresholdDbfs,
    QString *errorMessage)
{
    return impl->setChannelSquelch(channelId, mode, thresholdDbfs, errorMessage);
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
