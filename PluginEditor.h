#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
class HyperareaAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                            private juce::Timer
{
public:
    explicit HyperareaAudioProcessorEditor (HyperareaAudioProcessor&);
    ~HyperareaAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override {}

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    void drawBackdrop (juce::Graphics&);
    void drawEq (juce::Graphics&);
    void drawTitle (juce::Graphics&);
    void drawMix (juce::Graphics&);
    void drawSmallKnob (juce::Graphics&, juce::Point<float>, float radius,
                        float value01, const juce::String& label, const juce::String& value);
    void drawBar (juce::Graphics&);
    void drawKeyboard (juce::Graphics&);

    //--- geometry -----------------------------------------------------------
    juce::Rectangle<int> eqBounds() const;
    juce::Rectangle<int> barBounds() const;
    juce::Point<float>   mixCentre() const;
    juce::Point<float>   outCentre() const;
    juce::Point<float>   sftCentre() const;
    juce::Rectangle<int> rootSlot() const;
    juce::Rectangle<int> scaleSlot() const;
    juce::Rectangle<int> octSlot() const;
    juce::Point<float>   keyCentre (int degree) const;
    float                keyRadius (int degree) const;

    juce::Point<float> eqPoint (int band) const;
    juce::Rectangle<float> eqPlot() const;

    //--- params -------------------------------------------------------------
    float value (const char* id) const;
    void  setValue (const char* id, float v);
    bool  keyInScale (int midi) const;

    void showRootMenu();
    void showScaleMenu();
    void showOctMenu();

    HyperareaAudioProcessor& processorRef;

    int   dragMode = 0;          // 1-4 eq band, 5 mix, 6 out, 7 sft, 8 keyboard
    int   dragDegree = 0;
    float dragStart = 0.0f;

    float peakDb = -90.0f;
    float phase = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HyperareaAudioProcessorEditor)
};
