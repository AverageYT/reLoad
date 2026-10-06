#include "analysis/Yin.h"

#include <algorithm>
#include <cmath>

namespace reload::analysis
{
Yin::Yin (int windowSize_, int maxLag_)
    : windowSize (windowSize_), maxLag (maxLag_)
{
    const int order = static_cast<int> (std::ceil (std::log2 (windowSize + maxLag)));
    fftSize = 1 << order;
    fft = std::make_unique<juce::dsp::FFT> (order);
    a.resize (static_cast<size_t> (2 * fftSize));
    b.resize (static_cast<size_t> (2 * fftSize));
    energy.resize (static_cast<size_t> (windowSize + maxLag + 1));
    diff.resize (static_cast<size_t> (maxLag + 2));
    cmnd.resize (static_cast<size_t> (maxLag + 2));
}

YinEstimate Yin::analyse (const float* x, int minLag, int maxLagToSearch, double threshold)
{
    const int n = windowSize + maxLag;
    maxLagToSearch = std::min (maxLagToSearch, maxLag - 1);
    minLag = std::max (minLag, 2);
    if (maxLagToSearch <= minLag)
        return {};

    // Running energy: energy[i] = sum of x^2 over [0, i).
    energy[0] = 0.0;
    for (int i = 0; i < n; ++i)
        energy[static_cast<size_t> (i + 1)] = energy[static_cast<size_t> (i)] + static_cast<double> (x[i]) * x[i];

    // r(tau) = sum_{j<W} x[j] x[j+tau] via FFT cross-correlation.
    std::fill (a.begin(), a.end(), 0.0f);
    std::fill (b.begin(), b.end(), 0.0f);
    std::copy_n (x, windowSize, a.begin());
    std::copy_n (x, n, b.begin());
    fft->performRealOnlyForwardTransform (a.data(), true);
    fft->performRealOnlyForwardTransform (b.data(), true);
    for (int k = 0; k <= fftSize / 2; ++k)
    {
        const float ar = a[static_cast<size_t> (2 * k)], ai = a[static_cast<size_t> (2 * k + 1)];
        const float br = b[static_cast<size_t> (2 * k)], bi = b[static_cast<size_t> (2 * k + 1)];
        // conj(A) * B
        b[static_cast<size_t> (2 * k)] = ar * br + ai * bi;
        b[static_cast<size_t> (2 * k + 1)] = ar * bi - ai * br;
    }
    fft->performRealOnlyInverseTransform (b.data());

    // d(tau) = e(0..W) + e(tau..tau+W) - 2 r(tau)
    const double e0 = energy[static_cast<size_t> (windowSize)];
    diff[0] = 0.0;
    cmnd[0] = 1.0;
    double runningSum = 0.0;
    for (int tau = 1; tau <= maxLagToSearch + 1; ++tau)
    {
        const double eTau = energy[static_cast<size_t> (tau + windowSize)] - energy[static_cast<size_t> (tau)];
        const double d = std::max (0.0, e0 + eTau - 2.0 * static_cast<double> (b[static_cast<size_t> (tau)]));
        diff[static_cast<size_t> (tau)] = d;
        runningSum += d;
        cmnd[static_cast<size_t> (tau)] = runningSum > 0.0 ? d * tau / runningSum : 1.0;
    }

    // First dip below threshold, then walk to its local minimum.
    int best = -1;
    for (int tau = minLag; tau <= maxLagToSearch; ++tau)
    {
        if (cmnd[static_cast<size_t> (tau)] < threshold)
        {
            while (tau + 1 <= maxLagToSearch && cmnd[static_cast<size_t> (tau + 1)] < cmnd[static_cast<size_t> (tau)])
                ++tau;
            best = tau;
            break;
        }
    }

    YinEstimate result;
    result.voiced = best >= 0;
    if (best < 0)
    {
        best = minLag;
        for (int tau = minLag + 1; tau <= maxLagToSearch; ++tau)
            if (cmnd[static_cast<size_t> (tau)] < cmnd[static_cast<size_t> (best)])
                best = tau;
    }

    // Parabolic interpolation around the minimum.
    // Parabolic interpolation around the minimum. The interpolated minimum
    // value is reported as aperiodicity: at short periods the nearest integer
    // lag can sit well off the true period and overstate it.
    double offset = 0.0;
    double minimum = cmnd[static_cast<size_t> (best)];
    if (best > 1 && best < maxLagToSearch + 1)
    {
        const double y0 = cmnd[static_cast<size_t> (best - 1)];
        const double y1 = cmnd[static_cast<size_t> (best)];
        const double y2 = cmnd[static_cast<size_t> (best + 1)];
        const double denom = y0 - 2.0 * y1 + y2;
        if (std::abs (denom) > 1.0e-12)
        {
            offset = juce::jlimit (-0.5, 0.5, 0.5 * (y0 - y2) / denom);
            minimum = std::max (0.0, y1 - 0.25 * (y0 - y2) * offset);
        }
    }

    result.period = best + offset;
    result.aperiodicity = minimum;
    return result;
}

double refinePeriod (const float* x, int numSamples, double approxPeriod, int maxMultiple)
{
    if (approxPeriod < 2.0)
        return approxPeriod;

    // Integration window: at least two periods, at least 1024 samples.
    const int window = std::max (1024, static_cast<int> (std::ceil (2.0 * approxPeriod)));

    auto d = [&] (int tau) {
        double sum = 0.0;
        for (int j = 0; j < window; ++j)
        {
            const double delta = static_cast<double> (x[j]) - x[j + tau];
            sum += delta * delta;
        }
        return sum;
    };

    const int multiple = std::min (maxMultiple, static_cast<int> ((numSamples - window - 3) / approxPeriod));
    if (multiple < 1)
        return approxPeriod;

    int tau = static_cast<int> (std::lround (multiple * approxPeriod));
    double dm = d (tau - 1), d0 = d (tau), dp = d (tau + 1);

    // Walk to the local minimum (the estimate can be off by a sample or two).
    for (int step = 0; step < 4; ++step)
    {
        if (dm < d0 && tau - 2 >= 1)
        {
            --tau; dp = d0; d0 = dm; dm = d (tau - 1);
        }
        else if (dp < d0 && tau + 2 + window <= numSamples)
        {
            ++tau; dm = d0; d0 = dp; dp = d (tau + 1);
        }
        else
            break;
    }

    const double denom = dm - 2.0 * d0 + dp;
    const double offset = std::abs (denom) > 1.0e-12 ? juce::jlimit (-0.5, 0.5, 0.5 * (dm - dp) / denom) : 0.0;
    const double refined = (tau + offset) / multiple;

    // Reject if refinement wandered more than a few percent (e.g. a different minimum).
    return std::abs (refined - approxPeriod) < 0.03 * approxPeriod ? refined : approxPeriod;
}
} // namespace reload::analysis
