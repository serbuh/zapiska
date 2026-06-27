#include "GrOsmoSdrSource.h"

#include <gnuradio/blocks/null_sink.h>
#include <gnuradio/gr_complex.h>
#include <gnuradio/top_block.h>
#include <osmosdr/device.h>
#include <osmosdr/source.h>

#include <algorithm>
#include <exception>
#include <mutex>
#include <string>

namespace marine {

namespace {

constexpr const char *DefaultDeviceArgs = "hackrf=0";

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

} // namespace

struct GrOsMoBlocks
{
    gr::top_block_sptr topBlock;
    osmosdr::source::sptr source;
    gr::blocks::null_sink::sptr nullSink;
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
        blocks.nullSink = gr::blocks::null_sink::make(sizeof(gr_complex));

        blocks.source->set_sample_rate(config.sampleRateHz);
        blocks.source->set_center_freq(static_cast<double>(config.centerFrequencyHz));
        if (config.gainDb > 0.0) {
            blocks.source->set_gain(config.gainDb);
        }

        blocks.topBlock->connect(blocks.source, 0, blocks.nullSink, 0);

        {
            std::lock_guard<std::mutex> lock(impl->mutex);
            impl->blocks = blocks;
            impl->activeConfig = config;
            impl->activeConfig.deviceArgs = deviceArgs;
            impl->activeConfig.sampleRateHz = static_cast<int>(blocks.source->get_sample_rate());
            impl->activeConfig.centerFrequencyHz = static_cast<qint64>(blocks.source->get_center_freq());
            impl->activeStats = {};
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

