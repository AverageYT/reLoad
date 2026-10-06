#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "dsp/SynthEngine.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace reload::test
{
struct TimedMidi
{
    int sample;
    juce::MidiMessage message;
};

struct StereoRender
{
    std::vector<float> left, right;
};

// Renders the engine offline in fixed-size blocks, delivering each MIDI event
// at its exact sample offset inside the block it falls in.
inline StereoRender renderEngine (dsp::SynthEngine& engine, int blockSize, int totalSamples, std::vector<TimedMidi> events)
{
    std::sort (events.begin(), events.end(), [] (const auto& a, const auto& b) { return a.sample < b.sample; });

    StereoRender out;
    out.left.resize (static_cast<size_t> (totalSamples));
    out.right.resize (static_cast<size_t> (totalSamples));

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    size_t nextEvent = 0;

    for (int start = 0; start < totalSamples; start += blockSize)
    {
        const int n = std::min (blockSize, totalSamples - start);
        buffer.setSize (2, n, false, false, true);
        midi.clear();

        while (nextEvent < events.size() && events[nextEvent].sample < start + n)
        {
            midi.addEvent (events[nextEvent].message, events[nextEvent].sample - start);
            ++nextEvent;
        }

        engine.process (buffer, midi);
        std::copy_n (buffer.getReadPointer (0), n, out.left.begin() + start);
        std::copy_n (buffer.getReadPointer (1), n, out.right.begin() + start);
    }
    return out;
}

// Frequency of a (near-)periodic signal from linearly interpolated rising zero
// crossings. Accurate to well under 0.01 Hz for clean tones.
inline double estimateFrequency (const float* x, int n, double sampleRate)
{
    double first = -1.0, last = -1.0;
    int crossings = 0;
    for (int i = 1; i < n; ++i)
    {
        if (x[i - 1] < 0.0f && x[i] >= 0.0f)
        {
            const double t = static_cast<double> (i - 1) + static_cast<double> (x[i - 1]) / static_cast<double> (x[i - 1] - x[i]);
            if (first < 0.0)
                first = t;
            last = t;
            ++crossings;
        }
    }
    if (crossings < 2)
        return 0.0;
    return (crossings - 1) * sampleRate / (last - first);
}

inline double rms (const float* x, int n)
{
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
        sum += static_cast<double> (x[i]) * x[i];
    return n > 0 ? std::sqrt (sum / n) : 0.0;
}

inline bool allFinite (const std::vector<float>& x)
{
    return std::all_of (x.begin(), x.end(), [] (float v) { return std::isfinite (v); });
}

inline double midiToHz (int note)
{
    return 440.0 * std::pow (2.0, (note - 69) / 12.0);
}
} // namespace reload::test
