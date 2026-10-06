#include <catch2/catch_test_macros.hpp>

#include "TestSignals.h"
#include "plugin/PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

using namespace reload;

// Hidden test (not run by ctest): renders the editor to a PNG for visual
// review. Usage: RELOAD_SNAPSHOT_DIR=/some/dir reload_tests "[.snapshot]"
TEST_CASE ("Editor snapshot with an imported sample", "[.snapshot]")
{
    const auto dirPath = juce::SystemStats::getEnvironmentVariable ("RELOAD_SNAPSHOT_DIR", {});
    if (dirPath.isEmpty())
        SKIP ("RELOAD_SNAPSHOT_DIR not set");

    juce::ScopedJuceInitialiser_GUI juceInit;
    ReloadProcessor p;

    // A plucked-string-like source: quiet pre-roll, then a decaying G3 saw with noise.
    test::SignalSpec spec;
    spec.seconds = 1.6;
    spec.decaySeconds = 0.5;
    auto tone = test::saw (196.0, spec, 40);
    test::addNoise (tone, 0.004f);
    std::vector<float> samples (4800, 0.0f);
    samples.insert (samples.end(), tone.begin(), tone.end());

    auto source = std::make_shared<analysis::SourceAudio>();
    source->samples = std::move (samples);
    source->sampleRate = spec.sampleRate;
    source->name = "pluck_G3.wav";
    source->path = "/tmp/pluck_G3.wav";

    const auto result = analysis::analyse (*source, {});
    REQUIRE (result.report.ok);
    p.getImporter().applyResult (result, source);

    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);

    const auto file = juce::File (dirPath).getChildFile ("editor.png");
    file.deleteFile();
    juce::FileOutputStream out (file);
    REQUIRE (out.openedOk());
    REQUIRE (juce::PNGImageFormat().writeImageToStream (image, out));
}
