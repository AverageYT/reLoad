#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "TestSignals.h"
#include "analysis/Analyzer.h"
#include "analysis/NoteUtils.h"
#include "analysis/SingleCycle.h"

#include <numeric>

using namespace reload;
using Catch::Matchers::WithinAbs;

namespace
{
analysis::SourceAudio makeSource (std::vector<float> samples, double sampleRate)
{
    analysis::SourceAudio s;
    s.samples = std::move (samples);
    s.sampleRate = sampleRate;
    s.name = "synthetic";
    return s;
}

analysis::AnalysisResult analyseSignal (std::vector<float> samples, double sampleRate, analysis::ImportSettings settings = {})
{
    return analysis::analyse (makeSource (std::move (samples), sampleRate), settings);
}
} // namespace

//==============================================================================
TEST_CASE ("Pitch detection is within 1 cent for sine, saw and square", "[analysis][pitch]")
{
    const double sampleRate = GENERATE (44100.0, 48000.0, 96000.0);
    const double f0 = GENERATE (30.0, 55.0, 110.0, 220.5, 440.0, 1046.5, 2093.0, 3520.0);
    const int shape = GENERATE (0, 1, 2);
    CAPTURE (sampleRate, f0, shape);

    test::SignalSpec spec;
    spec.sampleRate = sampleRate;
    spec.seconds = 0.6;
    auto x = shape == 0 ? test::sine (f0, spec) : shape == 1 ? test::saw (f0, spec) : test::square (f0, spec);

    const auto result = analyseSignal (std::move (x), sampleRate);
    REQUIRE (result.report.ok);
    REQUIRE (result.wavetable != nullptr);
    CHECK (std::abs (test::centsBetween (result.report.f0, f0)) < 1.0);
    // YIN's difference function is evaluated at whole-sample lags, so very
    // short periods (bright, high notes) read as slightly less periodic.
    CHECK (result.report.aperiodicity < (sampleRate / f0 < 20.0 ? 0.15 : 0.05));
    CHECK (result.wavetable->getInfo().rootNote == static_cast<int> (std::lround (analysis::hzToMidi (f0))));
}

TEST_CASE ("Pitch detection tolerates noise and decay", "[analysis][pitch]")
{
    test::SignalSpec spec;
    spec.seconds = 1.5;
    spec.decaySeconds = 0.6;
    auto x = test::saw (164.81, spec); // E3
    test::addNoise (x, 0.01f);         // ~ -30 dB relative to the signal

    const auto result = analyseSignal (std::move (x), spec.sampleRate);
    REQUIRE (result.report.ok);
    CHECK (std::abs (test::centsBetween (result.report.f0, 164.81)) < 3.0);
}

TEST_CASE ("Pitch override forces or narrows the detected pitch", "[analysis][pitch]")
{
    test::SignalSpec spec;
    const auto x = test::saw (220.0, spec);

    SECTION ("frequency override is used verbatim")
    {
        analysis::ImportSettings settings;
        settings.pitchOverride = "110 Hz";
        const auto r = analyseSignal (x, spec.sampleRate, settings);
        REQUIRE (r.report.ok);
        CHECK (r.report.pitchWasOverridden);
        CHECK_THAT (r.report.f0, WithinAbs (110.0, 1e-9));
    }

    SECTION ("note override searches near that note and refines")
    {
        analysis::ImportSettings settings;
        settings.pitchOverride = "A3";
        const auto r = analyseSignal (x, spec.sampleRate, settings);
        REQUIRE (r.report.ok);
        CHECK (std::abs (test::centsBetween (r.report.f0, 220.0)) < 1.0);
    }

    SECTION ("invalid override reports an error")
    {
        analysis::ImportSettings settings;
        settings.pitchOverride = "banana";
        const auto r = analyseSignal (x, spec.sampleRate, settings);
        CHECK_FALSE (r.report.ok);
        CHECK (r.report.error.isNotEmpty());
        CHECK (r.wavetable == nullptr);
    }
}

TEST_CASE ("Pitch override text parsing", "[analysis][notes]")
{
    using K = analysis::PitchOverride::Kind;
    const auto parse = analysis::parsePitchOverride;

    CHECK (parse ("").kind == K::none);
    CHECK (parse ("  auto ").kind == K::none);
    CHECK (parse ("A3").kind == K::note);
    CHECK (parse ("A3").value == 57.0);
    CHECK (parse ("c#4").value == 61.0);
    CHECK (parse ("Eb2").value == 39.0);
    CHECK (parse ("C-1").value == 0.0);
    CHECK (parse ("220").kind == K::hz);
    CHECK (parse ("220 Hz").value == 220.0);
    CHECK (parse ("220.5hz").value == 220.5);
    CHECK (parse ("H3").kind == K::invalid);
    CHECK (parse ("A").kind == K::invalid);
    CHECK (parse ("99999").kind == K::invalid);

    CHECK (analysis::noteName (60) == "C4");
    CHECK (analysis::noteName (57) == "A3");
    CHECK (analysis::noteName (0) == "C-1");
    CHECK (analysis::describePitch (440.0) == "A4 +0 c");
}

//==============================================================================
TEST_CASE ("Single-cycle extraction preserves the harmonic spectrum", "[analysis][extract]")
{
    // Saw with exactly 30 harmonics: frame harmonics should follow 1/h and
    // nothing should appear above harmonic 30.
    test::SignalSpec spec;
    const double f0 = 220.0;
    const auto x = test::saw (f0, spec, 30);

    const auto r = analyseSignal (x, spec.sampleRate);
    REQUIRE (r.report.ok);
    const float* frame = r.wavetable->getFrame (0);
    constexpr int size = dsp::Wavetable::frameSize;

    const double h1 = test::harmonicMagnitude (frame, size, 1);
    REQUIRE (h1 > 0.1);
    for (int h = 2; h <= 30; ++h)
    {
        CAPTURE (h);
        const double ratioDb = 20.0 * std::log10 (test::harmonicMagnitude (frame, size, h) / h1);
        CHECK_THAT (ratioDb, WithinAbs (20.0 * std::log10 (1.0 / h), 0.3));
    }
    for (int h : { 31, 40, 60, 100, 300, 1000 })
    {
        CAPTURE (h);
        CHECK (20.0 * std::log10 (test::harmonicMagnitude (frame, size, h) / h1 + 1e-12) < -70.0);
    }
}

TEST_CASE ("Loop point stays clean for a decaying source", "[analysis][extract]")
{
    // A decaying tone has a level step between one cycle's start and the
    // next. Without loop cleanup that step spreads energy across all
    // harmonics; with it, harmonics above the source's 20 stay far down.
    test::SignalSpec spec;
    spec.decaySeconds = 0.08; // fast decay: ~5% level drop per cycle at 110 Hz
    const auto x = test::saw (110.0, spec, 20);

    analysis::ImportSettings settings;
    settings.cyclesToAverage = 8;
    const auto r = analyseSignal (x, spec.sampleRate, settings);
    REQUIRE (r.report.ok);
    const float* frame = r.wavetable->getFrame (0);

    const double h1 = test::harmonicMagnitude (frame, dsp::Wavetable::frameSize, 1);
    double worst = -200.0;
    for (int h = 25; h <= 400; ++h)
        worst = std::max (worst, 20.0 * std::log10 (test::harmonicMagnitude (frame, dsp::Wavetable::frameSize, h) / h1 + 1e-12));
    CHECK (worst < -50.0);
}

TEST_CASE ("Extracted sine starts at a rising zero crossing", "[analysis][extract]")
{
    test::SignalSpec spec;
    const auto x = test::harmonicSignal (330.0, spec, 1, [] (int) { return 1.0; }, 1.234); // arbitrary start phase

    const auto r = analyseSignal (x, spec.sampleRate);
    REQUIRE (r.report.ok);
    const float* frame = r.wavetable->getFrame (0);
    constexpr int size = dsp::Wavetable::frameSize;

    double dot = 0.0, norm = 0.0;
    for (int i = 0; i < size; ++i)
    {
        const double ref = std::sin (test::twoPi * i / size);
        dot += frame[i] * ref;
        norm += frame[i] * frame[i];
    }
    const double correlation = dot / std::sqrt (norm * size * 0.5);
    CHECK (correlation > 0.9999);
    CHECK_THAT (frame[0], WithinAbs (0.0, 0.01));
    CHECK_THAT (frame[size / 4], WithinAbs (1.0, 0.01)); // normalised peak
}

TEST_CASE ("DC removal and normalisation options", "[analysis][extract]")
{
    test::SignalSpec spec;
    auto x = test::saw (196.0, spec, 25);
    for (auto& v : x)
        v += 0.2f;

    auto frameMean = [] (const dsp::Wavetable& t) {
        const float* f = t.getFrame (0);
        return std::accumulate (f, f + dsp::Wavetable::frameSize, 0.0) / dsp::Wavetable::frameSize;
    };
    auto framePeak = [] (const dsp::Wavetable& t) {
        const float* f = t.getFrame (0);
        float peak = 0.0f;
        for (int i = 0; i < dsp::Wavetable::frameSize; ++i)
            peak = std::max (peak, std::abs (f[i]));
        return peak;
    };

    analysis::ImportSettings settings;
    auto r = analyseSignal (x, spec.sampleRate, settings);
    REQUIRE (r.report.ok);
    CHECK (std::abs (frameMean (*r.wavetable)) < 1e-4);
    CHECK_THAT (framePeak (*r.wavetable), WithinAbs (1.0, 1e-5));

    settings.removeDC = false;
    settings.normalize = false;
    r = analyseSignal (x, spec.sampleRate, settings);
    REQUIRE (r.report.ok);
    CHECK_THAT (frameMean (*r.wavetable), WithinAbs (0.2, 0.01));
}

TEST_CASE ("Region selection limits analysis to part of the source", "[analysis]")
{
    test::SignalSpec half;
    half.seconds = 0.5;
    auto x = test::saw (110.0, half);
    const auto second = test::sine (330.0, half);
    x.insert (x.end(), second.begin(), second.end());

    analysis::ImportSettings settings;
    settings.regionStart = 0.6;
    settings.regionEnd = 1.0;
    const auto r = analyseSignal (x, half.sampleRate, settings);
    REQUIRE (r.report.ok);
    CHECK (std::abs (test::centsBetween (r.report.f0, 330.0)) < 1.0);
    CHECK (r.report.extractStart >= 0.6 * 1.0 - 1e-9);
}

TEST_CASE ("Analysis reports errors for unusable input", "[analysis]")
{
    SECTION ("silence")
    {
        const auto r = analyseSignal (std::vector<float> (48000, 0.0f), 48000.0);
        CHECK_FALSE (r.report.ok);
        CHECK (r.report.error.containsIgnoreCase ("silent"));
    }
    SECTION ("too short")
    {
        test::SignalSpec spec;
        spec.seconds = 0.01;
        const auto r = analyseSignal (test::saw (220.0, spec), spec.sampleRate);
        CHECK_FALSE (r.report.ok);
        CHECK (r.report.error.containsIgnoreCase ("too short"));
    }
    SECTION ("empty")
    {
        const auto r = analyseSignal ({}, 48000.0);
        CHECK_FALSE (r.report.ok);
    }
}
