#pragma once

#include "dsp/Wavetable.h"

#include <juce_data_structures/juce_data_structures.h>

namespace reload::state
{
// Wavetable <-> ValueTree. Frames are stored as gzip-compressed float32 so
// projects and presets reload without the original audio file. Mipmaps are
// rebuilt on load.
juce::ValueTree wavetableToValueTree (const dsp::Wavetable& table);
std::shared_ptr<const dsp::Wavetable> wavetableFromValueTree (const juce::ValueTree& tree);

// Plugin state blob: 4-byte magic + binary ValueTree.
void writeState (const juce::ValueTree& state, juce::MemoryBlock& dest);
juce::ValueTree readState (const void* data, int sizeInBytes);
} // namespace reload::state
