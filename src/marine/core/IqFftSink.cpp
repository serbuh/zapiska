#include "IqFftSink.h"

#include <gnuradio/gr_complex.h>
#include <gnuradio/io_signature.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace marine {

namespace {

constexpr double Pi = 3.14159265358979323846;
constexpr double MinimumPower = 1.0e-20;

int normalizedFftSize(int requestedSize)
{
    int size = 1;
    while (size < requestedSize) {
        size <<= 1;
    }
    return std::max(size, 16);
}

void fftInPlace(std::vector<std::complex<float>> &values)
{
    const int size = static_cast<int>(values.size());
    int reverseIndex = 0;
    for (int index = 1; index < size; ++index) {
        int bit = size >> 1;
        for (; reverseIndex & bit; bit >>= 1) {
            reverseIndex ^= bit;
        }
        reverseIndex ^= bit;

        if (index < reverseIndex) {
            std::swap(values[index], values[reverseIndex]);
        }
    }

    for (int length = 2; length <= size; length <<= 1) {
        const double angle = -2.0 * Pi / static_cast<double>(length);
        const std::complex<float> phaseStep(
            static_cast<float>(std::cos(angle)),
            static_cast<float>(std::sin(angle)));

        for (int start = 0; start < size; start += length) {
            std::complex<float> phase(1.0F, 0.0F);
            const int halfLength = length / 2;
            for (int offset = 0; offset < halfLength; ++offset) {
                const auto even = values[start + offset];
                const auto odd = values[start + offset + halfLength] * phase;
                values[start + offset] = even + odd;
                values[start + offset + halfLength] = even - odd;
                phase *= phaseStep;
            }
        }
    }
}

} // namespace

IqFftSink::sptr IqFftSink::make(int fftSize,
    quint64 reportIntervalSamples,
    qint64 centerFrequencyHz,
    int sampleRateHz,
    Callback callback)
{
    return sptr(new IqFftSink(
        normalizedFftSize(fftSize),
        std::max<quint64>(1, reportIntervalSamples),
        centerFrequencyHz,
        sampleRateHz,
        std::move(callback)));
}

IqFftSink::IqFftSink(int fftSize,
    quint64 reportIntervalSamples,
    qint64 centerFrequencyHz,
    int sampleRateHz,
    Callback callback)
    : gr::sync_block("zapiska_iq_fft_sink",
        gr::io_signature::make(1, 1, sizeof(gr_complex)),
        gr::io_signature::make(0, 0, 0))
    , fftSize(fftSize)
    , reportIntervalSamples(reportIntervalSamples)
    , centerFrequencyHz(centerFrequencyHz)
    , sampleRateHz(sampleRateHz)
    , nextReportSample(reportIntervalSamples)
    , sampleRing(static_cast<std::size_t>(fftSize))
    , window(static_cast<std::size_t>(fftSize))
    , callback(std::move(callback))
{
    for (int index = 0; index < fftSize; ++index) {
        window[static_cast<std::size_t>(index)] = static_cast<float>(
            0.5 - 0.5 * std::cos((2.0 * Pi * static_cast<double>(index))
                / static_cast<double>(std::max(fftSize - 1, 1))));
    }
}

int IqFftSink::work(int noutputItems,
    gr_vector_const_void_star &inputItems,
    gr_vector_void_star &outputItems)
{
    (void)outputItems;

    if (noutputItems <= 0) {
        return noutputItems;
    }

    const auto *samples = static_cast<const gr_complex *>(inputItems[0]);
    for (int index = 0; index < noutputItems; ++index) {
        sampleRing[static_cast<std::size_t>(writeIndex)] = samples[index];
        writeIndex = (writeIndex + 1) % fftSize;
        filledSamples = std::min(filledSamples + 1, fftSize);
        ++totalSamples;

        if (filledSamples == fftSize && totalSamples >= nextReportSample) {
            publishFrame();
            nextReportSample = totalSamples + reportIntervalSamples;
        }
    }

    return noutputItems;
}

void IqFftSink::publishFrame()
{
    if (!callback) {
        return;
    }

    std::vector<std::complex<float>> fftInput(static_cast<std::size_t>(fftSize));
    for (int index = 0; index < fftSize; ++index) {
        const int ringIndex = (writeIndex + index) % fftSize;
        fftInput[static_cast<std::size_t>(index)] =
            sampleRing[static_cast<std::size_t>(ringIndex)] * window[static_cast<std::size_t>(index)];
    }

    fftInPlace(fftInput);

    SdrSpectrumFrame frame;
    frame.centerFrequencyHz = centerFrequencyHz;
    frame.sampleRateHz = sampleRateHz;
    frame.samplesRead = totalSamples;
    frame.powerDbfs.resize(fftSize);

    const double scale = 1.0 / static_cast<double>(fftSize * fftSize);
    const int halfSize = fftSize / 2;
    for (int bin = 0; bin < fftSize; ++bin) {
        const int sourceIndex = (bin + halfSize) % fftSize;
        const double power = std::norm(fftInput[static_cast<std::size_t>(sourceIndex)]) * scale;
        frame.powerDbfs[bin] = static_cast<float>(
            10.0 * std::log10(std::max(power, MinimumPower)));
    }

    callback(frame);
}

} // namespace marine
