#include "IqPowerSink.h"

#include <gnuradio/gr_complex.h>
#include <gnuradio/io_signature.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace zapiska {

namespace {

constexpr double MinimumPower = 1.0e-20;

} // namespace

IqPowerSink::sptr IqPowerSink::make(quint64 reportIntervalSamples, Callback callback)
{
    return sptr(new IqPowerSink(std::max<quint64>(1, reportIntervalSamples), std::move(callback)));
}

IqPowerSink::IqPowerSink(quint64 reportIntervalSamples, Callback callback)
    : gr::sync_block("zapiska_iq_power_sink",
        gr::io_signature::make(1, 1, sizeof(gr_complex)),
        gr::io_signature::make(0, 0, 0))
    , reportIntervalSamples(reportIntervalSamples)
    , callback(std::move(callback))
{
}

int IqPowerSink::work(int noutputItems,
    gr_vector_const_void_star &inputItems,
    gr_vector_void_star &outputItems)
{
    (void)outputItems;

    if (noutputItems <= 0) {
        return noutputItems;
    }

    const auto *samples = static_cast<const gr_complex *>(inputItems[0]);
    double powerSum = 0.0;
    for (int i = 0; i < noutputItems; ++i) {
        const double real = samples[i].real();
        const double imag = samples[i].imag();
        powerSum += real * real + imag * imag;
    }

    const auto sampleCount = static_cast<quint64>(noutputItems);
    totalSamples += sampleCount;
    windowSamples += sampleCount;
    windowPowerSum += powerSum;

    if (windowSamples >= reportIntervalSamples) {
        const double meanPower = windowPowerSum / static_cast<double>(windowSamples);
        IqPowerUpdate update;
        update.samplesRead = totalSamples;
        update.powerDbfs = 10.0 * std::log10(std::max(meanPower, MinimumPower));

        if (callback) {
            callback(update);
        }

        windowSamples = 0;
        windowPowerSum = 0.0;
    }

    return noutputItems;
}

} // namespace zapiska
