#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "TestHelpers.h"
#include "TestSignals.h"
#include "dsp/WavetableExchange.h"

#include <juce_dsp/juce_dsp.h>

#include <thread>

using namespace reload;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace
{
constexpr int frameSize = dsp::Wavetable::frameSize;

// One cycle of a band-limited saw with `harmonics` harmonics.
std::vector<float> sawFrame (int harmonics)
{
    std::vector<float> f (frameSize, 0.0f);
    for (int h = 1; h <= harmonics; ++h)
        for (int i = 0; i < frameSize; ++i)
            f[static_cast<size_t> (i)] += static_cast<float> (0.5 / h * std::sin (test::twoPi * h * i / frameSize));
    return f;
}

std::shared_ptr<const dsp::Wavetable> sawTable (int rootNote = 69, double rootHz = 440.0)
{
    dsp::WavetableInfo info;
    info.rootNote = rootNote;
    info.rootFrequency = rootHz;
    return dsp::Wavetable::create (sawFrame (1023), info);
}

// Fraction of spectral energy (dB) that lies away from multiples of f0, below maxHz.
double inharmonicEnergyDb (const std::vector<float>& x, double sampleRate, double f0, double maxHz)
{
    constexpr int order = 15;
    constexpr int n = 1 << order;
    REQUIRE (static_cast<int> (x.size()) >= n);

    std::vector<float> buf (2 * n, 0.0f);
    const auto offset = x.size() - n;
    for (int i = 0; i < n; ++i)
    {
        // Blackman-Harris window: sidelobes < -92 dB.
        const double t = test::twoPi * i / (n - 1);
        const double w = 0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2 * t) - 0.01168 * std::cos (3 * t);
        buf[static_cast<size_t> (i)] = static_cast<float> (x[offset + static_cast<size_t> (i)] * w);
    }
    juce::dsp::FFT fft (order);
    fft.performFrequencyOnlyForwardTransform (buf.data(), true);

    double harmonic = 0.0, other = 0.0;
    const double binHz = sampleRate / n;
    for (int k = 1; k < n / 2; ++k)
    {
        const double hz = k * binHz;
        if (hz > maxHz)
            break;
        const double e = static_cast<double> (buf[static_cast<size_t> (k)]) * buf[static_cast<size_t> (k)];
        const double nearestHarmonic = std::round (hz / f0) * f0;
        (std::abs (hz - nearestHarmonic) <= 6 * binHz ? harmonic : other) += e;
    }
    return 10.0 * std::log10 (other / harmonic + 1e-30);
}
} // namespace

TEST_CASE ("Mip level 0 reproduces a band-limited frame", "[wavetable]")
{
    const auto frame = sawFrame (1000);
    const auto table = dsp::Wavetable::create (frame, {});
    const float* mip = table->getMip (0, 0);
    float maxErr = 0.0f;
    for (int i = 0; i < frameSize; ++i)
        maxErr = std::max (maxErr, std::abs (mip[i] - frame[static_cast<size_t> (i)]));
    CHECK (maxErr < 1e-4f);
    // Wrapped guard samples for 4-point interpolation.
    CHECK_THAT (mip[-1], WithinAbs (mip[frameSize - 1], 0.0));
    CHECK_THAT (mip[frameSize], WithinAbs (mip[0], 0.0));
    CHECK_THAT (mip[frameSize + 1], WithinAbs (mip[1], 0.0));
}

TEST_CASE ("Each mip level is band-limited to its harmonic budget", "[wavetable]")
{
    const auto table = sawTable();
    for (int level = 0; level < dsp::Wavetable::numLevels; ++level)
    {
        CAPTURE (level);
        const float* mip = table->getMip (0, level);
        const int keep = dsp::Wavetable::maxHarmonic (level);
        // Kept harmonics are intact...
        CHECK_THAT (test::harmonicMagnitude (mip, frameSize, 1), WithinRel (0.5, 1e-3));
        CHECK_THAT (test::harmonicMagnitude (mip, frameSize, keep), WithinRel (0.5 / keep, 1e-2));
        // ...and everything above is gone.
        if (keep < 1023)
            CHECK (test::harmonicMagnitude (mip, frameSize, keep + 1) < 1e-5);
    }
}

TEST_CASE ("Mip level choice keeps harmonics under the alias-safe limit", "[wavetable]")
{
    const double sampleRate = GENERATE (32000.0, 44100.0, 48000.0, 96000.0);
    const double f = GENERATE (8.0, 27.5, 110.0, 440.0, 1000.0, 4186.0, 12543.0);
    CAPTURE (sampleRate, f);

    const int level = dsp::Wavetable::chooseLevel (f, sampleRate);
    const double limit = sampleRate < 40000.0 ? 0.5 * sampleRate : std::min (sampleRate - 20000.0, 0.55 * sampleRate);
    if (level < dsp::Wavetable::numLevels - 1)
        CHECK (dsp::Wavetable::maxHarmonic (level) * f <= limit);
    if (level > 0)
        CHECK (dsp::Wavetable::maxHarmonic (level - 1) * f > limit); // not more band-limited than needed
}

TEST_CASE ("High notes from a bright table don't alias", "[wavetable][render]")
{
    const double sampleRate = GENERATE (44100.0, 48000.0);
    const int note = GENERATE (84, 96, 103, 108);
    CAPTURE (sampleRate, note);

    const auto table = sawTable();
    dsp::SynthEngine engine;
    engine.prepare (sampleRate, 512);
    engine.setAmpEnvelope ({ 0.001f, 0.01f, 1.0f, 0.1f });
    engine.setWavetable (table.get());

    const auto out = test::renderEngine (engine, 512, static_cast<int> (sampleRate), { { 0, juce::MidiMessage::noteOn (1, note, 1.0f) } });
    const double f0 = 440.0 * std::pow (2.0, (note - 69) / 12.0);
    CHECK (inharmonicEnergyDb (out.left, sampleRate, f0, 20000.0) < -70.0);
}

TEST_CASE ("Low and mid notes stay clean too", "[wavetable][render]")
{
    // Lower notes use richer mip levels, where linear interpolation images
    // are the main inharmonic component; they must still be well down.
    const int note = GENERATE (24, 36, 48, 60, 72);
    CAPTURE (note);
    constexpr double sampleRate = 48000.0;

    const auto table = sawTable();
    dsp::SynthEngine engine;
    engine.prepare (sampleRate, 512);
    engine.setAmpEnvelope ({ 0.001f, 0.01f, 1.0f, 0.1f });
    engine.setWavetable (table.get());

    const auto out = test::renderEngine (engine, 512, 48000, { { 0, juce::MidiMessage::noteOn (1, note, 1.0f) } });
    const double f0 = 440.0 * std::pow (2.0, (note - 69) / 12.0);
    CHECK (inharmonicEnergyDb (out.left, sampleRate, f0, 20000.0) < -60.0);
}

TEST_CASE ("Root note and source tuning map pitch", "[wavetable][render]")
{
    // A table extracted from a source at 225 Hz: nearest note A3 (57), +39 cents.
    dsp::WavetableInfo info;
    info.rootNote = 57;
    info.rootFrequency = 225.0;
    const auto table = dsp::Wavetable::create (sawFrame (1), info); // pure sine cycle
    constexpr double sampleRate = 48000.0;

    auto measure = [&] (int note, bool sourceTuning) {
        dsp::SynthEngine engine;
        engine.prepare (sampleRate, 256);
        engine.setAmpEnvelope ({ 0.001f, 0.01f, 1.0f, 0.1f });
        engine.setWavetable (table.get());
        engine.setSourceTuning (sourceTuning);
        const auto out = test::renderEngine (engine, 256, 24000, { { 0, juce::MidiMessage::noteOn (1, note, 1.0f) } });
        return test::estimateFrequency (out.left.data() + 2400, 24000 - 2400, sampleRate);
    };

    CHECK_THAT (measure (57, true), WithinAbs (225.0, 0.02));
    CHECK_THAT (measure (69, true), WithinAbs (450.0, 0.02));
    CHECK_THAT (measure (57, false), WithinAbs (220.0, 0.02));
    CHECK_THAT (measure (45, false), WithinAbs (110.0, 0.02));
}

//==============================================================================
TEST_CASE ("WavetableExchange swaps tables and frees retired ones off the audio thread", "[wavetable][realtime]")
{
    dsp::WavetableExchange exchange;
    CHECK (exchange.acquire() == nullptr);

    auto first = sawTable();
    std::weak_ptr<const dsp::Wavetable> firstWeak = first;
    exchange.publish (first);
    CHECK (exchange.acquire() == first.get());
    first.reset();
    CHECK_FALSE (firstWeak.expired()); // still owned by the exchange

    auto second = dsp::Wavetable::createSine();
    exchange.publish (second);
    CHECK (exchange.acquire() == second.get());
    CHECK_FALSE (firstWeak.expired()); // retired, but only freed by collectGarbage
    exchange.collectGarbage();
    CHECK (firstWeak.expired());

    SECTION ("a pending table replaced before the audio thread saw it is freed immediately")
    {
        auto a = sawTable();
        std::weak_ptr<const dsp::Wavetable> aWeak = a;
        exchange.publish (std::move (a));
        exchange.publish (dsp::Wavetable::createSine());
        CHECK (aWeak.expired());
    }
}

TEST_CASE ("WavetableExchange survives concurrent publish/acquire", "[wavetable][realtime]")
{
    dsp::WavetableExchange exchange;
    exchange.publish (dsp::Wavetable::createSine());

    std::atomic<bool> stop { false };
    std::atomic<int> acquired { 0 };
    std::thread audio ([&] {
        double sum = 0.0;
        while (! stop.load())
        {
            if (const auto* t = exchange.acquire())
            {
                const float* mip = t->getMip (0, 3);
                for (int i = 0; i < 64; ++i)
                    sum += mip[i];
                ++acquired;
            }
        }
        CHECK (std::isfinite (sum));
    });

    for (int i = 0; i < 300; ++i)
    {
        exchange.publish (i % 2 == 0 ? dsp::Wavetable::createSine() : sawTable());
        exchange.collectGarbage();
    }
    stop = true;
    audio.join();
    CHECK (acquired.load() > 0);
}
