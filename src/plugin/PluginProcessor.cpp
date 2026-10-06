#include "plugin/PluginProcessor.h"

#include "plugin/Parameters.h"
#include "plugin/PluginEditor.h"
#include "plugin/StateSerialization.h"

namespace reload
{
namespace
{
    const juce::Identifier stateType ("reLoadState");
    const juce::Identifier wavetableType ("Wavetable");
    const juce::Identifier importType ("Import");
} // namespace

ReloadProcessor::ReloadProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, stateType, params::createLayout())
{
    masterGainDb = parameters.getRawParameterValue (params::id::masterGain);
    ampAttack = parameters.getRawParameterValue (params::id::ampAttack);
    ampDecay = parameters.getRawParameterValue (params::id::ampDecay);
    ampSustain = parameters.getRawParameterValue (params::id::ampSustain);
    ampRelease = parameters.getRawParameterValue (params::id::ampRelease);
    sourceTuning = parameters.getRawParameterValue (params::id::sourceTuning);

    setWavetable (dsp::Wavetable::createSine());
    keyboardState.addListener (this);
    startTimerHz (4); // frees wavetables retired by the audio thread
}

ReloadProcessor::~ReloadProcessor()
{
    stopTimer();
    keyboardState.removeListener (this);
}

void ReloadProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    masterGain.reset (sampleRate, 0.02);
    masterGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (masterGainDb->load(), params::minGainDb));
}

void ReloadProcessor::releaseResources()
{
    engine.reset();
}

bool ReloadProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void ReloadProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    engine.setWavetable (tableExchange.acquire());
    engine.setSourceTuning (sourceTuning->load() >= 0.5f);
    engine.setAmpEnvelope ({ ampAttack->load(), ampDecay->load(), ampSustain->load(), ampRelease->load() });

    injectedMidi.drain ([this] (const MidiInjectionFifo::Event& e) {
        engine.handleMidi (e.bytes.data(), e.size);
    });

    engine.process (buffer, midi);

    masterGain.setTargetValue (juce::Decibels::decibelsToGain (masterGainDb->load(), params::minGainDb));
    masterGain.applyGain (buffer, buffer.getNumSamples());
}

juce::AudioProcessorEditor* ReloadProcessor::createEditor()
{
    return new ReloadEditor (*this);
}

std::shared_ptr<const dsp::Wavetable> ReloadProcessor::getWavetable() const
{
    const juce::ScopedLock sl (tableLock);
    return wavetable;
}

void ReloadProcessor::setWavetable (std::shared_ptr<const dsp::Wavetable> table)
{
    if (table == nullptr)
        return;
    {
        const juce::ScopedLock sl (tableLock);
        wavetable = table;
    }
    tableExchange.publish (std::move (table));
}

void ReloadProcessor::timerCallback()
{
    tableExchange.collectGarbage();
}

void ReloadProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty ("stateVersion", stateVersion, nullptr);
    state.appendChild (importer.toValueTree(), nullptr);
    if (const auto table = getWavetable())
        state.appendChild (state::wavetableToValueTree (*table), nullptr);

    state::writeState (state, destData);
}

void ReloadProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto state = state::readState (data, sizeInBytes);
    if (! state.isValid() || ! state.hasType (stateType))
        return;

    const auto tableTree = state.getChildWithName (wavetableType);
    const auto importTree = state.getChildWithName (importType);
    state.removeChild (tableTree, nullptr);
    state.removeChild (importTree, nullptr);
    parameters.replaceState (state);

    auto table = state::wavetableFromValueTree (tableTree);
    setWavetable (table != nullptr ? std::move (table) : dsp::Wavetable::createSine());
    importer.restoreFromValueTree (importTree);
}

void ReloadProcessor::handleNoteOn (juce::MidiKeyboardState*, int midiChannel, int note, float velocity)
{
    injectedMidi.push (juce::MidiMessage::noteOn (midiChannel, note, velocity));
}

void ReloadProcessor::handleNoteOff (juce::MidiKeyboardState*, int midiChannel, int note, float velocity)
{
    injectedMidi.push (juce::MidiMessage::noteOff (midiChannel, note, velocity));
}
} // namespace reload

// Entry point used by the JUCE plugin wrappers.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new reload::ReloadProcessor();
}
