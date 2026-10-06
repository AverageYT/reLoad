#include "analysis/SingleCycle.h"

#include "analysis/SincInterpolator.h"
#include "dsp/Wavetable.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <complex>

namespace reload::analysis
{
namespace
{
    const SincInterpolator& sinc()
    {
        static const SincInterpolator instance;
        return instance;
    }
} // namespace

SingleCycleResult extractSingleCycle (const float* x, int numSamples, double start, double period,
                                      const SingleCycleOptions& options)
{
    constexpr int outSize = dsp::Wavetable::frameSize;
    constexpr int maxHarmonic = outSize / 2 - 1;

    SingleCycleResult result;
    if (period < 2.0 || start < 0.0)
        return result;

    // Need cycles + 1 periods (the extra one provides the loop continuation sample).
    const int available = static_cast<int> ((numSamples - SincInterpolator::halfWidth - start) / period) - 1;
    const int cycles = std::min (std::max (options.cyclesToAverage, 1), available);
    if (cycles < 1)
        return result;

    // Sample each period on a grid at least as dense as the source so
    // resampling never aliases; band-limit afterwards.
    int gridOrder = 11;
    while ((1 << gridOrder) < period && gridOrder < 15)
        ++gridOrder;
    const int grid = 1 << gridOrder;

    std::vector<float> cycle (static_cast<size_t> (grid + 1), 0.0f);
    const auto& interp = sinc();
    for (int c = 0; c < cycles; ++c)
    {
        const double cycleStart = start + c * period;
        for (int k = 0; k <= grid; ++k)
            cycle[static_cast<size_t> (k)] += interp.read (x, numSamples, cycleStart + k * period / grid);
    }
    for (auto& v : cycle)
        v /= static_cast<float> (cycles);

    // Loop-point cleanup: remove the drift so sample `grid` (next cycle's
    // start) would equal sample 0.
    const float drift = cycle[static_cast<size_t> (grid)] - cycle[0];
    for (int k = 0; k < grid; ++k)
        cycle[static_cast<size_t> (k)] -= drift * static_cast<float> (k) / static_cast<float> (grid);

    juce::dsp::FFT fftIn (gridOrder);
    std::vector<float> spec (static_cast<size_t> (2 * grid), 0.0f);
    std::copy_n (cycle.begin(), grid, spec.begin());
    fftIn.performRealOnlyForwardTransform (spec.data(), true);

    auto bin = [&] (int h) { return std::complex<float> (spec[static_cast<size_t> (2 * h)], spec[static_cast<size_t> (2 * h + 1)]); };

    // Phase-align: rotate so harmonic 1 has the phase of a rising sine.
    // Skipped when the fundamental is too weak to give a stable phase.
    float maxMag = 0.0f;
    for (int h = 1; h <= maxHarmonic; ++h)
        maxMag = std::max (maxMag, std::abs (bin (h)));
    const auto fundamental = bin (1);
    const bool align = maxMag > 0.0f && std::abs (fundamental) > 0.05f * maxMag;
    const float rotation = align ? -juce::MathConstants<float>::halfPi - std::arg (fundamental) : 0.0f;

    // Copy harmonics 0..1023 into a 2048-point spectrum (rescaled for the size change).
    juce::dsp::FFT fftOut (11);
    std::vector<float> out (static_cast<size_t> (2 * outSize), 0.0f);
    const float scale = static_cast<float> (outSize) / static_cast<float> (grid);
    for (int h = options.removeDC ? 1 : 0; h <= maxHarmonic; ++h)
    {
        const auto v = bin (h) * std::polar (scale, rotation * static_cast<float> (h));
        out[static_cast<size_t> (2 * h)] = v.real();
        out[static_cast<size_t> (2 * h + 1)] = h == 0 ? 0.0f : v.imag();
    }
    fftOut.performRealOnlyInverseTransform (out.data());

    result.frame.assign (out.begin(), out.begin() + outSize);

    if (options.normalize)
    {
        float peak = 0.0f;
        for (auto v : result.frame)
            peak = std::max (peak, std::abs (v));
        if (peak > 1.0e-9f)
            for (auto& v : result.frame)
                v /= peak;
    }

    result.cyclesUsed = cycles;
    result.ok = true;
    return result;
}
} // namespace reload::analysis
