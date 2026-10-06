#pragma once

#include <juce_core/juce_core.h>

#include <cmath>

namespace reload::analysis
{
inline double midiToHz (double note) noexcept { return 440.0 * std::exp2 ((note - 69.0) / 12.0); }
inline double hzToMidi (double hz) noexcept { return 69.0 + 12.0 * std::log2 (hz / 440.0); }

// "A3", "C#4", ... (middle C = C4 = MIDI 60).
juce::String noteName (int midiNote);

// Note name plus cents, e.g. "A3 +12 c".
juce::String describePitch (double hz);

// User override for the source pitch: blank (auto), a note ("A3", "c#2",
// "Eb4") or a frequency ("220", "220 Hz", "220.5hz").
struct PitchOverride
{
    enum class Kind { none, note, hz, invalid };
    Kind kind = Kind::none;
    double value = 0.0; // MIDI note for Kind::note, Hz for Kind::hz
};

PitchOverride parsePitchOverride (const juce::String& text);
} // namespace reload::analysis
