#pragma once

#include "dsp/Wavetable.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <cstdint>

namespace reload::dsp
{
// Pitch mapping shared by all voices for the current block.
struct PitchContext
{
    const Wavetable* table = nullptr;
    double sampleRate = 44100.0;
    bool sourceTuning = true; // play the table's root note at the source's exact frequency
};

// One polyphonic voice: a mipmapped wavetable oscillator into an amp ADSR.
class Voice
{
public:
    void prepare (double newSampleRate);
    void setAmpEnvelope (const juce::ADSR::Parameters& p) { amp.setParameters (p); }

    void start (int midiNote, float velocity, std::uint64_t startOrder, const PitchContext& ctx) noexcept;
    void release() noexcept;   // enter the envelope's release stage
    void kill() noexcept;      // silence immediately
    void updatePitch (const PitchContext& ctx) noexcept;

    bool isActive() const noexcept { return active; }
    int getNote() const noexcept { return note; }
    std::uint64_t getOrder() const noexcept { return order; }
    double getFrequency() const noexcept { return frequency; }

    // Held by a key or by the sustain pedal (i.e. not yet released).
    bool isHeld() const noexcept { return keyDown || sustained; }
    bool keyDown = false;
    bool sustained = false;

    // Adds this voice into left/right. Pass the same pointer twice for mono.
    void render (const Wavetable& table, float* left, float* right, int numSamples) noexcept;

    static double noteFrequency (int midiNote, const PitchContext& ctx) noexcept;

private:
    double sampleRate = 44100.0;
    double phase = 0.0;          // [0, 1)
    double phaseIncrement = 0.0; // cycles per sample
    double frequency = 0.0;
    int mipLevel = 0;
    float level = 0.0f;
    juce::ADSR amp;
    int note = -1;
    std::uint64_t order = 0;
    bool active = false;
};

// Fixed-size polyphonic engine. Allocation- and lock-free after prepare().
class SynthEngine
{
public:
    static constexpr int maxVoices = 16;

    void prepare (double sampleRate, int maxBlockSize);
    void reset() noexcept;
    void setAmpEnvelope (const juce::ADSR::Parameters& p) noexcept;

    // Call once per block before process(). The table must stay alive until
    // the next call (WavetableExchange guarantees this).
    void setWavetable (const Wavetable* table) noexcept;
    void setSourceTuning (bool enabled) noexcept;

    // Overwrites the buffer. MIDI is applied sample-accurately at each event's offset.
    void process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi) noexcept;

    // Applies a raw MIDI message immediately (used for on-screen keyboard events).
    void handleMidi (const std::uint8_t* data, int numBytes) noexcept;

    int getNumActiveVoices() const noexcept;
    const Voice& getVoice (int index) const noexcept { return voices[static_cast<size_t> (index)]; }

private:
    void noteOn (int note, float velocity) noexcept;
    void noteOff (int note) noexcept;
    void setSustainPedal (bool down) noexcept;
    void allNotesOff() noexcept;
    Voice& chooseVoice (int note) noexcept;
    void render (juce::AudioBuffer<float>& buffer, int start, int numSamples) noexcept;

    std::array<Voice, maxVoices> voices;
    PitchContext pitch;
    std::uint64_t noteCounter = 0;
    bool sustainPedalDown = false;
};
} // namespace reload::dsp
