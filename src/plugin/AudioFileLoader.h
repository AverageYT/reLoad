#pragma once

#include "analysis/Analyzer.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace reload
{
// Longest source we'll decode. One-shots and chords are short; this bounds
// memory if someone drops a whole song.
inline constexpr double maxSourceSeconds = 60.0;

// File extensions accepted for drag-and-drop / the file chooser.
inline const juce::StringArray& supportedAudioExtensions()
{
    static const juce::StringArray exts { "wav", "aif", "aiff", "flac", "mp3", "ogg" };
    return exts;
}

// Decodes an audio file to a mono-summed SourceAudio. Returns null and sets
// `error` on failure. Blocking: call from a background thread.
std::shared_ptr<const analysis::SourceAudio> loadAudioFile (const juce::File& file, juce::String& error);
} // namespace reload
