#pragma once

#include "SdrSource.h"

#include <gnuradio/basic_block.h>
#include <gnuradio/top_block.h>

#include <functional>
#include <memory>

namespace marine {

struct ChannelReceiverConfig
{
    SdrChannelConfig channel;
    qint64 centerFrequencyHz = 156800000;
    int inputSampleRateHz = 2000000;
};

struct ChannelStatsUpdate
{
    SdrChannelStats stats;
};

class ChannelReceiver final
{
public:
    using sptr = std::shared_ptr<ChannelReceiver>;
    using Callback = std::function<void(const ChannelStatsUpdate &update)>;

    static sptr make(const ChannelReceiverConfig &config, Callback callback);

    void connectInput(const gr::top_block_sptr &topBlock, const gr::basic_block_sptr &source);
    void connectAudioOutput(const gr::top_block_sptr &topBlock,
        const gr::basic_block_sptr &destination,
        int destinationPort);
    SdrChannelStats initialStats() const;

private:
    struct Impl;

    explicit ChannelReceiver(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl;
};

} // namespace marine
