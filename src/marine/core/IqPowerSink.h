#pragma once

#include <gnuradio/sync_block.h>

#include <QtGlobal>

#include <functional>
#include <memory>

namespace marine {

struct IqPowerUpdate
{
    quint64 samplesRead = 0;
    double powerDbfs = -200.0;
};

class IqPowerSink final : public gr::sync_block
{
public:
    using sptr = std::shared_ptr<IqPowerSink>;
    using Callback = std::function<void(const IqPowerUpdate &update)>;

    static sptr make(quint64 reportIntervalSamples, Callback callback);

private:
    IqPowerSink(quint64 reportIntervalSamples, Callback callback);

    int work(int noutputItems,
        gr_vector_const_void_star &inputItems,
        gr_vector_void_star &outputItems) override;

    quint64 reportIntervalSamples = 1;
    quint64 totalSamples = 0;
    quint64 windowSamples = 0;
    double windowPowerSum = 0.0;
    Callback callback;
};

} // namespace marine
