#include "plugin/PluginProcessor.h"

#include "plugin/Parameters.h"
#include "plugin/PluginEditor.h"

namespace reload
{
ReloadProcessor::ReloadProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "reLoadState", params::createLayout())
{
    parameters.state.setProperty ("stateVersion", stateVersion, nullptr);

    masterGainDb = parameters.getRawParameterValue (params::id::masterGain);
    ampAttack = parameters.getRawParameterValue (params::id::ampAttack);
    ampDecay = parameters.getRawParameterValue (params::id::ampDecay);
    ampSustain = parameters.getRawParameterValue (params::id::ampSustain);
    ampRelease = parameters.getRawParameterValue (params::id::ampRelease);

    keyboardState.addListener (this);
}

ReloadProcessor::~ReloadProcessor()
{
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

void ReloadProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const auto state = parameters.copyState();
    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ReloadProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    state.setProperty ("stateVersion", stateVersion, nullptr);
    parameters.replaceState (state);
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
