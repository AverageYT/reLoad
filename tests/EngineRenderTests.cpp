#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "TestHelpers.h"

using namespace reload;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace
{
const dsp::Wavetable* sineTable()
{
    static const auto table = dsp::Wavetable::createSine();
    return table.get();
}

dsp::SynthEngine makeEngine (double sampleRate, int blockSize, juce::ADSR::Parameters env = { 0.005f, 0.2f, 0.8f, 0.3f })
{
    dsp::SynthEngine engine;
    engine.prepare (sampleRate, blockSize);
    engine.setAmpEnvelope (env);
    engine.setWavetable (sineTable());
    return engine;
}
} // namespace

TEST_CASE ("A4 renders at 440 Hz at any sample rate and block size", "[engine][render]")
{
    const double sampleRate = GENERATE (44100.0, 48000.0, 88200.0, 96000.0, 192000.0);
    const int blockSize = GENERATE (1, 37, 64, 512, 4096);
    CAPTURE (sampleRate, blockSize);

    auto engine = makeEngine (sampleRate, blockSize);
    const int total = static_cast<int> (sampleRate * 0.5);
    const auto out = test::renderEngine (engine, blockSize, total, { { 0, juce::MidiMessage::noteOn (1, 69, 1.0f) } });

    const int from = static_cast<int> (sampleRate * 0.1);
    const double f = test::estimateFrequency (out.left.data() + from, total - from, sampleRate);
    REQUIRE_THAT (f, WithinAbs (440.0, 0.05));
}

TEST_CASE ("MIDI notes map to equal-tempered pitch across the keyboard", "[engine][render]")
{
    const int note = GENERATE (21, 36, 48, 60, 72, 96, 108);
    CAPTURE (note);

    constexpr double sampleRate = 48000.0;
    auto engine = makeEngine (sampleRate, 512);
    const int total = 48000;
    const auto out = test::renderEngine (engine, 512, total, { { 0, juce::MidiMessage::noteOn (1, note, 1.0f) } });

    const double f = test::estimateFrequency (out.left.data() + 4800, total - 4800, sampleRate);
    REQUIRE_THAT (f, WithinRel (test::midiToHz (note), 1.0e-4));
}

TEST_CASE ("Output does not depend on block size", "[engine][render]")
{
    constexpr double sampleRate = 48000.0;
    const std::vector<test::TimedMidi> events {
        { 0, juce::MidiMessage::noteOn (1, 60, 0.8f) },
        { 1234, juce::MidiMessage::noteOn (1, 64, 0.6f) },
        { 5000, juce::MidiMessage::noteOn (1, 67, 1.0f) },
        { 9001, juce::MidiMessage::noteOff (1, 60) },
        { 15000, juce::MidiMessage::noteOff (1, 64) },
        { 20000, juce::MidiMessage::noteOn (1, 72, 0.5f) },
        { 30000, juce::MidiMessage::allNotesOff (1) },
    };
    constexpr int total = 48000;

    auto refEngine = makeEngine (sampleRate, 4096);
    const auto reference = test::renderEngine (refEngine, 4096, total, events);

    const int blockSize = GENERATE (1, 7, 64, 480, 1000);
    CAPTURE (blockSize);
    auto engine = makeEngine (sampleRate, blockSize);
    const auto out = test::renderEngine (engine, blockSize, total, events);

    float maxDiff = 0.0f;
    for (size_t i = 0; i < reference.left.size(); ++i)
        maxDiff = std::max (maxDiff, std::abs (reference.left[i] - out.left[i]));
    REQUIRE (maxDiff < 1.0e-6f);
}

TEST_CASE ("Silence without notes, and silence after release", "[engine]")
{
    constexpr double sampleRate = 48000.0;

    SECTION ("no notes -> exact silence")
    {
        auto engine = makeEngine (sampleRate, 256);
        const auto out = test::renderEngine (engine, 256, 4800, {});
        REQUIRE (test::rms (out.left.data(), 4800) == 0.0);
    }

    SECTION ("note off -> release -> voice freed")
    {
        auto engine = makeEngine (sampleRate, 256, { 0.005f, 0.1f, 0.8f, 0.3f });
        const int total = 48000; // release ends at 0.2 s + 0.3 s
        const auto out = test::renderEngine (engine, 256, total, {
            { 0, juce::MidiMessage::noteOn (1, 60, 1.0f) },
            { 9600, juce::MidiMessage::noteOff (1, 60) },
        });

        REQUIRE (test::rms (out.left.data() + 2400, 4800) > 0.05);
        const int tailStart = 9600 + static_cast<int> (0.35 * sampleRate);
        REQUIRE (test::rms (out.left.data() + tailStart, total - tailStart) == 0.0);
        REQUIRE (engine.getNumActiveVoices() == 0);
    }
}

TEST_CASE ("Polyphony is capped at 16 voices and stays finite", "[engine]")
{
    auto engine = makeEngine (48000.0, 512);
    std::vector<test::TimedMidi> events;
    for (int i = 0; i < 24; ++i)
        events.push_back ({ i * 10, juce::MidiMessage::noteOn (1, 40 + i, 1.0f) });

    const auto out = test::renderEngine (engine, 512, 24000, events);
    REQUIRE (engine.getNumActiveVoices() == dsp::SynthEngine::maxVoices);
    REQUIRE (test::allFinite (out.left));
    REQUIRE (test::allFinite (out.right));
}

TEST_CASE ("Sustain pedal holds released notes until pedal up", "[engine]")
{
    auto engine = makeEngine (48000.0, 512, { 0.001f, 0.05f, 1.0f, 0.05f });
    const auto out = test::renderEngine (engine, 512, 48000, {
        { 0, juce::MidiMessage::noteOn (1, 60, 1.0f) },
        { 100, juce::MidiMessage::controllerEvent (1, 64, 127) },
        { 2000, juce::MidiMessage::noteOff (1, 60) },
        { 24000, juce::MidiMessage::controllerEvent (1, 64, 0) },
    });

    REQUIRE (test::rms (out.left.data() + 12000, 4800) > 0.1);   // held by pedal
    REQUIRE (test::rms (out.left.data() + 36000, 12000) == 0.0); // released after pedal up
}

TEST_CASE ("Note-on with velocity 0 acts as note-off", "[engine]")
{
    auto engine = makeEngine (48000.0, 512, { 0.001f, 0.05f, 1.0f, 0.05f });
    const auto out = test::renderEngine (engine, 512, 24000, {
        { 0, juce::MidiMessage::noteOn (1, 60, 1.0f) },
        { 2000, juce::MidiMessage::noteOn (1, 60, (juce::uint8) 0) },
    });
    REQUIRE (test::rms (out.left.data() + 12000, 12000) == 0.0);
}
