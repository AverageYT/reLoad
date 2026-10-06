#include "analysis/NoteUtils.h"

namespace reload::analysis
{
namespace
{
    const char* const sharpNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

juce::String noteName (int midiNote)
{
    const int pitchClass = ((midiNote % 12) + 12) % 12;
    const int octave = static_cast<int> (std::floor (midiNote / 12.0)) - 1;
    return juce::String (sharpNames[pitchClass]) + juce::String (octave);
}

juce::String describePitch (double hz)
{
    if (! (hz > 0.0))
        return "-";
    const double midi = hzToMidi (hz);
    const int nearest = static_cast<int> (std::lround (midi));
    const int cents = static_cast<int> (std::lround ((midi - nearest) * 100.0));
    return noteName (nearest) + (cents >= 0 ? " +" : " ") + juce::String (cents) + " c";
}

PitchOverride parsePitchOverride (const juce::String& raw)
{
    const auto text = raw.trim().toLowerCase().removeCharacters (" ");
    if (text.isEmpty() || text == "auto")
        return {};

    // Frequency: digits with optional "hz".
    const auto numeric = text.endsWith ("hz") ? text.dropLastCharacters (2) : text;
    if (numeric.containsOnly ("0123456789.") && numeric.containsAnyOf ("0123456789"))
    {
        const double hz = numeric.getDoubleValue();
        if (hz >= 8.0 && hz <= 12000.0)
            return { PitchOverride::Kind::hz, hz };
        return { PitchOverride::Kind::invalid, 0.0 };
    }

    // Note name: letter, optional accidental, octave (may be negative).
    static const int letterToPc[] = { 9, 11, 0, 2, 4, 5, 7 }; // a b c d e f g
    const auto letter = text[0];
    if (letter < 'a' || letter > 'g')
        return { PitchOverride::Kind::invalid, 0.0 };

    int pc = letterToPc[letter - 'a'];
    int pos = 1;
    if (pos < text.length() && (text[pos] == '#' || text[pos] == 'b'))
    {
        pc += text[pos] == '#' ? 1 : -1;
        ++pos;
    }

    const auto octaveText = text.substring (pos);
    if (octaveText.isEmpty() || ! octaveText.trimCharactersAtStart ("-").containsOnly ("0123456789"))
        return { PitchOverride::Kind::invalid, 0.0 };

    const int midi = (octaveText.getIntValue() + 1) * 12 + pc;
    if (midi < 0 || midi > 127)
        return { PitchOverride::Kind::invalid, 0.0 };
    return { PitchOverride::Kind::note, static_cast<double> (midi) };
}
} // namespace reload::analysis
