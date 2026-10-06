#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace reload::params
{
// Parameter IDs are part of saved projects and host automation. Never rename
// one; add a new ID instead.
namespace id
{
    inline constexpr const char* masterGain = "masterGain";
    inline constexpr const char* ampAttack  = "ampAttack";
    inline constexpr const char* ampDecay   = "ampDecay";
    inline constexpr const char* ampSustain = "ampSustain";
    inline constexpr const char* ampRelease = "ampRelease";
    inline constexpr const char* sourceTuning = "sourceTuning";
} // namespace id

// Version hint for parameters introduced in this release (used by AU/VST3).
inline constexpr int version = 1;

inline constexpr float minGainDb = -60.0f;

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
} // namespace reload::params
