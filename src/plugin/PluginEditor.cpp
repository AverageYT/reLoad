#include "plugin/PluginEditor.h"

#include "analysis/NoteUtils.h"
#include "gui/Theme.h"
#include "plugin/AudioFileLoader.h"
#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"

namespace reload
{
namespace
{
    constexpr int headerHeight = 44;
    constexpr int keyboardHeight = 72;
    constexpr int knobRowHeight = 118;
    constexpr int margin = 12;

    juce::String firstSupportedFile (const juce::StringArray& files)
    {
        for (const auto& f : files)
            if (supportedAudioExtensions().contains (juce::File (f).getFileExtension().trimCharactersAtStart ("."), true))
                return f;
        return {};
    }
} // namespace

ReloadEditor::ReloadEditor (ReloadProcessor& p)
    : AudioProcessorEditor (p),
      reloadProcessor (p),
      keyboard (p.getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard)
{
    // ---- Import panel ----
    addAndMakeVisible (sourceView);
    sourceView.onRegionChanged = [this] (double, double) { pushSettings(); };

    addAndMakeVisible (frameView);

    addAndMakeVisible (loadButton);
    loadButton.onClick = [this] { openFileChooser(); };

    modeLabel.setText ("Mode", juce::dontSendNotification);
    addAndMakeVisible (modeLabel);
    modeBox.addItem (analysis::modeName (analysis::ImportSettings::Mode::automatic), 1);
    modeBox.addItem (analysis::modeName (analysis::ImportSettings::Mode::singleCycle), 2);
    modeBox.setTooltip ("Auto picks the import mode from the analysis. More modes (evolving, spectral, chord) arrive in M3.");
    modeBox.onChange = [this] { pushSettings(); };
    addAndMakeVisible (modeBox);

    pitchLabel.setText ("Pitch", juce::dontSendNotification);
    addAndMakeVisible (pitchLabel);
    pitchOverride.setTextToShowWhenEmpty ("auto (or A3, 220 Hz)", gui::theme::textDim);
    pitchOverride.setTooltip ("Leave empty to detect the pitch. Type a note (A3) to search near it, or a frequency (220 Hz) to force it.");
    pitchOverride.onReturnKey = [this] { pushSettings(); };
    pitchOverride.onFocusLost = [this] { pushSettings(); };
    addAndMakeVisible (pitchOverride);

    cyclesLabel.setText ("Cycles", juce::dontSendNotification);
    addAndMakeVisible (cyclesLabel);
    cyclesSlider.setRange (1, 16, 1);
    cyclesSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 36, 26);
    cyclesSlider.setTooltip ("How many consecutive cycles are averaged into the wavetable frame (reduces noise).");
    cyclesSlider.onValueChange = [this] { pushSettings(); };
    addAndMakeVisible (cyclesSlider);

    for (auto* b : { &removeDCButton, &normalizeButton })
    {
        b->onClick = [this] { pushSettings(); };
        addAndMakeVisible (*b);
    }
    tuneButton.setTooltip ("On: the root key plays the source's exact pitch, including any cents offset. Off: snap to A440 tuning.");
    tuneAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        p.getParameters(), params::id::sourceTuning, tuneButton);
    addAndMakeVisible (tuneButton);

    infoLabel.setJustificationType (juce::Justification::topLeft);
    infoLabel.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (infoLabel);
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (statusLabel);

    // ---- Amp knobs ----
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

    reloadProcessor.getImporter().addChangeListener (this);
    refreshFromImporter();

    setSize (920, 640);
}

ReloadEditor::~ReloadEditor()
{
    reloadProcessor.getImporter().removeChangeListener (this);
}

void ReloadEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshFromImporter();
}

void ReloadEditor::refreshFromImporter()
{
    auto& importer = reloadProcessor.getImporter();
    const auto settings = importer.getSettings();
    const auto report = importer.getReport();
    const auto source = importer.getSource();
    const auto table = reloadProcessor.getWavetable();

    modeBox.setSelectedId (settings.mode == analysis::ImportSettings::Mode::automatic ? 1 : 2, juce::dontSendNotification);
    if (! pitchOverride.hasKeyboardFocus (true))
        pitchOverride.setText (settings.pitchOverride, false);
    cyclesSlider.setValue (settings.cyclesToAverage, juce::dontSendNotification);
    removeDCButton.setToggleState (settings.removeDC, juce::dontSendNotification);
    normalizeButton.setToggleState (settings.normalize, juce::dontSendNotification);

    sourceView.setSource (source);
    sourceView.setRegion (settings.regionStart, settings.regionEnd);
    sourceView.setExtraction (report.extractStart, report.extractLength);

    const auto path = importer.getSourcePath();
    if (source == nullptr && path.isNotEmpty())
        sourceView.setPlaceholder ("Source file not loaded:\n" + juce::File (path).getFileName()
                                   + "\n(The wavetable was restored from the project. Load the file to re-analyse.)");
    else
        sourceView.setPlaceholder ({});

    frameView.setWavetable (table);

    juce::String info;
    if (source != nullptr)
        info << "File: " << source->name << "  (" << juce::String (source->getLengthSeconds(), 2) << " s, "
             << juce::String (source->sampleRate / 1000.0, 1) << " kHz)\n";
    else if (table != nullptr)
        info << "Table: " << table->getInfo().name << "\n";

    if (report.ok)
    {
        const int root = static_cast<int> (std::lround (analysis::hzToMidi (report.f0)));
        info << "Detected pitch: " << juce::String (report.f0, 2) << " Hz = " << analysis::describePitch (report.f0)
             << (report.pitchWasOverridden ? "  (override)" : "") << "\n"
             << "Root key: " << analysis::noteName (root) << "  (play " << analysis::noteName (root)
             << " to hear the source pitch)\n"
             << "Mode used: " << report.modeUsed << "\n"
             << report.reason;
    }
    else if (table != nullptr)
    {
        info << "Mode: " << table->getInfo().mode << " - " << table->getInfo().reason;
    }
    infoLabel.setText (info, juce::dontSendNotification);

    const auto status = importer.getStatus();
    const bool isError = status.isNotEmpty() && ! importer.isBusy();
    statusLabel.setText (status, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, isError ? gui::theme::error : gui::theme::textDim);
}

void ReloadEditor::pushSettings()
{
    auto settings = reloadProcessor.getImporter().getSettings();
    settings.mode = modeBox.getSelectedId() == 2 ? analysis::ImportSettings::Mode::singleCycle
                                                 : analysis::ImportSettings::Mode::automatic;
    settings.pitchOverride = pitchOverride.getText().trim();
    settings.cyclesToAverage = static_cast<int> (cyclesSlider.getValue());
    settings.removeDC = removeDCButton.getToggleState();
    settings.normalize = normalizeButton.getToggleState();
    settings.regionStart = sourceView.getRegionStart();
    settings.regionEnd = sourceView.getRegionEnd();
    reloadProcessor.getImporter().setSettings (settings);
}

void ReloadEditor::openFileChooser()
{
    chooser = std::make_unique<juce::FileChooser> ("Load a sample", juce::File(),
                                                   "*." + supportedAudioExtensions().joinIntoString (";*."));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc) {
                              const auto file = fc.getResult();
                              if (file.existsAsFile())
                                  reloadProcessor.getImporter().loadFile (file);
                          });
}

bool ReloadEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    return firstSupportedFile (files).isNotEmpty();
}

void ReloadEditor::fileDragEnter (const juce::StringArray&, int, int)
{
    sourceView.setDragHighlight (true);
}

void ReloadEditor::fileDragExit (const juce::StringArray&)
{
    sourceView.setDragHighlight (false);
}

void ReloadEditor::filesDropped (const juce::StringArray& files, int, int)
{
    sourceView.setDragHighlight (false);
    const auto path = firstSupportedFile (files);
    if (path.isNotEmpty())
        reloadProcessor.getImporter().loadFile (juce::File (path));
}

void ReloadEditor::paint (juce::Graphics& g)
{
    g.fillAll (gui::theme::background);

    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (margin, 0);
    g.setColour (gui::theme::text);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("reLoad", header, juce::Justification::centredLeft);

    g.setColour (gui::theme::textDim);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("M2 - sample import, interim UI", header, juce::Justification::centredRight, false);
}

void ReloadEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop (headerHeight);
    keyboard.setBounds (area.removeFromBottom (keyboardHeight));

    auto knobRow = area.removeFromBottom (knobRowHeight).reduced (margin, 6);
    const int knobWidth = knobRow.getWidth() / static_cast<int> (knobs.size());
    for (auto& k : knobs)
    {
        auto cell = knobRow.removeFromLeft (knobWidth);
        k.label.setBounds (cell.removeFromTop (18));
        k.slider.setBounds (cell.reduced (6, 0));
    }

    auto importArea = area.reduced (margin, 4);
    auto right = importArea.removeFromRight (300);
    importArea.removeFromRight (margin);

    // Left: source view, then two rows of controls.
    auto controls2 = importArea.removeFromBottom (28);
    importArea.removeFromBottom (6);
    auto controls1 = importArea.removeFromBottom (28);
    importArea.removeFromBottom (8);
    sourceView.setBounds (importArea);

    loadButton.setBounds (controls1.removeFromLeft (84));
    controls1.removeFromLeft (10);
    modeLabel.setBounds (controls1.removeFromLeft (42));
    modeBox.setBounds (controls1.removeFromLeft (116));
    controls1.removeFromLeft (10);
    pitchLabel.setBounds (controls1.removeFromLeft (38));
    pitchOverride.setBounds (controls1.removeFromLeft (150));
    controls1.removeFromLeft (10);
    cyclesLabel.setBounds (controls1.removeFromLeft (46));
    cyclesSlider.setBounds (controls1.removeFromLeft (88));

    removeDCButton.setBounds (controls2.removeFromLeft (110));
    normalizeButton.setBounds (controls2.removeFromLeft (110));
    tuneButton.setBounds (controls2.removeFromLeft (140));
    statusLabel.setBounds (controls2);

    // Right: frame view and analysis report.
    frameView.setBounds (right.removeFromTop (130));
    right.removeFromTop (8);
    infoLabel.setBounds (right);
}
} // namespace reload
