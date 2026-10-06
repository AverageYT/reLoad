#include "analysis/Analyzer.h"

#include "analysis/NoteUtils.h"
#include "analysis/SingleCycle.h"
#include "analysis/Yin.h"

#include <algorithm>
#include <cmath>

namespace reload::analysis
{
namespace
{
    constexpr double minF0 = 27.0;    // just below A0
    constexpr double maxF0 = 4200.0;  // just above C8
    constexpr double voicedThreshold = 0.15;
    constexpr double noisyAperiodicity = 0.35;

    bool cancelled (const std::atomic<bool>* cancel) { return cancel != nullptr && cancel->load(); }

    struct Frame
    {
        int position = 0; // sample index of analysis window start
        YinEstimate yin;
        double rms = 0.0;
    };

    AnalysisResult failure (juce::String message)
    {
        AnalysisResult r;
        r.report.error = std::move (message);
        return r;
    }
} // namespace

juce::String modeName (ImportSettings::Mode mode)
{
    switch (mode)
    {
        case ImportSettings::Mode::automatic: return "Auto";
        case ImportSettings::Mode::singleCycle: return "Single cycle";
    }
    return {};
}

//==============================================================================
juce::ValueTree ImportSettings::toValueTree() const
{
    juce::ValueTree v ("ImportSettings");
    v.setProperty ("mode", static_cast<int> (mode), nullptr);
    v.setProperty ("regionStart", regionStart, nullptr);
    v.setProperty ("regionEnd", regionEnd, nullptr);
    v.setProperty ("pitchOverride", pitchOverride, nullptr);
    v.setProperty ("cyclesToAverage", cyclesToAverage, nullptr);
    v.setProperty ("removeDC", removeDC, nullptr);
    v.setProperty ("normalize", normalize, nullptr);
    return v;
}

ImportSettings ImportSettings::fromValueTree (const juce::ValueTree& v)
{
    ImportSettings s;
    s.mode = static_cast<Mode> (juce::jlimit (0, 1, static_cast<int> (v.getProperty ("mode", 0))));
    s.regionStart = juce::jlimit (0.0, 1.0, static_cast<double> (v.getProperty ("regionStart", 0.0)));
    s.regionEnd = juce::jlimit (s.regionStart, 1.0, static_cast<double> (v.getProperty ("regionEnd", 1.0)));
    s.pitchOverride = v.getProperty ("pitchOverride", "").toString();
    s.cyclesToAverage = juce::jlimit (1, 32, static_cast<int> (v.getProperty ("cyclesToAverage", 4)));
    s.removeDC = v.getProperty ("removeDC", true);
    s.normalize = v.getProperty ("normalize", true);
    return s;
}

juce::ValueTree AnalysisReport::toValueTree() const
{
    juce::ValueTree v ("AnalysisReport");
    v.setProperty ("ok", ok, nullptr);
    v.setProperty ("error", error, nullptr);
    v.setProperty ("modeUsed", modeUsed, nullptr);
    v.setProperty ("reason", reason, nullptr);
    v.setProperty ("f0", f0, nullptr);
    v.setProperty ("aperiodicity", aperiodicity, nullptr);
    v.setProperty ("pitchWasOverridden", pitchWasOverridden, nullptr);
    v.setProperty ("extractStart", extractStart, nullptr);
    v.setProperty ("extractLength", extractLength, nullptr);
    v.setProperty ("cyclesUsed", cyclesUsed, nullptr);
    return v;
}

AnalysisReport AnalysisReport::fromValueTree (const juce::ValueTree& v)
{
    AnalysisReport r;
    r.ok = v.getProperty ("ok", false);
    r.error = v.getProperty ("error", "").toString();
    r.modeUsed = v.getProperty ("modeUsed", "").toString();
    r.reason = v.getProperty ("reason", "").toString();
    r.f0 = v.getProperty ("f0", 0.0);
    r.aperiodicity = v.getProperty ("aperiodicity", 1.0);
    r.pitchWasOverridden = v.getProperty ("pitchWasOverridden", false);
    r.extractStart = v.getProperty ("extractStart", 0.0);
    r.extractLength = v.getProperty ("extractLength", 0.0);
    r.cyclesUsed = v.getProperty ("cyclesUsed", 0);
    return r;
}

//==============================================================================
AnalysisResult analyse (const SourceAudio& source, const ImportSettings& settings, const std::atomic<bool>* cancel)
{
    const double sr = source.sampleRate;
    const int total = static_cast<int> (source.samples.size());
    if (total == 0 || sr <= 0.0)
        return failure ("The file contains no audio.");

    const int regionStart = juce::jlimit (0, total, static_cast<int> (settings.regionStart * total));
    const int regionEnd = juce::jlimit (regionStart, total, static_cast<int> (settings.regionEnd * total));
    const int regionLength = regionEnd - regionStart;
    const float* region = source.samples.data() + regionStart;

    // ---- Pitch search range (narrowed by a note override) ----
    const auto override = parsePitchOverride (settings.pitchOverride);
    if (override.kind == PitchOverride::Kind::invalid)
        return failure ("Couldn't read the pitch override \"" + settings.pitchOverride + "\". Use a note like A3 or a frequency like 220 Hz.");

    double searchMinF0 = minF0, searchMaxF0 = maxF0;
    if (override.kind == PitchOverride::Kind::note)
    {
        const double centre = midiToHz (override.value);
        searchMinF0 = centre * std::exp2 (-1.0 / 12.0);
        searchMaxF0 = centre * std::exp2 (1.0 / 12.0);
    }
    else if (override.kind == PitchOverride::Kind::hz)
    {
        searchMinF0 = searchMaxF0 = override.value;
    }

    const int maxLag = static_cast<int> (std::ceil (sr / std::max (searchMinF0 * 0.99, minF0))) + 2;
    const int window = std::max (1024, maxLag);
    Yin yin (window, maxLag);
    if (regionLength < yin.getSamplesNeeded())
        return failure ("The selected region is too short to analyse. Select at least "
                        + juce::String (yin.getSamplesNeeded() / sr * 1000.0, 0) + " ms.");

    // ---- Track pitch across the region ----
    const int minLag = std::max (2, static_cast<int> (std::floor (sr / (searchMaxF0 * 1.01))));
    const int maxLagToSearch = static_cast<int> (std::ceil (sr / (searchMinF0 * 0.99)));
    const int hop = std::max (256, window / 4);

    std::vector<Frame> frames;
    double maxRms = 0.0;
    for (int pos = 0; pos + yin.getSamplesNeeded() <= regionLength; pos += hop)
    {
        Frame f;
        f.position = pos;
        f.yin = yin.analyse (region + pos, minLag, maxLagToSearch, voicedThreshold);
        double sum = 0.0;
        for (int i = 0; i < window; ++i)
            sum += static_cast<double> (region[pos + i]) * region[pos + i];
        f.rms = std::sqrt (sum / window);
        maxRms = std::max (maxRms, f.rms);
        frames.push_back (f);

        if (cancelled (cancel))
            return failure ("Cancelled");
    }

    if (maxRms < 1.0e-5)
        return failure ("The selected region is silent.");

    // ---- Pick the most periodic frame among the reasonably loud ones ----
    const Frame* best = nullptr;
    for (const auto& f : frames)
    {
        if (f.rms < 0.1 * maxRms)
            continue;
        if (best == nullptr || f.yin.aperiodicity < best->yin.aperiodicity)
            best = &f;
    }
    jassert (best != nullptr);

    AnalysisReport report;
    double period = 0.0;
    if (override.kind == PitchOverride::Kind::hz)
    {
        period = sr / override.value;
        report.pitchWasOverridden = true;
    }
    else
    {
        period = refinePeriod (region + best->position, regionLength - best->position, best->yin.period);
        report.pitchWasOverridden = override.kind == PitchOverride::Kind::note;
    }
    report.f0 = sr / period;
    report.aperiodicity = best->yin.aperiodicity;

    if (cancelled (cancel))
        return failure ("Cancelled");

    // ---- Extract ----
    SingleCycleOptions options;
    options.cyclesToAverage = settings.cyclesToAverage;
    options.removeDC = settings.removeDC;
    options.normalize = settings.normalize;

    const auto cycle = extractSingleCycle (region, regionLength, best->position, period, options);
    if (! cycle.ok)
        return failure ("Couldn't extract a full cycle from the selected region.");

    report.ok = true;
    report.cyclesUsed = cycle.cyclesUsed;
    report.extractStart = (regionStart + best->position) / sr;
    report.extractLength = cycle.cyclesUsed * period / sr;
    report.modeUsed = "Single cycle";

    const auto where = juce::String (report.extractStart, 2) + " s";
    if (settings.mode == ImportSettings::Mode::automatic)
    {
        if (report.aperiodicity > noisyAperiodicity)
            report.reason = "Auto: the source is noisy or inharmonic (aperiodicity "
                            + juce::String (report.aperiodicity, 2)
                            + "), so a single cycle is only a rough approximation. Spectral mode arrives in M3.";
        else
            report.reason = "Auto: the source is periodic (aperiodicity " + juce::String (report.aperiodicity, 2)
                            + "); took " + juce::String (cycle.cyclesUsed) + " cycles at " + where + ".";
    }
    else
    {
        report.reason = "Single cycle (manual): " + juce::String (cycle.cyclesUsed) + " cycles at " + where + ".";
    }
    if (report.pitchWasOverridden)
        report.reason << " Pitch override: " << settings.pitchOverride.trim() << ".";

    dsp::WavetableInfo info;
    info.rootFrequency = report.f0;
    info.rootNote = static_cast<int> (std::lround (hzToMidi (report.f0)));
    info.name = source.name;
    info.mode = report.modeUsed;
    info.reason = report.reason;

    AnalysisResult result;
    result.report = report;
    result.wavetable = dsp::Wavetable::create (cycle.frame, std::move (info));
    return result;
}
} // namespace reload::analysis
