#pragma once

#include "SdrSource.h"

#include <gnuradio/sync_block.h>

#include <QtGlobal>

#include <complex>
#include <functional>
#include <memory>
#include <vector>

namespace zapiska {

class IqFftSink final : public gr::sync_block
{
public:
    using sptr = std::shared_ptr<IqFftSink>;
    using Callback = std::function<void(const SdrSpectrumFrame &frame)>;

    static sptr make(int fftSize,
        quint64 reportIntervalSamples,
        qint64 centerFrequencyHz,
        int sampleRateHz,
        Callback callback);

private:
    IqFftSink(int fftSize,
        quint64 reportIntervalSamples,
        qint64 centerFrequencyHz,
        int sampleRateHz,
        Callback callback);

    int work(int noutputItems,
        gr_vector_const_void_star &inputItems,
        gr_vector_void_star &outputItems) override;

    void publishFrame();

    int fftSize = 1024;
    quint64 reportIntervalSamples = 1;
    qint64 centerFrequencyHz = 0;
    int sampleRateHz = 0;
    quint64 totalSamples = 0;
    quint64 nextReportSample = 0;
    int writeIndex = 0;
    int filledSamples = 0;
    std::vector<std::complex<float>> sampleRing;
    std::vector<float> window;
    Callback callback;
};

} // namespace zapiska
