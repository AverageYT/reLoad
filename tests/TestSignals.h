#pragma once

#include <juce_core/juce_core.h>

#include <cmath>
#include <vector>

// Synthetic test signals with exactly known pitch and harmonic content.
namespace reload::test
{
inline constexpr double twoPi = 6.283185307179586476925286766559;

struct SignalSpec
{
    double sampleRate = 48000.0;
    double seconds = 1.0;
    double decaySeconds = 0.0; // 0 = no decay; otherwise amplitude *= exp(-t / decay)
    double amplitude = 0.5;
};

// Sum of harmonics with amplitudes amp(h); harmonics above Nyquist (or
// maxHarmonics) are omitted, so the signal is band-limited.
template <typename AmpFn>
std::vector<float> harmonicSignal (double f0, const SignalSpec& spec, int maxHarmonics, AmpFn amp, double phase = 0.0)
{
    const int n = static_cast<int> (spec.seconds * spec.sampleRate);
    const int harmonics = std::min (maxHarmonics, static_cast<int> (0.5 * spec.sampleRate / f0 - 1e-9));
    std::vector<double> acc (static_cast<size_t> (n), 0.0);
    for (int h = 1; h <= harmonics; ++h)
    {
        const double a = amp (h);
        if (a == 0.0)
            continue;
        const double w = twoPi * h * f0 / spec.sampleRate;
        for (int i = 0; i < n; ++i)
            acc[static_cast<size_t> (i)] += a * std::sin (w * i + h * phase);
    }

    std::vector<float> out (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
    {
        const double env = spec.decaySeconds > 0.0 ? std::exp (-(i / spec.sampleRate) / spec.decaySeconds) : 1.0;
        out[static_cast<size_t> (i)] = static_cast<float> (spec.amplitude * env * acc[static_cast<size_t> (i)]);
    }
    return out;
}

inline std::vector<float> sine (double f0, const SignalSpec& spec = {})
{
    return harmonicSignal (f0, spec, 1, [] (int) { return 1.0; });
}

// Saw: harmonic h has amplitude 1/h.
inline std::vector<float> saw (double f0, const SignalSpec& spec = {}, int maxHarmonics = 100000)
{
    return harmonicSignal (f0, spec, maxHarmonics, [] (int h) { return 0.6 / h; });
}

// Square: odd harmonics, amplitude 1/h.
inline std::vector<float> square (double f0, const SignalSpec& spec = {}, int maxHarmonics = 100000)
{
    return harmonicSignal (f0, spec, maxHarmonics, [] (int h) { return (h % 2 == 1) ? 0.6 / h : 0.0; });
}

inline void addNoise (std::vector<float>& x, float rmsLevel, juce::int64 seed = 1234)
{
    juce::Random random (seed);
    // Uniform [-1, 1] has RMS 1/sqrt(3).
    const float scale = rmsLevel * std::sqrt (3.0f);
    for (auto& v : x)
        v += (random.nextFloat() * 2.0f - 1.0f) * scale;
}

inline void mixInto (std::vector<float>& dest, const std::vector<float>& src, float gain = 1.0f)
{
    dest.resize (std::max (dest.size(), src.size()), 0.0f);
    for (size_t i = 0; i < src.size(); ++i)
        dest[i] += src[i] * gain;
}

inline double centsBetween (double f, double reference)
{
    return 1200.0 * std::log2 (f / reference);
}

// Magnitude of harmonic h of a single-cycle frame (direct DFT bin h).
inline double harmonicMagnitude (const float* frame, int size, int h)
{
    double re = 0.0, im = 0.0;
    for (int i = 0; i < size; ++i)
    {
        const double a = twoPi * h * i / size;
        re += frame[i] * std::cos (a);
        im -= frame[i] * std::sin (a);
    }
    return 2.0 * std::sqrt (re * re + im * im) / size;
}
} // namespace reload::test
