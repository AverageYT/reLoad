#pragma once

#include "dsp/Wavetable.h"
#include "gui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace reload::gui
{
// Draws one frame of the current wavetable.
class FrameView final : public juce::Component
{
public:
    void setWavetable (std::shared_ptr<const dsp::Wavetable> newTable)
    {
        table = std::move (newTable);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour (theme::panelBackground);
        g.fillRoundedRectangle (bounds, 4.0f);

        g.setColour (theme::textDim.withAlpha (0.4f));
        g.drawHorizontalLine (static_cast<int> (bounds.getCentreY()), bounds.getX(), bounds.getRight());

        if (table == nullptr)
            return;

        const auto area = bounds.reduced (6.0f, 10.0f);
        const float* frame = table->getFrame (0);
        juce::Path path;
        const int points = std::max (2, static_cast<int> (area.getWidth()));
        for (int i = 0; i < points; ++i)
        {
            const int index = i * (dsp::Wavetable::frameSize - 1) / (points - 1);
            const float x = area.getX() + static_cast<float> (i);
            const float y = area.getCentreY() - juce::jlimit (-1.0f, 1.0f, frame[index]) * area.getHeight() * 0.5f;
            if (i == 0)
                path.startNewSubPath (x, y);
            else
                path.lineTo (x, y);
        }
        g.setColour (theme::accent);
        g.strokePath (path, juce::PathStrokeType (1.6f));

        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (juce::String (table->getNumFrames()) + (table->getNumFrames() == 1 ? " frame" : " frames") + " x 2048",
                    getLocalBounds().reduced (6, 4), juce::Justification::topRight);
    }

private:
    std::shared_ptr<const dsp::Wavetable> table;
};
} // namespace reload::gui
