#include "dsp/SynthEngine.h"

#include <cmath>

namespace reload::dsp
{
namespace
{
    constexpr double twoPi = 6.283185307179586476925286766559;
    constexpr float voiceHeadroom = 0.25f; // ~ -12 dB per voice at full velocity

    double midiNoteToHz (int note) noexcept
    {
        return 440.0 * std::exp2 ((note - 69) / 12.0);
    }
} // namespace

//==============================================================================
void Voice::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    amp.setSampleRate (newSampleRate);
    kill();
}

void Voice::start (int midiNote, float velocity, std::uint64_t startOrder) noexcept
{
    if (! active)
    {
        // Fresh voice: start the sine at zero so there's no click.
        phase = 0.0;
        amp.reset();
    }
    // A retriggered/stolen voice keeps its phase and its current envelope level
    // (juce::ADSR attacks from wherever it is), avoiding discontinuities.

    note = midiNote;
    order = startOrder;
    level = velocity * voiceHeadroom;
    phaseIncrement = midiNoteToHz (midiNote) / sampleRate;
    keyDown = true;
    sustained = false;
    active = true;
    amp.noteOn();
}

void Voice::release() noexcept
{
    keyDown = false;
    sustained = false;
    amp.noteOff();
}

void Voice::kill() noexcept
{
    amp.reset();
    active = false;
    keyDown = false;
    sustained = false;
    note = -1;
}

void Voice::render (float* left, float* right, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        if (! amp.isActive())
        {
            kill();
            return;
        }

        const auto sample = static_cast<float> (std::sin (phase * twoPi)) * level * amp.getNextSample();
        left[i] += sample;
        if (right != left)
            right[i] += sample;

        phase += phaseIncrement;
        if (phase >= 1.0)
            phase -= 1.0;
    }

    if (! amp.isActive())
        kill();
}

//==============================================================================
void SynthEngine::prepare (double sampleRate, int /*maxBlockSize*/)
{
    for (auto& v : voices)
        v.prepare (sampleRate);
    sustainPedalDown = false;
}

void SynthEngine::reset() noexcept
{
    for (auto& v : voices)
        v.kill();
    sustainPedalDown = false;
}

void SynthEngine::setAmpEnvelope (const juce::ADSR::Parameters& p) noexcept
{
    for (auto& v : voices)
        v.setAmpEnvelope (p);
}

int SynthEngine::getNumActiveVoices() const noexcept
{
    int n = 0;
    for (const auto& v : voices)
        n += v.isActive() ? 1 : 0;
    return n;
}

void SynthEngine::process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi) noexcept
{
    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    int position = 0;
    for (const auto event : midi)
    {
        const int eventPos = juce::jlimit (0, numSamples, event.samplePosition);
        if (eventPos > position)
        {
            render (buffer, position, eventPos - position);
            position = eventPos;
        }
        handleMidi (event.data, event.numBytes);
    }

    if (position < numSamples)
        render (buffer, position, numSamples - position);
}

void SynthEngine::handleMidi (const std::uint8_t* data, int numBytes) noexcept
{
    // Parse raw bytes rather than constructing juce::MidiMessage, which can
    // allocate for long (sysex) messages.
    if (numBytes < 1 || data[0] < 0x80 || data[0] >= 0xf0)
        return;

    const int status = data[0] & 0xf0;
    const int d1 = numBytes > 1 ? data[1] & 0x7f : 0;
    const int d2 = numBytes > 2 ? data[2] & 0x7f : 0;

    switch (status)
    {
        case 0x90:
            if (d2 > 0)
                noteOn (d1, static_cast<float> (d2) / 127.0f);
            else
                noteOff (d1);
            break;

        case 0x80:
            noteOff (d1);
            break;

        case 0xb0:
            if (d1 == 64)
                setSustainPedal (d2 >= 64);
            else if (d1 == 120) // all sound off
                reset();
            else if (d1 == 123) // all notes off
                allNotesOff();
            break;

        default:
            break;
    }
}

void SynthEngine::noteOn (int note, float velocity) noexcept
{
    chooseVoice (note).start (note, velocity, ++noteCounter);
}

void SynthEngine::noteOff (int note) noexcept
{
    for (auto& v : voices)
    {
        if (v.isActive() && v.keyDown && v.getNote() == note)
        {
            if (sustainPedalDown)
            {
                v.keyDown = false;
                v.sustained = true;
            }
            else
            {
                v.release();
            }
        }
    }
}

void SynthEngine::setSustainPedal (bool down) noexcept
{
    sustainPedalDown = down;
    if (down)
        return;

    for (auto& v : voices)
        if (v.isActive() && v.sustained)
            v.release();
}

void SynthEngine::allNotesOff() noexcept
{
    sustainPedalDown = false;
    for (auto& v : voices)
        if (v.isActive() && v.isHeld())
            v.release();
}

Voice& SynthEngine::chooseVoice (int note) noexcept
{
    // 1. Retrigger a voice already playing this note.
    for (auto& v : voices)
        if (v.isActive() && v.getNote() == note)
            return v;

    // 2. A free voice.
    for (auto& v : voices)
        if (! v.isActive())
            return v;

    // 3. Steal: the oldest released voice, else the oldest voice overall.
    Voice* oldestReleased = nullptr;
    Voice* oldest = &voices[0];
    for (auto& v : voices)
    {
        if (! v.isHeld() && (oldestReleased == nullptr || v.getOrder() < oldestReleased->getOrder()))
            oldestReleased = &v;
        if (v.getOrder() < oldest->getOrder())
            oldest = &v;
    }
    return oldestReleased != nullptr ? *oldestReleased : *oldest;
}

void SynthEngine::render (juce::AudioBuffer<float>& buffer, int start, int numSamples) noexcept
{
    if (buffer.getNumChannels() == 0)
        return;

    auto* left = buffer.getWritePointer (0, start);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1, start) : left;

    for (auto& v : voices)
        if (v.isActive())
            v.render (left, right, numSamples);
}
} // namespace reload::dsp
