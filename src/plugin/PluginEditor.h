#pragma once

#include "gui/FrameView.h"
#include "gui/SourceView.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <array>

namespace reload
{
class ReloadProcessor;

// Interim functional UI: import panel (load/drop, region, pitch override,
// options, analysis report), a frame view, amp knobs and a keyboard. The
// styled layout arrives in M5.
class ReloadEditor final : public juce::AudioProcessorEditor,
                           public juce::FileDragAndDropTarget,
                           private juce::ChangeListener
{
public:
    explicit ReloadEditor (ReloadProcessor&);
    ~ReloadEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refreshFromImporter();
    void pushSettings();
    void openFileChooser();

    ReloadProcessor& reloadProcessor;

    gui::SourceView sourceView;
    gui::FrameView frameView;
    juce::TextButton loadButton { "Load..." };
    juce::ComboBox modeBox;
    juce::Label modeLabel, pitchLabel, cyclesLabel;
    juce::TextEditor pitchOverride;
    juce::Slider cyclesSlider { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::ToggleButton removeDCButton { "Remove DC" }, normalizeButton { "Normalize" }, tuneButton { "Tune to source" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> tuneAttachment;
    juce::Label infoLabel, statusLabel;

    std::array<Knob, 5> knobs;
    juce::MidiKeyboardComponent keyboard;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReloadEditor)
};
} // namespace reload
