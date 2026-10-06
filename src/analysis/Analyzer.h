#pragma once

#include "dsp/Wavetable.h"

#include <juce_data_structures/juce_data_structures.h>

#include <atomic>
#include <memory>
#include <vector>

namespace reload::analysis
{
// Decoded source audio (mono-summed), kept in memory for re-analysis.
struct SourceAudio
{
    std::vector<float> samples;
    double sampleRate = 44100.0;
    juce::String name; // file name for display
    juce::String path; // full path, used to reload the file with a project

    double getLengthSeconds() const noexcept { return static_cast<double> (samples.size()) / sampleRate; }
};

struct ImportSettings
{
    enum class Mode { automatic, singleCycle };

    Mode mode = Mode::automatic;
    double regionStart = 0.0;  // fraction of the source length
    double regionEnd = 1.0;
    juce::String pitchOverride; // "" = detect; "A3" or "220 Hz" = force
    int cyclesToAverage = 4;
    bool removeDC = true;
    bool normalize = true;

    juce::ValueTree toValueTree() const;
    static ImportSettings fromValueTree (const juce::ValueTree&);
    // Exact comparison is intended: it detects "nothing changed" to skip re-analysis.
    JUCE_BEGIN_IGNORE_WARNINGS_GCC_LIKE ("-Wfloat-equal")
    bool operator== (const ImportSettings&) const = default;
    JUCE_END_IGNORE_WARNINGS_GCC_LIKE
};

struct AnalysisReport
{
    bool ok = false;
    juce::String error;
    juce::String modeUsed;
    juce::String reason;
    double f0 = 0.0;             // Hz
    double aperiodicity = 1.0;   // YIN: 0 = perfectly periodic
    bool pitchWasOverridden = false;
    double extractStart = 0.0;   // seconds into the source
    double extractLength = 0.0;  // seconds (cycles averaged)
    int cyclesUsed = 0;

    juce::ValueTree toValueTree() const;
    static AnalysisReport fromValueTree (const juce::ValueTree&);
};

struct AnalysisResult
{
    AnalysisReport report;
    std::shared_ptr<const dsp::Wavetable> wavetable; // null on failure
};

// Pure function: no globals, safe to run on any background thread.
// `cancel` (optional) is polled between stages.
AnalysisResult analyse (const SourceAudio& source, const ImportSettings& settings,
                        const std::atomic<bool>* cancel = nullptr);

juce::String modeName (ImportSettings::Mode);
} // namespace reload::analysis
