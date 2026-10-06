#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "TestHelpers.h"
#include "TestSignals.h"
#include "plugin/AudioFileLoader.h"
#include "plugin/PluginProcessor.h"

using namespace reload;
using Catch::Matchers::WithinAbs;

namespace
{
// Writes a synthetic signal to an audio file in a temp folder (deleted on scope exit).
struct TempAudioFile
{
    TempAudioFile (const std::vector<float>& samples, double sampleRate, const juce::String& extension, int channels = 1)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        auto* format = formats.findFormatForFileExtension (extension);
        REQUIRE (format != nullptr);

        REQUIRE (dir.createDirectory());
        file = dir.getChildFile ("source." + extension);
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);

        const auto options = juce::AudioFormatWriterOptions()
                                 .withSampleRate (sampleRate)
                                 .withNumChannels (channels)
                                 .withBitsPerSample (extension == "ogg" ? 16 : 24)
                                 .withQualityOptionIndex (extension == "ogg" ? format->getQualityOptions().size() - 1 : 0);
        auto writer = format->createWriterFor (stream, options);
        REQUIRE (writer != nullptr);

        juce::AudioBuffer<float> buffer (channels, static_cast<int> (samples.size()));
        for (int ch = 0; ch < channels; ++ch)
            buffer.copyFrom (ch, 0, samples.data(), static_cast<int> (samples.size()));
        REQUIRE (writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples()));
    }

    juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getNonexistentChildFile ("reload-test", {}, false);
    juce::File file;

    ~TempAudioFile() { dir.deleteRecursively(); }
};

double renderPitch (ReloadProcessor& p, int note, double sampleRate = 48000.0)
{
    p.prepareToPlay (sampleRate, 512);
    juce::AudioBuffer<float> block (2, 512);
    std::vector<float> out;
    for (int b = 0; b < 60; ++b)
    {
        juce::MidiBuffer midi;
        if (b == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, note, 1.0f), 0);
        p.processBlock (block, midi);
        out.insert (out.end(), block.getReadPointer (0), block.getReadPointer (0) + 512);
    }
    p.releaseResources();
    return test::estimateFrequency (out.data() + 4800, static_cast<int> (out.size()) - 4800, sampleRate);
}

analysis::AnalysisResult importFile (const juce::File& file, analysis::ImportSettings settings = {})
{
    juce::String error;
    const auto source = loadAudioFile (file, error);
    INFO (error);
    REQUIRE (source != nullptr);
    return analysis::analyse (*source, settings);
}
} // namespace

TEST_CASE ("Imported files of every writable format play at the source pitch", "[import][e2e]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const juce::String extension = GENERATE ("wav", "aiff", "flac", "ogg");
    const double sourceRate = GENERATE (44100.0, 48000.0);
    CAPTURE (extension, sourceRate);

    // G3 sine with a slight detune (+7 cents) so source tuning is exercised.
    // A sine source keeps the output's zero crossings unambiguous for the pitch check.
    const double f0 = 196.0 * std::pow (2.0, 7.0 / 1200.0);
    test::SignalSpec spec;
    spec.sampleRate = sourceRate;
    spec.seconds = 1.0;
    const TempAudioFile wav (test::sine (f0, spec), sourceRate, extension, 2);

    const auto result = importFile (wav.file);
    REQUIRE (result.report.ok);
    CHECK (std::abs (test::centsBetween (result.report.f0, f0)) < (extension == "ogg" ? 2.0 : 0.5));
    CHECK (result.wavetable->getInfo().rootNote == 55); // G3

    ReloadProcessor p;
    p.getImporter().applyResult (result, nullptr);
    REQUIRE (p.getWavetable() == result.wavetable);

    // Playing the root key reproduces the source pitch; an octave up doubles it.
    // Playback is always at 48 kHz, so the 44.1 kHz case also checks rate independence.
    CHECK_THAT (renderPitch (p, 55), WithinAbs (result.report.f0, 0.05));
    CHECK_THAT (renderPitch (p, 67), WithinAbs (2.0 * result.report.f0, 0.1));
}

TEST_CASE ("Stereo files are mono-summed on load", "[import]")
{
    const double f0 = 261.63;
    test::SignalSpec spec;
    const TempAudioFile wav (test::saw (f0, spec), spec.sampleRate, "wav", 2);

    juce::String error;
    const auto source = loadAudioFile (wav.file, error);
    REQUIRE (source != nullptr);
    CHECK (source->samples.size() == static_cast<size_t> (spec.sampleRate));
    CHECK_THAT (source->sampleRate, WithinAbs (spec.sampleRate, 0.0));
    CHECK (source->name == "source.wav");
}

TEST_CASE ("Unreadable files fail cleanly", "[import]")
{
    juce::TemporaryFile temp (".wav");
    temp.getFile().replaceWithText ("this is not audio");
    juce::String error;
    CHECK (loadAudioFile (temp.getFile(), error) == nullptr);
    CHECK (error.isNotEmpty());

    CHECK (loadAudioFile (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("does-not-exist.wav"), error) == nullptr);
    CHECK (error.containsIgnoreCase ("not found"));
}

TEST_CASE ("Projects restore the wavetable without the original audio file", "[import][state][e2e]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    test::SignalSpec spec;
    const double f0 = 146.83; // D3
    juce::MemoryBlock saved;
    std::vector<float> originalFrame;
    analysis::AnalysisReport originalReport;

    {
        auto wav = std::make_unique<TempAudioFile> (test::saw (f0, spec, 40), spec.sampleRate, "wav");
        analysis::ImportSettings settings;
        settings.cyclesToAverage = 6;
        settings.pitchOverride = "D3";
        auto result = importFile (wav->file, settings);
        REQUIRE (result.report.ok);

        ReloadProcessor p;
        p.getImporter().setSettings (settings); // no source yet: stores settings only
        p.getImporter().applyResult (result, nullptr);
        originalFrame.assign (p.getWavetable()->getFrame (0), p.getWavetable()->getFrame (0) + dsp::Wavetable::frameSize);
        originalReport = p.getImporter().getReport();
        p.getStateInformation (saved);

        wav.reset(); // the original file is gone
    }

    ReloadProcessor restored;
    restored.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));

    const auto table = restored.getWavetable();
    REQUIRE (table != nullptr);
    REQUIRE (table->getNumFrames() == 1);
    CHECK (std::equal (originalFrame.begin(), originalFrame.end(), table->getFrame (0)));
    CHECK_THAT (table->getInfo().rootFrequency, WithinAbs (originalReport.f0, 1e-9));
    CHECK (table->getInfo().rootNote == 50);

    const auto settings = restored.getImporter().getSettings();
    CHECK (settings.cyclesToAverage == 6);
    CHECK (settings.pitchOverride == "D3");
    CHECK (restored.getImporter().getReport().ok);
    CHECK_THAT (restored.getImporter().getReport().f0, WithinAbs (originalReport.f0, 1e-9));
    CHECK (restored.getImporter().getSource() == nullptr);
}

TEST_CASE ("A failed analysis keeps the previous wavetable", "[import]")
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    ReloadProcessor p;
    const auto before = p.getWavetable();

    analysis::AnalysisResult failed;
    failed.report.error = "The selected region is silent.";
    p.getImporter().applyResult (failed, nullptr);

    CHECK (p.getWavetable() == before);
    CHECK (p.getImporter().getStatus() == failed.report.error);
}
