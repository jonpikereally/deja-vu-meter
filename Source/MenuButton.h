#pragma once

#include <JuceHeader.h>

//==============================================================================
// A TextButton that draws a small downward "disclosure" triangle at its right
// edge, signalling that clicking opens a menu.
//==============================================================================
class MenuButton : public juce::TextButton
{
public:
    using juce::TextButton::TextButton;

    void paintButton (juce::Graphics& g, bool shouldDrawButtonAsHighlighted,
                      bool shouldDrawButtonAsDown) override
    {
        juce::TextButton::paintButton (g, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);

        auto r = getLocalBounds().toFloat();
        const float cx = r.getRight() - 11.0f;
        const float cy = r.getCentreY();
        const float w  = 4.0f;   // half-width

        juce::Path tri;
        tri.addTriangle (cx - w, cy - 2.0f, cx + w, cy - 2.0f, cx, cy + 3.0f);
        g.setColour (findColour (juce::TextButton::textColourOffId).withAlpha (0.85f));
        g.fillPath (tri);
    }
};
