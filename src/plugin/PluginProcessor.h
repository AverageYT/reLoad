#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/SynthEngine.h"
#include "plugin/MidiInjectionFifo.h"

namespace reload
{
class ReloadProcessor final : public juce::AudioProcessor,
                              private juce::MidiKeyboardState::Listener
{
public:
    ReloadProcessor();
    ~ReloadProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameters; }
    juce::MidiKeyboardState& getKeyboardState() noexcept { return keyboardState; }

    static constexpr int stateVersion = 1;

private:
    void handleNoteOn (juce::MidiKeyboardState*, int midiChannel, int note, float velocity) override;
    void handleNoteOff (juce::MidiKeyboardState*, int midiChannel, int note, float velocity) override;

    juce::AudioProcessorValueTreeState parameters;
    dsp::SynthEngine engine;
    juce::SmoothedValue<float> masterGain;

    juce::MidiKeyboardState keyboardState;
    MidiInjectionFifo injectedMidi;

    std::atomic<float>* masterGainDb = nullptr;
    std::atomic<float>* ampAttack = nullptr;
    std::atomic<float>* ampDecay = nullptr;
    std::atomic<float>* ampSustain = nullptr;
    std::atomic<float>* ampRelease = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReloadProcessor)
};
} // namespace reload
