#include "AudioLevelSink.h"

#include <gnuradio/io_signature.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace zapiska {

namespace {

constexpr double MinimumPower = 1.0e-20;

} // namespace

AudioLevelSink::sptr AudioLevelSink::make(quint64 reportIntervalSamples, Callback callback)
{
    return sptr(new AudioLevelSink(std::max<quint64>(1, reportIntervalSamples), std::move(callback)));
}

AudioLevelSink::AudioLevelSink(quint64 reportIntervalSamples, Callback callback)
    : gr::sync_block("zapiska_audio_level_sink",
        gr::io_signature::make(1, 1, sizeof(float)),
        gr::io_signature::make(0, 0, 0))
    , reportIntervalSamples(reportIntervalSamples)
    , callback(std::move(callback))
{
}

int AudioLevelSink::work(int noutputItems,
    gr_vector_const_void_star &inputItems,
    gr_vector_void_star &outputItems)
{
    (void)outputItems;

    if (noutputItems <= 0) {
        return noutputItems;
    }

    const auto *samples = static_cast<const float *>(inputItems[0]);
    double powerSum = 0.0;
    for (int i = 0; i < noutputItems; ++i) {
        const double sample = samples[i];
        powerSum += sample * sample;
    }

    const auto sampleCount = static_cast<quint64>(noutputItems);
    totalSamples += sampleCount;
    windowSamples += sampleCount;
    windowPowerSum += powerSum;

    if (windowSamples >= reportIntervalSamples) {
        const double meanPower = windowPowerSum / static_cast<double>(windowSamples);
        AudioLevelUpdate update;
        update.samplesRead = totalSamples;
        update.levelDbfs = 10.0 * std::log10(std::max(meanPower, MinimumPower));

        if (callback) {
            callback(update);
        }

        windowSamples = 0;
        windowPowerSum = 0.0;
    }

    return noutputItems;
}

} // namespace zapiska
