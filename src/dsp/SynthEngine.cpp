#include "dsp/SynthEngine.h"

#include <cmath>

namespace reload::dsp
{
namespace
{
    constexpr float voiceHeadroom = 0.25f; // ~ -12 dB per voice at full velocity
} // namespace

//==============================================================================
void Voice::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    amp.setSampleRate (newSampleRate);
    kill();
}

double Voice::noteFrequency (int midiNote, const PitchContext& ctx) noexcept
{
    if (ctx.table == nullptr)
        return 440.0 * std::exp2 ((midiNote - 69) / 12.0);

    const auto& info = ctx.table->getInfo();
    const double rootHz = ctx.sourceTuning ? info.rootFrequency
                                           : 440.0 * std::exp2 ((info.rootNote - 69) / 12.0);
    return rootHz * std::exp2 ((midiNote - info.rootNote) / 12.0);
}

void Voice::start (int midiNote, float velocity, std::uint64_t startOrder, const PitchContext& ctx) noexcept
{
    if (! active)
    {
        // Fresh voice: start at phase 0 (tables are aligned to begin at a
        // rising zero crossing of the fundamental).
        phase = 0.0;
        amp.reset();
    }
    // A retriggered/stolen voice keeps its phase and its current envelope level
    // (juce::ADSR attacks from wherever it is), avoiding discontinuities.

    note = midiNote;
    order = startOrder;
    level = velocity * voiceHeadroom;
    keyDown = true;
    sustained = false;
    active = true;
    updatePitch (ctx);
    amp.noteOn();
}

void Voice::updatePitch (const PitchContext& ctx) noexcept
{
    frequency = noteFrequency (note, ctx);
    // Kept below Nyquist so the phase wrap in render() is always a single subtraction.
    phaseIncrement = std::min (frequency / sampleRate, 0.49);
    mipLevel = Wavetable::chooseLevel (frequency, sampleRate);
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

void Voice::render (const Wavetable& table, float* left, float* right, int numSamples) noexcept
{
    const float* mip = table.getMip (0, mipLevel);
    constexpr double size = Wavetable::frameSize;

    for (int i = 0; i < numSamples; ++i)
    {
        if (! amp.isActive())
        {
            kill();
            return;
        }

        // 4-point cubic Hermite: far lower interpolation images than linear
        // on the harmonic-rich low mip levels.
        const double pos = phase * size;
        const int index = static_cast<int> (pos);
        const float t = static_cast<float> (pos - index);
        const float xm1 = mip[index - 1], x0 = mip[index], x1 = mip[index + 1], x2 = mip[index + 2];
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        const float osc = ((c3 * t + c2) * t + c1) * t + x0;

        const float sample = osc * level * amp.getNextSample();
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
    pitch.sampleRate = sampleRate;
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

void SynthEngine::setWavetable (const Wavetable* table) noexcept
{
    if (table == pitch.table)
        return;
    pitch.table = table;
    for (auto& v : voices)
        if (v.isActive())
            v.updatePitch (pitch);
}

void SynthEngine::setSourceTuning (bool enabled) noexcept
{
    if (enabled == pitch.sourceTuning)
        return;
    pitch.sourceTuning = enabled;
    for (auto& v : voices)
        if (v.isActive())
            v.updatePitch (pitch);
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
    chooseVoice (note).start (note, velocity, ++noteCounter, pitch);
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
    if (buffer.getNumChannels() == 0 || pitch.table == nullptr)
        return;

    auto* left = buffer.getWritePointer (0, start);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1, start) : left;

    for (auto& v : voices)
        if (v.isActive())
            v.render (*pitch.table, left, right, numSamples);
}
} // namespace reload::dsp
