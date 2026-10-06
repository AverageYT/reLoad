#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <array>

namespace reload
{
class ReloadProcessor;

// M1 placeholder UI: a few knobs and an on-screen keyboard. The styled
// layout arrives in M5.
class ReloadEditor final : public juce::AudioProcessorEditor
{
public:
    explicit ReloadEditor (ReloadProcessor&);
    ~ReloadEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    std::array<Knob, 5> knobs;
    juce::MidiKeyboardComponent keyboard;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReloadEditor)
};
} // namespace reload
