#pragma once

#include <juce_dsp/juce_dsp.h>

#include <memory>
#include <vector>

namespace reload::analysis
{
struct YinEstimate
{
    double period = 0.0;       // samples (fractional)
    double aperiodicity = 1.0; // cumulative-mean-normalised difference at the period: 0 = perfectly periodic
    bool voiced = false;
};

// YIN pitch estimator (de Cheveigné & Kawahara 2002) with an FFT-computed
// difference function. One instance per thread; analyse() does not allocate.
class Yin
{
public:
    // windowSize: integration window W. maxLag: longest period considered.
    // analyse() reads windowSize + maxLag samples.
    Yin (int windowSize, int maxLag);

    int getWindowSize() const noexcept { return windowSize; }
    int getMaxLag() const noexcept { return maxLag; }
    int getSamplesNeeded() const noexcept { return windowSize + maxLag; }

    YinEstimate analyse (const float* x, int minLag, int maxLagToSearch, double threshold = 0.15);

private:
    int windowSize, maxLag, fftSize;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> a, b;
    std::vector<double> energy, diff, cmnd;
};

// Refines a period estimate by locating the difference-function minimum near
// a multiple of the period (error shrinks with the multiple). Returns the
// input estimate if refinement isn't possible.
double refinePeriod (const float* x, int numSamples, double approxPeriod, int maxMultiple = 16);
} // namespace reload::analysis
