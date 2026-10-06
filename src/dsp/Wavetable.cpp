#include "dsp/Wavetable.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>

namespace reload::dsp
{
namespace
{
    constexpr int fftOrder = 11;
    static_assert ((1 << fftOrder) == Wavetable::frameSize);
} // namespace

std::shared_ptr<const Wavetable> Wavetable::create (std::vector<float> frames, WavetableInfo info)
{
    const int numFrames = juce::jlimit (1, maxFrames, static_cast<int> (frames.size()) / frameSize);
    frames.resize (static_cast<size_t> (numFrames) * frameSize, 0.0f);

    std::shared_ptr<Wavetable> table (new Wavetable());
    table->numFrames = numFrames;
    table->info = std::move (info);
    table->mips.resize (static_cast<size_t> (numFrames) * numLevels * levelStride);

    juce::dsp::FFT fft (fftOrder);
    std::vector<float> spectrum (2 * frameSize), work (2 * frameSize);

    for (int f = 0; f < numFrames; ++f)
    {
        std::fill (spectrum.begin(), spectrum.end(), 0.0f);
        std::copy_n (frames.data() + static_cast<size_t> (f) * frameSize, frameSize, spectrum.begin());
        fft.performRealOnlyForwardTransform (spectrum.data(), true);

        for (int level = 0; level < numLevels; ++level)
        {
            work = spectrum;
            // Bins are interleaved (re, im). Keep harmonics 1..maxHarmonic(level)
            // and DC; zero everything above, including the Nyquist bin.
            for (int bin = maxHarmonic (level) + 1; bin <= frameSize / 2; ++bin)
                work[static_cast<size_t> (2 * bin)] = work[static_cast<size_t> (2 * bin + 1)] = 0.0f;
            std::fill (work.begin() + frameSize + 2, work.end(), 0.0f);

            fft.performRealOnlyInverseTransform (work.data());

            auto* dest = table->mips.data() + (static_cast<size_t> (f) * numLevels + static_cast<size_t> (level)) * levelStride + guardBefore;
            std::copy_n (work.begin(), frameSize, dest);
            dest[-1] = dest[frameSize - 1];
            dest[frameSize] = dest[0];
            dest[frameSize + 1] = dest[1];
        }
    }

    table->frames = std::move (frames);
    return table;
}

std::shared_ptr<const Wavetable> Wavetable::createSine()
{
    std::vector<float> frame (frameSize);
    for (int i = 0; i < frameSize; ++i)
        frame[static_cast<size_t> (i)] = static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * i / frameSize));

    WavetableInfo info;
    info.name = "Sine";
    info.mode = "Default";
    info.reason = "No sample loaded";
    return create (std::move (frame), std::move (info));
}

int Wavetable::chooseLevel (double frequency, double sampleRate) noexcept
{
    const double limit = sampleRate < 40000.0 ? 0.5 * sampleRate
                                              : std::min (sampleRate - 20000.0, 0.55 * sampleRate);
    const double allowedHarmonics = limit / std::max (frequency, 1.0e-3);

    for (int level = 0; level < numLevels; ++level)
        if (maxHarmonic (level) <= allowedHarmonics)
            return level;
    return numLevels - 1;
}
} // namespace reload::dsp
