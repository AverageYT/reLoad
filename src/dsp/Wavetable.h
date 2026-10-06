#pragma once

#include <juce_core/juce_core.h>

#include <memory>
#include <vector>

namespace reload::dsp
{
struct WavetableInfo
{
    // Frequency the source had at rootNote. Playing rootNote with source
    // tuning on reproduces the source pitch exactly (including any cents offset).
    double rootFrequency = 440.0;
    int rootNote = 69;
    juce::String name;   // e.g. source file name
    juce::String mode;   // analysis mode that produced the table
    juce::String reason; // why that mode was used
};

// Immutable once created, so the audio thread can read it without locks.
// Frames are 2048 samples (one cycle each). Every frame is stored as 11
// per-octave band-limited mipmap levels built by FFT, so playback never needs
// harmonics above the playback Nyquist limit.
class Wavetable
{
public:
    static constexpr int frameSize = 2048;
    static constexpr int maxFrames = 256;
    static constexpr int numLevels = 11;
    // Each level stores wrapped guard samples (x[N-1] before, x[0], x[1] after)
    // so 4-point interpolation never needs to wrap indices.
    static constexpr int guardBefore = 1;
    static constexpr int guardAfter = 2;
    static constexpr int levelStride = guardBefore + frameSize + guardAfter;

    // Builds mipmaps (allocates, runs FFTs): never call on the audio thread.
    // `frames` holds numFrames * frameSize samples; extra samples are dropped.
    static std::shared_ptr<const Wavetable> create (std::vector<float> frames, WavetableInfo info);
    static std::shared_ptr<const Wavetable> createSine();

    int getNumFrames() const noexcept { return numFrames; }
    const WavetableInfo& getInfo() const noexcept { return info; }
    const std::vector<float>& getFrames() const noexcept { return frames; }
    const float* getFrame (int frame) const noexcept { return frames.data() + static_cast<size_t> (frame) * frameSize; }

    // Pointer to sample 0 of a mip level. Indices -1 .. frameSize + 1 are valid
    // (the guards hold wrapped copies).
    const float* getMip (int frame, int level) const noexcept
    {
        return mips.data() + (static_cast<size_t> (frame) * numLevels + static_cast<size_t> (level)) * levelStride + guardBefore;
    }

    // Highest harmonic kept at a mip level: 1023, 512, 256, ... 1.
    static constexpr int maxHarmonic (int level) noexcept { return level == 0 ? frameSize / 2 - 1 : (frameSize / 2) >> level; }

    // Mip level to use for a fundamental of `frequency` Hz at `sampleRate`.
    // Harmonics are allowed slightly past Nyquist only where their aliases fold
    // back above 20 kHz (inaudible).
    static int chooseLevel (double frequency, double sampleRate) noexcept;

private:
    Wavetable() = default;

    int numFrames = 0;
    std::vector<float> frames;
    std::vector<float> mips;
    WavetableInfo info;
};
} // namespace reload::dsp
