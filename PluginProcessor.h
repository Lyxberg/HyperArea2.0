#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <vector>

//==============================================================================
/**
    Pitch-morph - monophonic harmonic remapping (Pitchmap style).

    STFT -> detect the fundamental -> snap it to the scale (or transpose by a
    fixed interval) -> phase-vocoder pitch shift every bin by the same ratio.

    Every harmonic moves together, so the timbre is re-pitched instead of
    filtered: that is the "morph" character colour bass is built on.
*/
class HyperareaAudioProcessor final : public juce::AudioProcessor
{
public:
    HyperareaAudioProcessor();
    ~HyperareaAudioProcessor() override = default;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                       { return true; }

    const juce::String getName() const override           { return JucePlugin_Name; }

    bool acceptsMidi() const override                     { return false; }
    bool producesMidi() const override                    { return false; }
    bool isMidiEffect() const override                    { return false; }
    double getTailLengthSeconds() const override          { return 0.0; }

    int getNumPrograms() override                         { return 1; }
    int getCurrentProgram() override                      { return 0; }
    void setCurrentProgram (int) override                 {}
    const juce::String getProgramName (int) override      { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    //==========================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    juce::AudioProcessorValueTreeState apvts;

    struct ScopeSnapshot
    {
        static constexpr int maxBins = 1025;
        std::array<float, maxBins> db {};
        int  numBins = 0;
        bool valid = false;
    };

    bool readScope (ScopeSnapshot& dest) const;

    int  getDetectedMidi() const noexcept { return detectedMidi.load(); }
    int  getTargetMidi()   const noexcept { return targetMidi.load(); }
    bool isVoiced()        const noexcept { return voiced.load(); }
    float getLevel()       const noexcept { return level.load(); }

    static juce::String noteName (int midi);
    static bool isInScale (int midi, int root, int scale);
    static bool scaleMask (int scale, int semitone);

private:
    //==========================================================================
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void analysePitch();
    void renderFrame (int channel, const float* ratios, int numVoices);

    bool  spectralSnap = false;
    float frameF0 = 65.4f;
    int   frameRoot = 0, frameScale = 3, frameOctave = 0, frameShift = 0;
    std::vector<float> magAcc, outPhaseSum;

    //==========================================================================
    static constexpr int fftOrder  = 11;
    static constexpr int fftSize   = 1 << fftOrder;      // 2048
    static constexpr int hopSize   = fftSize / 4;
    static constexpr int halfBins  = fftSize / 2;
    static constexpr int latency   = fftSize;
    static constexpr int anaSize   = 4096;
    static constexpr int anaDec    = 8;
    static constexpr int anaWin    = anaSize / anaDec;

    struct Peak
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;
        void setPeaking (double sr, double f, double q, double gainDb) noexcept
        {
            f = juce::jlimit (20.0, sr * 0.45, f);
            q = juce::jmax (0.3, q);

            const double A = std::pow (10.0, gainDb / 40.0);
            const double w0 = 2.0 * juce::MathConstants<double>::pi * f / sr;
            const double alpha = std::sin (w0) / (2.0 * q);
            const double a0 = 1.0 + alpha / A;

            b0 = (float) ((1.0 + alpha * A) / a0);
            b1 = (float) (-2.0 * std::cos (w0) / a0);
            b2 = (float) ((1.0 - alpha * A) / a0);
            a1 = b1;
            a2 = (float) ((1.0 - alpha / A) / a0);
        }

        void setHighPass (double sr, double f, double q) noexcept
        {
            f = juce::jlimit (10.0, sr * 0.45, f);

            const double w0    = 2.0 * juce::MathConstants<double>::pi * f / sr;
            const double cw    = std::cos (w0);
            const double alpha = std::sin (w0) / (2.0 * juce::jmax (0.3, q));
            const double a0    = 1.0 + alpha;

            b0 = (float) ((1.0 + cw) * 0.5 / a0);
            b1 = (float) (-(1.0 + cw) / a0);
            b2 = b0;
            a1 = (float) (-2.0 * cw / a0);
            a2 = (float) ((1.0 - alpha) / a0);
        }

        void setLowPass (double sr, double f, double q) noexcept
        {
            f = juce::jlimit (10.0, sr * 0.45, f);

            const double w0    = 2.0 * juce::MathConstants<double>::pi * f / sr;
            const double cw    = std::cos (w0);
            const double alpha = std::sin (w0) / (2.0 * juce::jmax (0.3, q));
            const double a0    = 1.0 + alpha;

            b0 = (float) ((1.0 - cw) * 0.5 / a0);
            b1 = (float) ((1.0 - cw) / a0);
            b2 = b0;
            a1 = (float) (-2.0 * cw / a0);
            a2 = (float) ((1.0 - alpha) / a0);
        }

        void setHighShelf (double sr, double f, double gainDb) noexcept
        {
            f = juce::jlimit (20.0, sr * 0.45, f);

            const double A  = std::pow (10.0, gainDb / 40.0);
            const double w0 = 2.0 * juce::MathConstants<double>::pi * f / sr;
            const double cw = std::cos (w0);
            const double alpha = std::sin (w0) * 0.5 * juce::MathConstants<double>::sqrt2;
            const double a0 = (A + 1.0) - (A - 1.0) * cw + 2.0 * std::sqrt (A) * alpha;

            b0 = (float) (A * ((A + 1.0) + (A - 1.0) * cw + 2.0 * std::sqrt (A) * alpha) / a0);
            b1 = (float) (-2.0 * A * ((A - 1.0) + (A + 1.0) * cw) / a0);
            b2 = (float) (A * ((A + 1.0) + (A - 1.0) * cw - 2.0 * std::sqrt (A) * alpha) / a0);
            a1 = (float) (2.0 * ((A - 1.0) - (A + 1.0) * cw) / a0);
            a2 = (float) ((A + 1.0) - (A - 1.0) * cw - 2.0 * std::sqrt (A) * alpha) / a0;
        }

        void reset() noexcept { z1 = z2 = 0.0f; }

        float process (float x) noexcept
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    struct EnvFollower
    {
        float y = 0.0f, aA = 0.0f, aR = 0.0f;

        void setTimes (double sr, float attackMs, float releaseMs) noexcept
        {
            aA = std::exp (-1.0f / juce::jmax (1.0e-3f, attackMs * 0.001f * (float) sr));
            aR = std::exp (-1.0f / juce::jmax (1.0e-3f, releaseMs * 0.001f * (float) sr));
        }

        float process (float x) noexcept
        {
            const float in = std::abs (x);
            const float c  = in > y ? aA : aR;
            y = in + c * (y - in);
            return y;
        }

        void reset() noexcept { y = 0.0f; }
    };

    struct Channel
    {
        Peak fmt[3];
        Peak eq[4];
        EnvFollower envFast, envSlow, envTail;

        std::vector<float> inRing, ola, hopOut, dryDelay;
        std::vector<float> fftBuf, residual;
        std::vector<float> lastPhase, trueFreq;
        std::array<std::vector<float>, 4> outPhase;
        std::vector<float> specPhase;
        int inPos = 0, outIndex = 0, dryPos = 0;
    };

    juce::dsp::FFT fft { fftOrder };
    std::vector<float> window;
    Channel chans[2];

    std::vector<float> anaRing, decBuf, corrBuf;

    double currentSampleRate = 48000.0;
    float  curRatio = 1.0f;
    int    blockCounter = 0;
    float  lastVowel = -1.0f, lastQ = -1.0f;
    int    candidateMidi = -1;
    int    candidateCount = 0;

    void captureScope (const float* buf, int halfBins);

    mutable juce::SpinLock scopeLock;
    ScopeSnapshot scope;

    std::array<bool, 12> curMask {};
    int curRoot = 0;

    bool inScaleMidi (int midi) const noexcept
    {
        return curMask[(size_t) (((midi - curRoot) % 12 + 12) % 12)];
    }

    std::atomic<int>  detectedMidi { 0 };
    std::atomic<int>  targetMidi   { 36 };
    std::atomic<bool> voiced { false };
    std::atomic<float> level { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HyperareaAudioProcessor)
};
