#include "plugin/PluginEditor.h"

#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"

namespace reload
{
namespace
{
    constexpr int headerHeight = 44;
    constexpr int keyboardHeight = 80;
} // namespace

ReloadEditor::ReloadEditor (ReloadProcessor& p)
    : AudioProcessorEditor (p),
      keyboard (p.getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard)
{
    const std::array<std::pair<const char*, const char*>, 5> knobSpecs { {
        { params::id::ampAttack, "Attack" },
        { params::id::ampDecay, "Decay" },
        { params::id::ampSustain, "Sustain" },
        { params::id::ampRelease, "Release" },
        { params::id::masterGain, "Master" },
    } };

    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& k = knobs[i];
        k.label.setText (knobSpecs[i].second, juce::dontSendNotification);
        k.label.setJustificationType (juce::Justification::centred);
        k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 20);
        addAndMakeVisible (k.slider);
        addAndMakeVisible (k.label);
        k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            p.getParameters(), knobSpecs[i].first, k.slider);
    }

    keyboard.setAvailableRange (24, 108);
    keyboard.setOctaveForMiddleC (4);
    addAndMakeVisible (keyboard);

    setSize (640, 300);
}

ReloadEditor::~ReloadEditor() = default;

void ReloadEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff15171c));

    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (14, 0);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("reLoad", header, juce::Justification::centredLeft);

    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("M1 skeleton - sine oscillator", header, juce::Justification::centredRight);
}

void ReloadEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop (headerHeight);
    keyboard.setBounds (area.removeFromBottom (keyboardHeight));

    auto row = area.reduced (14, 8);
    const int knobWidth = row.getWidth() / static_cast<int> (knobs.size());
    for (auto& k : knobs)
    {
        auto cell = row.removeFromLeft (knobWidth);
        k.label.setBounds (cell.removeFromTop (20));
        k.slider.setBounds (cell.reduced (6, 0));
    }
}
} // namespace reload
