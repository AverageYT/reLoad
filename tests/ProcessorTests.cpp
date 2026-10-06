#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "TestHelpers.h"
#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"

using namespace reload;

namespace
{
juce::AudioBuffer<float> renderProcessor (ReloadProcessor& p, int numChannels, int blockSize, int numBlocks, int note)
{
    juce::AudioBuffer<float> out (numChannels, blockSize * numBlocks);
    juce::AudioBuffer<float> block (numChannels, blockSize);

    for (int b = 0; b < numBlocks; ++b)
    {
        juce::MidiBuffer midi;
        if (b == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, note, 1.0f), 0);
        p.processBlock (block, midi);
        for (int ch = 0; ch < numChannels; ++ch)
            out.copyFrom (ch, b * blockSize, block, ch, 0, blockSize);
    }
    return out;
}
} // namespace

TEST_CASE ("Processor renders a pitched, bounded sine in stereo and mono", "[processor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    ReloadProcessor p;

    for (const int channels : { 2, 1 })
    {
        CAPTURE (channels);
        const auto layout = channels == 2 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::mono();
        REQUIRE (p.setBusesLayout ({ {}, { layout } }));

        p.prepareToPlay (48000.0, 512);
        const auto out = renderProcessor (p, channels, 512, 94, 57); // ~1 s of A3
        p.releaseResources();

        const auto* left = out.getReadPointer (0);
        const int n = out.getNumSamples();
        REQUIRE (out.getMagnitude (0, n) <= 1.0f);
        REQUIRE (test::rms (left + 4800, n - 4800) > 0.01);
        REQUIRE_THAT (test::estimateFrequency (left + 4800, n - 4800, 48000.0), Catch::Matchers::WithinAbs (220.0, 0.05));
    }
}

TEST_CASE ("Unsupported bus layouts are rejected", "[processor]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    ReloadProcessor p;
    REQUIRE_FALSE (p.setBusesLayout ({ {}, { juce::AudioChannelSet::create5point1() } }));
}

TEST_CASE ("Parameter state survives a save/restore round trip", "[processor][state]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::MemoryBlock saved;
    {
        ReloadProcessor p;
        p.getParameters().getParameter (params::id::ampAttack)->setValueNotifyingHost (0.7f);
        p.getParameters().getParameter (params::id::masterGain)->setValueNotifyingHost (0.25f);
        p.getStateInformation (saved);
    }

    ReloadProcessor restored;
    restored.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    REQUIRE_THAT (restored.getParameters().getParameter (params::id::ampAttack)->getValue(), Catch::Matchers::WithinAbs (0.7, 1e-6));
    REQUIRE_THAT (restored.getParameters().getParameter (params::id::masterGain)->getValue(), Catch::Matchers::WithinAbs (0.25, 1e-6));

    // Garbage input must be ignored, not crash.
    const char junk[] = "not a valid state";
    restored.setStateInformation (junk, sizeof (junk));
    REQUIRE_THAT (restored.getParameters().getParameter (params::id::ampAttack)->getValue(), Catch::Matchers::WithinAbs (0.7, 1e-6));
}
