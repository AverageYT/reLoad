#pragma once

#include <vector>

namespace reload::analysis
{
struct SingleCycleOptions
{
    int cyclesToAverage = 4; // consecutive periods averaged to reduce noise
    bool removeDC = true;
    bool normalize = true;   // peak-normalise to 1
};

struct SingleCycleResult
{
    bool ok = false;
    std::vector<float> frame; // Wavetable::frameSize samples, one cycle
    int cyclesUsed = 0;
};

// Extracts one cycle of period `period` (fractional samples) starting at
// `start`, averaging up to options.cyclesToAverage periods.
//
// Steps: windowed-sinc resampling of each period onto a fixed grid; average;
// remove the linear drift between the cycle start and the next cycle's start
// (loop-point cleanup); FFT, keep harmonics 0..1023; rotate phase so the
// fundamental starts as a rising sine; inverse FFT to 2048 samples.
SingleCycleResult extractSingleCycle (const float* x, int numSamples, double start, double period,
                                      const SingleCycleOptions& options);
} // namespace reload::analysis
