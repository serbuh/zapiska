#pragma once

#include <gnuradio/sync_block.h>

#include <QtGlobal>

#include <functional>
#include <memory>

namespace zapiska {

struct AudioLevelUpdate
{
    quint64 samplesRead = 0;
    double levelDbfs = -200.0;
};

class AudioLevelSink final : public gr::sync_block
{
public:
    using sptr = std::shared_ptr<AudioLevelSink>;
    using Callback = std::function<void(const AudioLevelUpdate &update)>;

    static sptr make(quint64 reportIntervalSamples, Callback callback);

private:
    AudioLevelSink(quint64 reportIntervalSamples, Callback callback);

    int work(int noutputItems,
        gr_vector_const_void_star &inputItems,
        gr_vector_void_star &outputItems) override;

    quint64 reportIntervalSamples = 1;
    quint64 totalSamples = 0;
    quint64 windowSamples = 0;
    double windowPowerSum = 0.0;
    Callback callback;
};

} // namespace zapiska
