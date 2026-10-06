#pragma once

#include <array>
#include <cmath>
#include <vector>

namespace reload::analysis
{
// Blackman-windowed sinc interpolation for reading a signal at fractional
// positions (offline analysis, not the audio thread). Kernel values come from
// a precomputed table so per-sample cost is a dot product.
class SincInterpolator
{
public:
    static constexpr int halfWidth = 16;               // taps each side
    static constexpr int numTaps = 2 * halfWidth;
    static constexpr int phases = 1024;                // fractional resolution

    SincInterpolator()
    {
        table.resize (static_cast<size_t> ((phases + 1) * numTaps));
        for (int p = 0; p <= phases; ++p)
        {
            const double frac = static_cast<double> (p) / phases;
            for (int k = 0; k < numTaps; ++k)
            {
                const double t = (k - halfWidth + 1) - frac; // tap offset relative to read position
                const double sinc = std::abs (t) < 1.0e-12 ? 1.0 : std::sin (pi * t) / (pi * t);
                const double w = 0.42 + 0.5 * std::cos (pi * t / halfWidth) + 0.08 * std::cos (2.0 * pi * t / halfWidth);
                table[static_cast<size_t> (p * numTaps + k)] = static_cast<float> (sinc * w);
            }
        }
    }

    // Value of x at fractional index pos; samples outside [0, n) count as 0.
    float read (const float* x, int n, double pos) const noexcept
    {
        const double floorPos = std::floor (pos);
        const int i0 = static_cast<int> (floorPos);
        const double phase = (pos - floorPos) * phases;
        const int p = static_cast<int> (phase);
        const float mix = static_cast<float> (phase - p);

        const float* k0 = table.data() + static_cast<size_t> (p) * numTaps;
        const float* k1 = k0 + numTaps;
        const int first = i0 - halfWidth + 1;

        float sum = 0.0f;
        for (int k = 0; k < numTaps; ++k)
        {
            const int idx = first + k;
            if (idx < 0 || idx >= n)
                continue;
            const float kernel = k0[k] + mix * (k1[k] - k0[k]);
            sum += x[idx] * kernel;
        }
        return sum;
    }

private:
    static constexpr double pi = 3.14159265358979323846;
    std::vector<float> table;
};
} // namespace reload::analysis
