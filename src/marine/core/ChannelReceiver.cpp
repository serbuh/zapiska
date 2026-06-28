#include "ChannelReceiver.h"

#include "AudioLevelSink.h"
#include "IqPowerSink.h"

#include <gnuradio/analog/quadrature_demod_cf.h>
#include <gnuradio/filter/firdes.h>
#include <gnuradio/filter/freq_xlating_fir_filter.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace marine {

namespace {

constexpr int DefaultBandwidthHz = 10000;
constexpr int TargetChannelSampleRateHz = 50000;
constexpr int MinimumTransitionWidthHz = 2000;
constexpr int PowerReportsPerSecond = 20;
constexpr double NarrowFmDeviationHz = 5000.0;
constexpr double DefaultSquelchThresholdDbfs = -45.0;
constexpr double Pi = 3.14159265358979323846;

int normalizedBandwidth(int bandwidthHz)
{
    return std::max(bandwidthHz, DefaultBandwidthHz);
}

int channelDecimation(int inputSampleRateHz)
{
    if (inputSampleRateHz <= TargetChannelSampleRateHz) {
        return 1;
    }

    return std::max(1, static_cast<int>(std::lround(
        static_cast<double>(inputSampleRateHz) / static_cast<double>(TargetChannelSampleRateHz))));
}

std::vector<float> channelTaps(int inputSampleRateHz, int bandwidthHz)
{
    const double nyquistHz = static_cast<double>(inputSampleRateHz) / 2.0;
    double cutoffHz = static_cast<double>(normalizedBandwidth(bandwidthHz)) / 2.0;
    double transitionHz = std::max(static_cast<double>(MinimumTransitionWidthHz), cutoffHz);

    if (cutoffHz >= nyquistHz) {
        cutoffHz = nyquistHz * 0.5;
    }
    if (cutoffHz + transitionHz >= nyquistHz) {
        transitionHz = std::max(100.0, nyquistHz - cutoffHz);
    }

    return gr::filter::firdes::low_pass(
        1.0,
        static_cast<double>(inputSampleRateHz),
        cutoffHz,
        transitionHz);
}

quint64 powerReportIntervalSamples(int sampleRateHz)
{
    return static_cast<quint64>(std::max(sampleRateHz / PowerReportsPerSecond, 1));
}

float quadratureDemodGain(int sampleRateHz)
{
    return static_cast<float>(
        static_cast<double>(sampleRateHz) / (2.0 * Pi * NarrowFmDeviationHz));
}

SdrChannelConfig normalizedChannelConfig(const SdrChannelConfig &channel)
{
    SdrChannelConfig normalized = channel;
    if (normalized.id.isEmpty()) {
        normalized.id = QStringLiteral("16");
    }
    if (normalized.name.isEmpty()) {
        normalized.name = QStringLiteral("Marine Channel 16");
    }
    if (normalized.frequencyHz <= 0) {
        normalized.frequencyHz = MarineChannel16FrequencyHz;
    }
    normalized.bandwidthHz = normalizedBandwidth(normalized.bandwidthHz);
    if (normalized.squelchThresholdDbfs > 0.0) {
        normalized.squelchThresholdDbfs = DefaultSquelchThresholdDbfs;
    }
    return normalized;
}

bool squelchOpenFor(SdrSquelchMode mode, double levelDbfs, double thresholdDbfs)
{
    switch (mode) {
    case SdrSquelchMode::Automatic:
        return levelDbfs >= thresholdDbfs;
    case SdrSquelchMode::ForcedOpen:
        return true;
    case SdrSquelchMode::ForcedClosed:
        return false;
    }

    return false;
}

} // namespace

struct ChannelReceiver::Impl
{
    mutable std::mutex mutex;
    SdrChannelStats stats;
    gr::filter::freq_xlating_fir_filter_ccf::sptr translatingFilter;
    IqPowerSink::sptr powerSink;
    gr::analog::quadrature_demod_cf::sptr demodulator;
    AudioLevelSink::sptr audioLevelSink;
    Callback callback;

    Impl(const ChannelReceiverConfig &config, Callback callback)
        : callback(std::move(callback))
    {
        const SdrChannelConfig channel = normalizedChannelConfig(config.channel);
        const int inputSampleRateHz = std::max(config.inputSampleRateHz, 1);
        const qint64 offsetHz = channel.frequencyHz - config.centerFrequencyHz;
        const double halfBandwidthHz = static_cast<double>(channel.bandwidthHz) / 2.0;
        const double nyquistHz = static_cast<double>(inputSampleRateHz) / 2.0;
        if (std::abs(static_cast<double>(offsetHz)) + halfBandwidthHz >= nyquistHz) {
            throw std::runtime_error("channel is outside the configured SDR sample-rate span");
        }

        const int decimation = channelDecimation(inputSampleRateHz);
        const int outputSampleRateHz = std::max(inputSampleRateHz / decimation, 1);

        stats.id = channel.id;
        stats.name = channel.name;
        stats.frequencyHz = channel.frequencyHz;
        stats.offsetHz = offsetHz;
        stats.bandwidthHz = channel.bandwidthHz;
        stats.sampleRateHz = outputSampleRateHz;
        stats.audioSampleRateHz = outputSampleRateHz;
        stats.squelchThresholdDbfs = channel.squelchThresholdDbfs;
        stats.squelchMode = channel.squelchMode;
        stats.monitorEnabled = channel.monitorEnabled;
        if (stats.squelchMode != SdrSquelchMode::Automatic) {
            stats.hasSquelch = true;
            stats.squelchOpen = squelchOpenFor(stats.squelchMode, stats.audioLevelDbfs, stats.squelchThresholdDbfs);
        }

        translatingFilter = gr::filter::freq_xlating_fir_filter_ccf::make(
            decimation,
            channelTaps(inputSampleRateHz, channel.bandwidthHz),
            static_cast<double>(offsetHz),
            static_cast<double>(inputSampleRateHz));
        powerSink = IqPowerSink::make(
            powerReportIntervalSamples(outputSampleRateHz),
            [this](const IqPowerUpdate &update) {
                SdrChannelStats updateStats;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    stats.samplesRead = update.samplesRead;
                    stats.hasPower = true;
                    stats.powerDbfs = update.powerDbfs;
                    updateStats = stats;
                }
                if (this->callback) {
                    this->callback(ChannelStatsUpdate { updateStats });
                }
            });
        demodulator = gr::analog::quadrature_demod_cf::make(
            quadratureDemodGain(outputSampleRateHz));
        audioLevelSink = AudioLevelSink::make(
            powerReportIntervalSamples(outputSampleRateHz),
            [this](const AudioLevelUpdate &update) {
                SdrChannelStats updateStats;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    stats.audioSamplesRead = update.samplesRead;
                    stats.hasAudioLevel = true;
                    stats.audioLevelDbfs = update.levelDbfs;
                    stats.hasSquelch = true;
                    stats.squelchOpen = squelchOpenFor(
                        stats.squelchMode,
                        update.levelDbfs,
                        stats.squelchThresholdDbfs);
                    updateStats = stats;
                }
                if (this->callback) {
                    this->callback(ChannelStatsUpdate { updateStats });
                }
            });
    }
};

ChannelReceiver::sptr ChannelReceiver::make(const ChannelReceiverConfig &config, Callback callback)
{
    return sptr(new ChannelReceiver(std::make_unique<Impl>(config, std::move(callback))));
}

ChannelReceiver::ChannelReceiver(std::unique_ptr<Impl> impl)
    : impl(std::move(impl))
{
}

void ChannelReceiver::connectInput(const gr::top_block_sptr &topBlock, const gr::basic_block_sptr &source)
{
    topBlock->connect(source, 0, impl->translatingFilter, 0);
    topBlock->connect(impl->translatingFilter, 0, impl->powerSink, 0);
    topBlock->connect(impl->translatingFilter, 0, impl->demodulator, 0);
    topBlock->connect(impl->demodulator, 0, impl->audioLevelSink, 0);
}

void ChannelReceiver::connectAudioOutput(const gr::top_block_sptr &topBlock,
    const gr::basic_block_sptr &destination,
    int destinationPort)
{
    topBlock->connect(impl->demodulator, 0, destination, destinationPort);
}

void ChannelReceiver::disconnectAudioOutput(const gr::top_block_sptr &topBlock,
    const gr::basic_block_sptr &destination,
    int destinationPort)
{
    topBlock->disconnect(impl->demodulator, 0, destination, destinationPort);
}

void ChannelReceiver::setSquelch(SdrSquelchMode mode, double thresholdDbfs)
{
    SdrChannelStats updateStats;
    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        impl->stats.squelchMode = mode;
        impl->stats.squelchThresholdDbfs = thresholdDbfs > 0.0
            ? DefaultSquelchThresholdDbfs
            : thresholdDbfs;
        if (impl->stats.squelchMode != SdrSquelchMode::Automatic || impl->stats.hasAudioLevel) {
            impl->stats.hasSquelch = true;
            impl->stats.squelchOpen = squelchOpenFor(
                impl->stats.squelchMode,
                impl->stats.audioLevelDbfs,
                impl->stats.squelchThresholdDbfs);
        } else {
            impl->stats.hasSquelch = false;
            impl->stats.squelchOpen = false;
        }
        updateStats = impl->stats;
    }

    if (impl->callback) {
        impl->callback(ChannelStatsUpdate { updateStats });
    }
}

SdrChannelStats ChannelReceiver::initialStats() const
{
    std::lock_guard<std::mutex> lock(impl->mutex);
    return impl->stats;
}

} // namespace marine
