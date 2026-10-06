#pragma once

#include <juce_graphics/juce_graphics.h>

// Interim colours. The full LookAndFeel arrives with the M5 GUI pass.
namespace reload::gui::theme
{
inline const juce::Colour background { 0xff15171c };
inline const juce::Colour panelBackground { 0xff1e2128 };
inline const juce::Colour text { 0xffe6e8ec };
inline const juce::Colour textDim { 0xff8b919c };
inline const juce::Colour accent { 0xff3fb6a8 };
inline const juce::Colour waveform { 0xffa7c4ff };
inline const juce::Colour handle { 0xfff2c14e };
inline const juce::Colour error { 0xffff6b6b };
} // namespace reload::gui::theme
