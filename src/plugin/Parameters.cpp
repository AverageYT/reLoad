#include "plugin/Parameters.h"

namespace reload::params
{
namespace
{
    juce::String formatSeconds (float seconds, int)
    {
        if (seconds < 1.0f)
            return juce::String (seconds * 1000.0f, 1) + " ms";
        return juce::String (seconds, 2) + " s";
    }

    float parseSeconds (const juce::String& text)
    {
        const auto t = text.trim().toLowerCase();
        const auto value = t.getFloatValue();
        if (t.endsWith ("ms"))
            return value / 1000.0f;
        if (t.endsWith ("s"))
            return value;
        // Bare number: values above the 10 s maximum are taken as milliseconds.
        return value > 10.0f ? value / 1000.0f : value;
    }

    std::unique_ptr<juce::AudioParameterFloat> makeTime (const char* paramId, const juce::String& name, float defaultSeconds)
    {
        juce::NormalisableRange<float> range (0.001f, 10.0f);
        range.setSkewForCentre (0.5f);
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { paramId, version }, name, range, defaultSeconds,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction (formatSeconds)
                .withValueFromStringFunction (parseSeconds));
    }
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::masterGain, version }, "Master Gain",
        juce::NormalisableRange<float> (minGainDb, 6.0f, 0.1f), -6.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("dB")
            .withStringFromValueFunction ([] (float db, int) {
                return db <= minGainDb ? juce::String ("-inf dB") : juce::String (db, 1) + " dB";
            })
            .withValueFromStringFunction ([] (const juce::String& t) {
                return t.trim().startsWithIgnoreCase ("-inf") ? minGainDb : t.getFloatValue();
            })));

    layout.add (makeTime (id::ampAttack, "Amp Attack", 0.005f));
    layout.add (makeTime (id::ampDecay, "Amp Decay", 0.2f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::ampSustain, version }, "Amp Sustain",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.8f,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; })
            .withValueFromStringFunction ([] (const juce::String& t) {
                const auto v = t.getFloatValue();
                return t.containsChar ('%') || v > 1.0f ? v / 100.0f : v;
            })));

    layout.add (makeTime (id::ampRelease, "Amp Release", 0.3f));

    return layout;
}
} // namespace reload::params
