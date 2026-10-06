#include "gui/SourceView.h"

#include "gui/Theme.h"

namespace reload::gui
{
namespace
{
    constexpr float handleGrabPixels = 8.0f;
    constexpr double minRegion = 0.005;
} // namespace

SourceView::SourceView()
{
    setRepaintsOnMouseActivity (false);
}

void SourceView::setSource (std::shared_ptr<const analysis::SourceAudio> newSource)
{
    if (newSource == source)
        return;
    source = std::move (newSource);
    rebuildPeaks();
    repaint();
}

void SourceView::setRegion (double start, double end)
{
    if (dragging != Handle::none)
        return; // don't fight the user's drag
    regionStart = start;
    regionEnd = end;
    repaint();
}

void SourceView::setExtraction (double startSeconds, double lengthSeconds)
{
    extractStart = startSeconds;
    extractLength = lengthSeconds;
    repaint();
}

void SourceView::setPlaceholder (const juce::String& text)
{
    placeholder = text;
    repaint();
}

void SourceView::setDragHighlight (bool on)
{
    dragHighlight = on;
    repaint();
}

void SourceView::resized()
{
    rebuildPeaks();
}

void SourceView::rebuildPeaks()
{
    peaks.clear();
    const int columns = std::max (1, getWidth());
    if (source == nullptr || source->samples.empty())
        return;

    const auto& s = source->samples;
    const double perColumn = static_cast<double> (s.size()) / columns;
    peaks.resize (static_cast<size_t> (columns));
    for (int c = 0; c < columns; ++c)
    {
        const auto begin = static_cast<size_t> (c * perColumn);
        const auto end = std::max (begin + 1, std::min (s.size(), static_cast<size_t> ((c + 1) * perColumn)));
        float lo = 0.0f, hi = 0.0f;
        for (auto i = begin; i < end; ++i)
        {
            lo = std::min (lo, s[i]);
            hi = std::max (hi, s[i]);
        }
        peaks[static_cast<size_t> (c)] = { lo, hi };
    }
}

float SourceView::fractionToX (double fraction) const
{
    return static_cast<float> (fraction * getWidth());
}

double SourceView::xToFraction (float x) const
{
    return juce::jlimit (0.0, 1.0, static_cast<double> (x) / std::max (1, getWidth()));
}

SourceView::Handle SourceView::handleAt (float x) const
{
    if (source == nullptr)
        return Handle::none;
    const float ds = std::abs (x - fractionToX (regionStart));
    const float de = std::abs (x - fractionToX (regionEnd));
    if (std::min (ds, de) > handleGrabPixels)
        return Handle::none;
    return ds <= de ? Handle::start : Handle::end;
}

void SourceView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (theme::panelBackground);
    g.fillRoundedRectangle (bounds, 4.0f);

    if (source == nullptr || peaks.empty())
    {
        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (14.0f));
        g.drawFittedText (placeholder.isNotEmpty() ? placeholder : juce::String ("Drop an audio file here, or click Load"),
                          getLocalBounds().reduced (10), juce::Justification::centred, 3);
    }
    else
    {
        const float mid = bounds.getCentreY();
        const float scale = bounds.getHeight() * 0.45f;

        // Extraction highlight.
        const double length = source->getLengthSeconds();
        if (extractLength > 0.0 && length > 0.0)
        {
            const float x0 = fractionToX (extractStart / length);
            const float x1 = std::max (x0 + 2.0f, fractionToX ((extractStart + extractLength) / length));
            g.setColour (theme::accent.withAlpha (0.35f));
            g.fillRect (juce::Rectangle<float> (x0, bounds.getY(), x1 - x0, bounds.getHeight()));
        }

        g.setColour (theme::waveform);
        for (size_t c = 0; c < peaks.size(); ++c)
        {
            const auto [lo, hi] = peaks[c];
            const float x = static_cast<float> (c);
            g.drawVerticalLine (static_cast<int> (x), mid - hi * scale, mid - lo * scale + 1.0f);
        }

        // Dim outside the region.
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        const float xs = fractionToX (regionStart), xe = fractionToX (regionEnd);
        g.fillRect (juce::Rectangle<float> (0.0f, 0.0f, xs, bounds.getHeight()));
        g.fillRect (juce::Rectangle<float> (xe, 0.0f, bounds.getWidth() - xe, bounds.getHeight()));

        g.setColour (theme::handle);
        for (const float x : { xs, xe })
        {
            g.fillRect (juce::Rectangle<float> (x - 1.0f, 0.0f, 2.0f, bounds.getHeight()));
            g.fillRect (juce::Rectangle<float> (x - 4.0f, 0.0f, 8.0f, 6.0f));
        }
    }

    if (dragHighlight)
    {
        g.setColour (theme::accent);
        g.drawRoundedRectangle (bounds.reduced (1.0f), 4.0f, 2.0f);
    }
}

void SourceView::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (handleAt (e.position.x) != Handle::none ? juce::MouseCursor::LeftRightResizeCursor
                                                            : juce::MouseCursor::NormalCursor);
}

void SourceView::mouseDown (const juce::MouseEvent& e)
{
    dragging = handleAt (e.position.x);
}

void SourceView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging == Handle::none)
        return;
    const double f = xToFraction (e.position.x);
    if (dragging == Handle::start)
        regionStart = juce::jlimit (0.0, regionEnd - minRegion, f);
    else
        regionEnd = juce::jlimit (regionStart + minRegion, 1.0, f);
    repaint();
}

void SourceView::mouseUp (const juce::MouseEvent&)
{
    if (dragging != Handle::none && onRegionChanged != nullptr)
    {
        dragging = Handle::none;
        onRegionChanged (regionStart, regionEnd);
    }
    dragging = Handle::none;
}
} // namespace reload::gui
