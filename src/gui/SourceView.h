#pragma once

#include "analysis/Analyzer.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace reload::gui
{
// Overview of the loaded source with draggable region handles and a highlight
// showing where the cycle was extracted.
class SourceView final : public juce::Component
{
public:
    SourceView();

    // Called on mouse-up after the user drags a region handle (fractions 0..1).
    std::function<void (double start, double end)> onRegionChanged;

    void setSource (std::shared_ptr<const analysis::SourceAudio> newSource);
    void setRegion (double start, double end);
    double getRegionStart() const noexcept { return regionStart; }
    double getRegionEnd() const noexcept { return regionEnd; }
    void setExtraction (double startSeconds, double lengthSeconds);
    void setPlaceholder (const juce::String& text);
    void setDragHighlight (bool on);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    enum class Handle { none, start, end };

    float fractionToX (double fraction) const;
    double xToFraction (float x) const;
    Handle handleAt (float x) const;
    void rebuildPeaks();

    std::shared_ptr<const analysis::SourceAudio> source;
    std::vector<std::pair<float, float>> peaks; // min/max per column
    double regionStart = 0.0, regionEnd = 1.0;
    double extractStart = 0.0, extractLength = 0.0;
    juce::String placeholder;
    bool dragHighlight = false;
    Handle dragging = Handle::none;
};
} // namespace reload::gui
