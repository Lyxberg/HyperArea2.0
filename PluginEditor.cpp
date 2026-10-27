#include "PluginEditor.h"

#include <complex>

namespace
{
    const juce::Colour kBgTop   { 0xff141419 };
    const juce::Colour kBgBot   { 0xff0a0a0d };
    const juce::Colour kPanel   { 0xff202027 };
    const juce::Colour kPanelHi { 0xff2a2a33 };
    const juce::Colour kBorder  { 0xff34343f };
    const juce::Colour kAccent  { 0xff9b8cff };
    const juce::Colour kAccentHi{ 0xffd6cfff };
    const juce::Colour kText    { 0xfff2f2f5 };
    const juce::Colour kDim     { 0xff86868f };
    const juce::Colour kBar     { 0xff2b2b33 };

    constexpr int kWidth  = 440;
    constexpr int kHeight = 555;

    struct PeakCoef { double b0, b1, b2, a1, a2; };

    PeakCoef makePeak (double sr, double f, double q, double gainDb)
    {
        f = juce::jlimit (20.0, sr * 0.45, f);

        const double A     = std::pow (10.0, gainDb / 40.0);
        const double w0    = 2.0 * juce::MathConstants<double>::pi * f / sr;
        const double alpha = std::sin (w0) / (2.0 * q);
        const double a0    = 1.0 + alpha / A;

        PeakCoef c;
        c.b0 = (1.0 + alpha * A) / a0;
        c.b1 = (-2.0 * std::cos (w0)) / a0;
        c.b2 = (1.0 - alpha * A) / a0;
        c.a1 = c.b1;
        c.a2 = (1.0 - alpha / A) / a0;
        return c;
    }

    PeakCoef makeHighPass (double sr, double f, double q)
    {
        f = juce::jlimit (10.0, sr * 0.45, f);

        const double w0    = 2.0 * juce::MathConstants<double>::pi * f / sr;
        const double cw    = std::cos (w0);
        const double alpha = std::sin (w0) / (2.0 * q);
        const double a0    = 1.0 + alpha;

        PeakCoef c;
        c.b0 = (1.0 + cw) * 0.5 / a0;
        c.b1 = -(1.0 + cw) / a0;
        c.b2 = c.b0;
        c.a1 = -2.0 * cw / a0;
        c.a2 = (1.0 - alpha) / a0;
        return c;
    }

    PeakCoef makeLowPass (double sr, double f, double q)
    {
        f = juce::jlimit (10.0, sr * 0.45, f);

        const double w0    = 2.0 * juce::MathConstants<double>::pi * f / sr;
        const double cw    = std::cos (w0);
        const double alpha = std::sin (w0) / (2.0 * q);
        const double a0    = 1.0 + alpha;

        PeakCoef c;
        c.b0 = (1.0 - cw) * 0.5 / a0;
        c.b1 = (1.0 - cw) / a0;
        c.b2 = c.b0;
        c.a1 = -2.0 * cw / a0;
        c.a2 = (1.0 - alpha) / a0;
        return c;
    }

    PeakCoef makeHighShelf (double sr, double f, double gainDb)
    {
        f = juce::jlimit (20.0, sr * 0.45, f);

        const double A  = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * juce::MathConstants<double>::pi * f / sr;
        const double cw = std::cos (w0);
        const double alpha = std::sin (w0) * 0.5 * juce::MathConstants<double>::sqrt2;
        const double a0 = (A + 1.0) - (A - 1.0) * cw + 2.0 * std::sqrt (A) * alpha;

        PeakCoef c;
        c.b0 = A * ((A + 1.0) + (A - 1.0) * cw + 2.0 * std::sqrt (A) * alpha) / a0;
        c.b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cw) / a0;
        c.b2 = A * ((A + 1.0) + (A - 1.0) * cw - 2.0 * std::sqrt (A) * alpha) / a0;
        c.a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cw) / a0;
        c.a2 = ((A + 1.0) - (A - 1.0) * cw - 2.0 * std::sqrt (A) * alpha) / a0;
        return c;
    }

    double peakMag (const PeakCoef& c, double w)
    {
        const std::complex<double> z1 = std::polar (1.0, -w);
        const std::complex<double> z2 = z1 * z1;

        const std::complex<double> num = c.b0 + c.b1 * z1 + c.b2 * z2;
        const std::complex<double> den = 1.0 + c.a1 * z1 + c.a2 * z2;

        return std::abs (num / den);
    }

    // layered stroke = cheap bloom
    void glowStroke (juce::Graphics& g, const juce::Path& p, juce::Colour c, float width, float intensity)
    {
        const float w = juce::jmax (0.5f, width);

        g.setColour (c.withAlpha (0.05f * intensity));
        g.strokePath (p, juce::PathStrokeType (w * 4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (c.withAlpha (0.10f * intensity));
        g.strokePath (p, juce::PathStrokeType (w * 2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (c.withAlpha (0.24f * intensity));
        g.strokePath (p, juce::PathStrokeType (w * 1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // white keys bottom row / black keys top row, like a piano
    const int kWhiteSemis[7] { 0, 2, 4, 5, 7, 9, 11 };
    const int kBlackSemis[5] { 1, 3, 6, 8, 10 };
    const int kBlackAfter[5] { 0, 1, 3, 4, 5 };   // index of the white key to its left
}

//==============================================================================
HyperareaAudioProcessorEditor::HyperareaAudioProcessorEditor (HyperareaAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    setResizable (false, false);
    setSize (kWidth, kHeight);
    startTimerHz (60);
}

void HyperareaAudioProcessorEditor::timerCallback()
{
    phase += 1.0f / 60.0f;
    repaint();
}

//==============================================================================
juce::Rectangle<int> HyperareaAudioProcessorEditor::eqBounds() const   { return { 12, 12, 416, 126 }; }
juce::Rectangle<int> HyperareaAudioProcessorEditor::barBounds() const  { return { 0, 392, kWidth, 32 }; }
juce::Point<float>   HyperareaAudioProcessorEditor::mixCentre() const  { return { 186.0f, 292.0f }; }
juce::Point<float>   HyperareaAudioProcessorEditor::outCentre() const  { return { 386.0f, 218.0f }; }
juce::Point<float>   HyperareaAudioProcessorEditor::sftCentre() const  { return { 386.0f, 322.0f }; }

juce::Rectangle<int> HyperareaAudioProcessorEditor::rootSlot() const  { return { 62, 394, 104, 28 }; }
juce::Rectangle<int> HyperareaAudioProcessorEditor::scaleSlot() const { return { 176, 394, 150, 28 }; }
juce::Rectangle<int> HyperareaAudioProcessorEditor::octSlot() const   { return { 336, 394, 90, 28 }; }

juce::Rectangle<float> HyperareaAudioProcessorEditor::eqPlot() const
{
    return eqBounds().toFloat().reduced (14.0f, 16.0f);
}

juce::Point<float> HyperareaAudioProcessorEditor::keyCentre (int degree) const
{
    for (int i = 0; i < 7; ++i)
        if (kWhiteSemis[i] == degree)
            return { 34.0f + (float) i * 62.0f, 498.0f };

    for (int i = 0; i < 5; ++i)
        if (kBlackSemis[i] == degree)
        {
            const float leftWhite = 34.0f + (float) kBlackAfter[i] * 62.0f;
            return { leftWhite + 31.0f, 452.0f };
        }

    return { 0.0f, 0.0f };
}

float HyperareaAudioProcessorEditor::keyRadius (int degree) const
{
    for (int i = 0; i < 7; ++i)
        if (kWhiteSemis[i] == degree)
            return 22.0f;

    return 17.0f;
}

//==============================================================================
float HyperareaAudioProcessorEditor::value (const char* id) const
{
    return processorRef.apvts.getRawParameterValue (id)->load();
}

void HyperareaAudioProcessorEditor::setValue (const char* id, float v)
{
    if (auto* p = processorRef.apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (v));
}

bool HyperareaAudioProcessorEditor::keyInScale (int midi) const
{
    const int root  = (int) value ("root");
    const int scale = (int) value ("scale");
    const int deg   = ((midi - root) % 12 + 12) % 12;

    if (scale == 8)
        return value (("deg" + juce::String (deg)).toRawUTF8()) > 0.5f;

    return HyperareaAudioProcessor::isInScale (midi, root, scale);
}

//==============================================================================
juce::Point<float> HyperareaAudioProcessorEditor::eqPoint (int band) const
{
    auto r = eqPlot();

    const float startF = value ("eq1f");
    const float lpF    = value ("eq4f");

    float f = 1000.0f, g = 0.0f;

    if (band == 0)      { f = startF; g = 0.0f; }
    else if (band == 3) { f = lpF;    g = value ("eq4g"); }
    else
    {
        f = value (band == 1 ? "eq2f" : "eq3f");
        g = value (band == 1 ? "eq2g" : "eq3g");
    }

    const float u = std::log (juce::jlimit (20.0f, 20000.0f, f) / 20.0f) / std::log (1000.0f);
    const float y = r.getCentreY() - juce::jlimit (-18.0f, 18.0f, g) / 18.0f * r.getHeight() * 0.45f;

    return { r.getX() + u * r.getWidth(), y };
}

//==============================================================================
void HyperareaAudioProcessorEditor::paint (juce::Graphics& g)
{
    drawBackdrop (g);
    drawEq (g);
    drawTitle (g);
    drawMix (g);

    drawSmallKnob (g, outCentre(), 20.0f, (value ("output") + 24.0f) / 48.0f, "OUT",
                   juce::String (value ("output"), 1) + " dB");
    drawSmallKnob (g, sftCentre(), 20.0f, (value ("shift") + 24.0f) / 48.0f, "SFT",
                   juce::String ((int) value ("shift")) + " st");

    drawBar (g);
    drawKeyboard (g);
}

//==============================================================================
void HyperareaAudioProcessorEditor::drawBackdrop (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();

    g.setGradientFill (juce::ColourGradient (kBgTop, 0.0f, 0.0f, kBgBot, 0.0f, b.getBottom(), false));
    g.fillAll();

    const float lvl = juce::jlimit (0.0f, 1.0f, processorRef.getLevel());
    const float breathe = 0.5f + 0.5f * std::sin (phase * 1.6f);
    const auto c = mixCentre();

    // slow breathing halo + a tight one that follows the audio
    juce::ColourGradient halo (kAccent.withAlpha (0.10f + 0.06f * breathe + 0.16f * lvl), c.x, c.y,
                               juce::Colours::transparentBlack, c.x, c.y + 235.0f, true);
    g.setGradientFill (halo);
    g.fillRect (b);

    juce::ColourGradient tight (kAccentHi.withAlpha (0.05f + 0.12f * lvl), c.x, c.y,
                                juce::Colours::transparentBlack, c.x, c.y + 120.0f, true);
    g.setGradientFill (tight);
    g.fillRect (b);
}

//==============================================================================
void HyperareaAudioProcessorEditor::drawTitle (juce::Graphics& g)
{
    const auto area = juce::Rectangle<int> (16, 142, 260, 30);

    // soft glow, then the word itself
    g.setFont (juce::Font (24.0f, juce::Font::bold));

    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy)
        {
            if (dx == 0 && dy == 0)
                continue;

            g.setColour (kAccent.withAlpha (0.16f));
            g.drawText ("HyperArea", area.translated (dx, dy), juce::Justification::centredLeft, false);
        }

    g.setColour (kText);
    g.drawText ("HyperArea", area, juce::Justification::centredLeft, false);
}

//==============================================================================
void HyperareaAudioProcessorEditor::drawEq (juce::Graphics& g)
{
    auto panel = eqBounds();
    auto pf = panel.toFloat();

    g.setColour (kPanel);
    g.fillRoundedRectangle (pf, 12.0f);

    g.setColour (kBorder);
    g.drawRoundedRectangle (pf.reduced (0.5f), 12.0f, 1.0f);

    auto r = eqPlot();

    // grid
    g.setColour (juce::Colours::white.withAlpha (0.05f));

    for (int i = 1; i < 4; ++i)
        g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * (float) i / 4.0f), r.getX(), r.getRight());

    for (int i = 1; i < 8; ++i)
        g.drawVerticalLine ((int) (r.getX() + r.getWidth() * (float) i / 8.0f), r.getY(), r.getBottom());

    g.setColour (juce::Colours::white.withAlpha (0.09f));
    g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());

    // response
    const double sr = processorRef.getSampleRate() > 0.0 ? processorRef.getSampleRate() : 48000.0;

    PeakCoef coef[4];
    coef[0] = makeHighPass (sr, value ("eq1f"), 0.707);
    coef[1] = makePeak (sr, value ("eq2f"), 0.9, value ("eq2g"));
    coef[2] = makePeak (sr, value ("eq3f"), 0.9, value ("eq3g"));
    coef[3] = makeHighShelf (sr, value ("eq4f"), value ("eq4g"));

    juce::Path curve;
    const int w = (int) r.getWidth();

    for (int px = 0; px < w; ++px)
    {
        const float f = 20.0f * std::pow (1000.0f, (float) px / (float) juce::jmax (1, w - 1));
        const double omega = 2.0 * juce::MathConstants<double>::pi * f / sr;

        double mag = 1.0;

        for (int i = 0; i < 4; ++i)
            mag *= peakMag (coef[i], omega);

        const float db = (float) (20.0 * std::log10 (juce::jmax (1.0e-6, mag)));
        const float y = r.getCentreY() - juce::jlimit (-18.0f, 18.0f, db) / 18.0f * r.getHeight() * 0.45f;
        const float x = r.getX() + (float) px;

        if (px == 0) curve.startNewSubPath (x, y);
        else         curve.lineTo (x, y);
    }

    const float lvl = juce::jlimit (0.0f, 1.0f, processorRef.getLevel());

    g.setColour (kAccent.withAlpha (0.10f));
    g.strokePath (curve, juce::PathStrokeType (14.0f, juce::PathStrokeType::curved));
    g.setColour (kAccent.withAlpha (0.20f + 0.12f * lvl));
    g.strokePath (curve, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved));

    g.setColour (kAccentHi);
    g.strokePath (curve, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved));

    // handles
    for (int i = 0; i < 4; ++i)
    {
        const auto p = eqPoint (i);
        const bool hpLp = (i == 0 || i == 3);

        g.setColour (kBgBot.withAlpha (0.9f));
        g.fillEllipse (p.x - 6.5f, p.y - 6.5f, 13.0f, 13.0f);

        g.setColour (hpLp ? kText.withAlpha (0.85f) : kAccent);
        g.fillEllipse (p.x - 4.5f, p.y - 4.5f, 9.0f, 9.0f);

        g.setColour (kAccent.withAlpha (0.30f));
        g.drawEllipse (p.x - 8.5f, p.y - 8.5f, 17.0f, 17.0f, 1.0f);

        g.setColour (kAccent.withAlpha (0.10f));
        g.fillEllipse (p.x - 11.0f, p.y - 11.0f, 22.0f, 22.0f);
    }
}

//==============================================================================
void HyperareaAudioProcessorEditor::drawMix (juce::Graphics& g)
{
    const auto c = mixCentre();
    const float radius = 94.0f;
    const float mix = juce::jlimit (0.0f, 1.0f, value ("mix") * 0.01f);

    const float start = juce::MathConstants<float>::pi * 0.75f;      // 135 deg
    const float end   = juce::MathConstants<float>::pi * 2.25f;      // 405 deg

    const float lvl = juce::jlimit (0.0f, 1.0f, processorRef.getLevel());

    // track
    g.setColour (juce::Colours::white.withAlpha (0.055f));
    g.drawEllipse (c.x - radius, c.y - radius, radius * 2.0f, radius * 2.0f, 9.0f);

    // value arc, glowing with the audio
    const float angle = start + (end - start) * mix;

    juce::Path arc;
    arc.addCentredArc (c.x, c.y, radius, radius, 0.0f, start, angle, true);

    glowStroke (g, arc, kAccent, 9.0f + 2.0f * lvl, 0.85f + 0.75f * lvl);

    // end cap
    const float capX = c.x + radius * std::sin (angle);
    const float capY = c.y - radius * std::cos (angle);

    g.setColour (kAccentHi.withAlpha (0.30f + 0.25f * lvl));
    g.fillEllipse (capX - 11.0f, capY - 11.0f, 22.0f, 22.0f);

    g.setColour (juce::Colours::white);
    g.fillEllipse (capX - 5.0f, capY - 5.0f, 10.0f, 10.0f);

    // centre readout
    g.setColour (kText);
    g.setFont (juce::Font (20.0f, juce::Font::bold));
    g.drawText ("MIX " + juce::String (juce::roundToInt (mix * 100.0f)) + "%",
                juce::Rectangle<int> ((int) c.x - 120, (int) c.y + 46, 240, 28),
                juce::Justification::centred, false);
}

//==============================================================================
void HyperareaAudioProcessorEditor::drawSmallKnob (juce::Graphics& g, juce::Point<float> c,
                                                   float radius, float value01,
                                                   const juce::String& label, const juce::String& valueText)
{
    g.setColour (kDim);
    g.setFont (juce::Font (15.0f, juce::Font::bold));
    g.drawText (label, juce::Rectangle<int> ((int) c.x - 40, (int) c.y - 52, 80, 20),
                juce::Justification::centred, false);

    // body
    g.setGradientFill (juce::ColourGradient (kPanelHi, c.x, c.y - radius,
                                             juce::Colour (0xff15151a), c.x, c.y + radius, false));
    g.fillEllipse (c.x - radius, c.y - radius, radius * 2.0f, radius * 2.0f);

    g.setColour (kBorder);
    g.drawEllipse (c.x - radius, c.y - radius, radius * 2.0f, radius * 2.0f, 1.5f);

    // track arc
    const float start = juce::MathConstants<float>::pi * 0.75f;
    const float end   = juce::MathConstants<float>::pi * 2.25f;
    const float angle = start + (end - start) * juce::jlimit (0.0f, 1.0f, value01);
    const float rr = radius + 5.0f;

    juce::Path track;
    track.addCentredArc (c.x, c.y, rr, rr, 0.0f, start, end, true);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.strokePath (track, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved));

    juce::Path arc;
    arc.addCentredArc (c.x, c.y, rr, rr, 0.0f, start, angle, true);
    glowStroke (g, arc, kAccent, 2.5f, 1.0f);

    // pointer
    g.setColour (kAccentHi);
    g.drawLine (c.x, c.y,
                c.x + std::sin (angle) * radius * 0.62f,
                c.y - std::cos (angle) * radius * 0.62f, 2.5f);

    g.setColour (kDim);
    g.setFont (juce::Font (9.0f, juce::Font::bold));
    g.drawText (valueText, juce::Rectangle<int> ((int) c.x - 40, (int) c.y + 28, 80, 16),
                juce::Justification::centred, false);
}

//==============================================================================
void HyperareaAudioProcessorEditor::drawBar (juce::Graphics& g)
{
    auto b = barBounds().toFloat();

    g.setColour (kBar);
    g.fillRect (b);

    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawHorizontalLine (barBounds().getY(), b.getX(), b.getRight());

    const int root  = (int) value ("root");
    const int scale = (int) value ("scale");
    const int oct   = (int) value ("octave") - 2;

    static const char* noteNames[12] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static const char* scaleNames[9] { "Chromatic", "Major", "Minor", "Minor Pentatonic",
                                       "Major Pentatonic", "Dorian", "Phrygian", "Harmonic Minor", "Custom" };

    g.setColour (kText);
    g.setFont (juce::Font (18.0f, juce::Font::bold));
    g.drawText (noteNames[juce::jlimit (0, 11, root)], rootSlot(), juce::Justification::centred, false);
    g.drawText (scaleNames[juce::jlimit (0, 8, scale)], scaleSlot(), juce::Justification::centred, false);

    g.setColour (kDim);
    g.setFont (juce::Font (12.0f, juce::Font::bold));
    g.drawText ("OCT " + juce::String (oct > 0 ? "+" : "") + juce::String (oct),
                octSlot(), juce::Justification::centred, false);
}

//==============================================================================
void HyperareaAudioProcessorEditor::drawKeyboard (juce::Graphics& g)
{
    const int root = (int) value ("root");
    const int det  = processorRef.getDetectedMidi();
    const bool voiced = processorRef.isVoiced();
    const int detDeg = ((det % 12) + 12) % 12;

    for (int deg = 0; deg < 12; ++deg)
    {
        const auto c = keyCentre (deg);
        const float r = keyRadius (deg);

        const bool inScale = keyInScale (deg);            // absolute pitch class, root offset handled inside
        const bool isRoot  = (deg == root);

        if (inScale)
        {
            g.setColour (kAccent.withAlpha (0.14f));
            g.fillEllipse (c.x - r - 7.0f, c.y - r - 7.0f, (r + 7.0f) * 2.0f, (r + 7.0f) * 2.0f);

            g.setColour (kAccent.withAlpha (0.92f));
            g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
        }
        else
        {
            g.setColour (kAccent.withAlpha (0.05f));
            g.fillEllipse (c.x - r - 6.0f, c.y - r - 6.0f, (r + 6.0f) * 2.0f, (r + 6.0f) * 2.0f);

            g.setColour (kBgBot);
            g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);

            g.setColour (kAccent.withAlpha (0.55f));
            g.drawEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f, 2.0f);

            g.setColour (kAccent.withAlpha (0.10f));
            g.drawEllipse (c.x - r - 2.5f, c.y - r - 2.5f, (r + 2.5f) * 2.0f, (r + 2.5f) * 2.0f, 3.0f);
        }

        if (isRoot)
        {
            g.setColour (juce::Colours::white.withAlpha (0.18f));
            g.drawEllipse (c.x - r - 5.0f, c.y - r - 5.0f, (r + 5.0f) * 2.0f, (r + 5.0f) * 2.0f, 4.0f);

            g.setColour (kText);
            g.drawEllipse (c.x - r - 2.5f, c.y - r - 2.5f, (r + 2.5f) * 2.0f, (r + 2.5f) * 2.0f, 1.5f);
        }

        if (voiced && deg == detDeg)
        {
            const float pulse = 0.55f + 0.45f * std::sin (phase * 7.0f);

            g.setColour (juce::Colour (0xff3ef0b0).withAlpha (0.30f * pulse));
            g.drawEllipse (c.x - r - 6.0f, c.y - r - 6.0f, (r + 6.0f) * 2.0f, (r + 6.0f) * 2.0f, 5.0f);

            g.setColour (juce::Colour (0xff3ef0b0).withAlpha (0.9f * pulse));
            g.drawEllipse (c.x - r - 2.0f, c.y - r - 2.0f, (r + 2.0f) * 2.0f, (r + 2.0f) * 2.0f, 2.0f);
        }
    }
}

//==============================================================================
void HyperareaAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.position;

    //--- EQ handles ---------------------------------------------------------
    for (int i = 0; i < 4; ++i)
    {
        if (eqPoint (i).getDistanceFrom (p) < 13.0f)
        {
            dragMode = 1 + i;
            return;
        }
    }

    //--- mix ring -----------------------------------------------------------
    if (p.getDistanceFrom (mixCentre()) > 60.0f && p.getDistanceFrom (mixCentre()) < 118.0f)
    {
        dragMode = 5;
        dragStart = value ("mix");
        return;
    }

    //--- small knobs --------------------------------------------------------
    if (p.getDistanceFrom (outCentre()) < 30.0f) { dragMode = 6; dragStart = value ("output"); return; }
    if (p.getDistanceFrom (sftCentre()) < 30.0f) { dragMode = 7; dragStart = value ("shift");  return; }

    //--- bar ----------------------------------------------------------------
    if (rootSlot().contains (e.getPosition()))  { showRootMenu();  return; }
    if (scaleSlot().contains (e.getPosition())) { showScaleMenu(); return; }
    if (octSlot().contains (e.getPosition()))   { showOctMenu();   return; }

    //--- keyboard -----------------------------------------------------------
    for (int deg = 0; deg < 12; ++deg)
    {
        if (p.getDistanceFrom (keyCentre (deg)) < keyRadius (deg) + 4.0f)
        {
            if (e.mods.isRightButtonDown() || e.mods.isAltDown())
            {
                setValue ("root", (float) deg);
            }
            else
            {
                dragMode = 8;
                dragDegree = deg;

                const int root = (int) value ("root");
                const int d = ((deg - root) % 12 + 12) % 12;
                const juce::String id = "deg" + juce::String (d);

                const bool on = value (id.toRawUTF8()) > 0.5f;

                int enabled = 0;
                for (int k = 0; k < 12; ++k)
                    if (value (("deg" + juce::String (k)).toRawUTF8()) > 0.5f)
                        ++enabled;

                if (! (on && enabled <= 1))
                {
                    setValue ("scale", 8.0f);                 // Custom
                    setValue (id.toRawUTF8(), on ? 0.0f : 1.0f);
                }
            }

            repaint();
            return;
        }
    }
}

void HyperareaAudioProcessorEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragMode >= 1 && dragMode <= 4)
    {
        const int band = dragMode - 1;
        const auto r = eqPlot();

        const float u = juce::jlimit (0.0f, 1.0f, (e.position.x - r.getX()) / r.getWidth());
        const float f = juce::jlimit (20.0f, 20000.0f, 20.0f * std::pow (1000.0f, u));

        if (band == 0)
        {
            setValue ("eq1f", juce::jlimit (20.0f, 2000.0f, f));
        }
        else if (band == 3)
        {
            const float g = juce::jlimit (-18.0f, 18.0f,
                                          (r.getCentreY() - e.position.y) / (r.getHeight() * 0.45f) * 18.0f);

            setValue ("eq4f", juce::jlimit (200.0f, 20000.0f, f));
            setValue ("eq4g", g);
        }
        else
        {
            const float g = juce::jlimit (-18.0f, 18.0f,
                                          (r.getCentreY() - e.position.y) / (r.getHeight() * 0.45f) * 18.0f);

            setValue (band == 1 ? "eq2f" : "eq3f", juce::jlimit (30.0f, 18000.0f, f));
            setValue (band == 1 ? "eq2g" : "eq3g", g);
        }

        repaint();
        return;
    }

    if (dragMode == 5)
    {
        setValue ("mix", juce::jlimit (0.0f, 100.0f, dragStart - (float) e.getDistanceFromDragStartY() * 0.5f));
        repaint();
        return;
    }

    if (dragMode == 6)
    {
        setValue ("output", juce::jlimit (-24.0f, 24.0f, dragStart - (float) e.getDistanceFromDragStartY() * 0.1f));
        repaint();
        return;
    }

    if (dragMode == 7)
    {
        setValue ("shift", juce::jlimit (-24.0f, 24.0f,
                  std::round (dragStart - (float) e.getDistanceFromDragStartY() * 0.08f)));
        repaint();
        return;
    }
}

void HyperareaAudioProcessorEditor::mouseUp (const juce::MouseEvent&)
{
    dragMode = 0;
}

//==============================================================================
void HyperareaAudioProcessorEditor::showRootMenu()
{
    juce::PopupMenu m;
    static const char* names[12] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int current = (int) value ("root");

    for (int i = 0; i < 12; ++i)
        m.addItem (i + 1, names[i], true, i == current);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                        .withTargetScreenArea (localAreaToGlobal (rootSlot())),
                     [this] (int result)
                     {
                         if (result > 0) { setValue ("root", (float) (result - 1)); repaint(); }
                     });
}

void HyperareaAudioProcessorEditor::showScaleMenu()
{
    juce::PopupMenu m;
    static const char* names[9] { "Chromatic", "Major", "Minor", "Minor Pentatonic",
                                  "Major Pentatonic", "Dorian", "Phrygian", "Harmonic Minor", "Custom" };
    const int current = (int) value ("scale");

    for (int i = 0; i < 9; ++i)
        m.addItem (i + 1, names[i], true, i == current);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                        .withTargetScreenArea (localAreaToGlobal (scaleSlot())),
                     [this] (int result)
                     {
                         if (result > 0) { setValue ("scale", (float) (result - 1)); repaint(); }
                     });
}

void HyperareaAudioProcessorEditor::showOctMenu()
{
    juce::PopupMenu m;
    static const char* names[5] { "-2", "-1", "0", "+1", "+2" };
    const int current = (int) value ("octave");

    for (int i = 0; i < 5; ++i)
        m.addItem (i + 1, names[i], true, i == current);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                        .withTargetScreenArea (localAreaToGlobal (octSlot())),
                     [this] (int result)
                     {
                         if (result > 0) { setValue ("octave", (float) (result - 1)); repaint(); }
                     });
}
