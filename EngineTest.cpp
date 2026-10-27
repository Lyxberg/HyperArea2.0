//==============================================================================
//  Offline self-test for Hyperarea.
//  Build & run:
//      .\build\HyperareaTest_artefacts\Release\HyperareaTest.exe
//      .\build\HyperareaTest_artefacts\Release\HyperareaTest.exe ui out.png
//==============================================================================

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
    constexpr double kSampleRate = 48000.0;
    constexpr int    kBlock      = 512;

    void setParam (HyperareaAudioProcessor& p, const juce::String& id, float value)
    {
        if (auto* param = p.apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    std::vector<float> render (HyperareaAudioProcessor& p, const std::vector<float>& in)
    {
        std::vector<float> out (in.size(), 0.0f);
        size_t pos = 0;

        while (pos < in.size())
        {
            const int n = (int) juce::jmin ((size_t) kBlock, in.size() - pos);
            juce::AudioBuffer<float> buf (2, n);
            buf.clear();

            for (int i = 0; i < n; ++i)
            {
                buf.setSample (0, i, in[pos + (size_t) i]);
                buf.setSample (1, i, in[pos + (size_t) i]);
            }

            juce::MidiBuffer midi;
            p.processBlock (buf, midi);

            for (int i = 0; i < n; ++i)
                out[pos + (size_t) i] = buf.getSample (0, i);

            pos += (size_t) n;
        }

        return out;
    }

    float rms (const std::vector<float>& v, size_t from)
    {
        double acc = 0.0;
        int n = 0;

        for (size_t i = from; i < v.size(); ++i) { acc += (double) v[i] * v[i]; ++n; }
        return n > 0 ? (float) std::sqrt (acc / (double) n) : 0.0f;
    }

    struct Report
    {
        int failures = 0;
        void check (bool ok, const juce::String& what, const juce::String& detail)
        {
            std::printf ("  [%s] %-46s %s\n", ok ? " ok " : "FAIL",
                         what.toRawUTF8(), detail.toRawUTF8());
            if (! ok) ++failures;
        }
    };

    int renderUiShot (const juce::File& out, float eq4g = 0.0f)
    {
        HyperareaAudioProcessor proc;
        proc.prepareToPlay (kSampleRate, kBlock);

        if (std::abs (eq4g) > 0.01f)
            setParam (proc, "eq4g", eq4g);

        juce::AudioBuffer<float> b (2, kBlock);
        juce::MidiBuffer midi;

        for (int blk = 0; blk < 60; ++blk)
        {
            for (int i = 0; i < kBlock; ++i)
            {
                const float t = (float) (blk * kBlock + i) / (float) kSampleRate;
                const float s = 0.4f * std::sin (2.0f * juce::MathConstants<float>::pi * 55.0f * t);
                b.setSample (0, i, s);
                b.setSample (1, i, s);
            }

            proc.processBlock (b, midi);
        }

        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        ed->setSize (440, 555);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds());

        out.deleteFile();
        juce::FileOutputStream os (out);

        if (! os.openedOk())
            return 1;

        juce::PNGImageFormat().writeImageToStream (img, os);
        std::printf ("wrote %s\n", out.getFullPathName().toRawUTF8());
        return 0;
    }
}

//==============================================================================
int main (int argc, char** argv)
{
    setvbuf (stdout, nullptr, _IONBF, 0);

    juce::ScopedJuceInitialiser_GUI init;

    if (argc > 1 && juce::String (argv[1]) == "ui")
        return renderUiShot (juce::File (juce::String (argv[2])),
                             argc > 3 ? (float) std::atof (argv[3]) : 0.0f);

    if (argc > 1 && juce::String (argv[1]) == "spec" && argc > 2)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (juce::File (juce::String (argv[2]))));
        if (rd == nullptr) { std::printf ("cannot read\\n"); return 1; }

        const double start = argc > 3 ? std::atof (argv[3]) : 0.0;
        const int order = 13;                    // 8192
        const int N = 1 << order;
        const int64 pos = (int64) (start * rd->sampleRate);
        juce::AudioBuffer<float> buf (1, N);
        rd->read (&buf, 0, N, pos, true, false);

        juce::dsp::FFT fft (order);
        std::vector<float> data ((size_t) N * 2, 0.0f);
        for (int i = 0; i < N; ++i)
            data[(size_t) i] = buf.getSample (0, i)
                             * (0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi
                                                        * (float) i / (float) N));
        fft.performRealOnlyForwardTransform (data.data(), true);

        struct Peak { float hz, db; };
        std::vector<Peak> peaks;

        for (int k = 2; k < N / 2 - 1; ++k)
        {
            const float m = std::sqrt (data[2 * k] * data[2 * k] + data[2 * k + 1] * data[2 * k + 1]);

            if (m > data[2 * (k - 1)] && m > data[2 * (k + 1)] && m > 1.0f)
            {
                const float hz = (float) k * (float) rd->sampleRate / (float) N;
                const float db = 20.0f * std::log10 (m + 1e-9f);
                if (db > -70.0f) peaks.push_back ({ hz, db });
            }
        }

        std::sort (peaks.begin(), peaks.end(), [] (const Peak& a, const Peak& b) { return a.hz < b.hz; });

        std::printf ("peaks (start %.2fs):\\n", start);
        int shown = 0;
        for (auto& pk : peaks)
        {
            std::printf ("  %8.2f Hz  %6.1f dB\\n", pk.hz, pk.db);
            if (++shown >= 24) break;
        }
        return 0;
    }

    if (argc > 1 && juce::String (argv[1]) == "pitch" && argc > 2)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (juce::File (juce::String (argv[2]))));
        if (rd == nullptr) { std::printf ("cannot read\\n"); return 1; }

        const int len = (int) juce::jmin (rd->lengthInSamples, (juce::int64) (rd->sampleRate * 30.0));
        const int nch = (int) juce::jmin ((unsigned) 2, rd->numChannels);
        juce::AudioBuffer<float> buf (nch, len);
        rd->read (&buf, 0, len, 0, true, true);

        HyperareaAudioProcessor pp;
        setParam (pp, "mix", 0.0f);
        pp.prepareToPlay (rd->sampleRate, 512);

        for (int pos = 0; pos < len; pos += 512)
        {
            const int n = juce::jmin (512, len - pos);
            juce::AudioBuffer<float> chunk (nch, n);
            for (int ch = 0; ch < nch; ++ch) chunk.copyFrom (ch, 0, buf, ch, pos, n);
            juce::MidiBuffer midi;
            pp.processBlock (chunk, midi);

            if ((pos / 512) % 8 == 0)
                std::printf ("  t=%5.2fs  %s  voiced=%d\\n",
                             (double) pos / rd->sampleRate,
                             HyperareaAudioProcessor::noteName (pp.getDetectedMidi()).toRawUTF8(),
                             pp.isVoiced() ? 1 : 0);
        }
        return 0;
    }

    if (argc > 1 && juce::String (argv[1]) == "bass" && argc > 2)
    {
        const double sr = 48000.0;
        const int n = (int) (sr * 4.0);
        juce::AudioBuffer<float> b (2, n);
        b.clear();

        const float notes[] { 32.70f, 32.70f, 38.89f, 43.65f, 49.00f, 43.65f, 32.70f, 32.70f };
        const int perNote = n / 8;
        juce::Random rng (7);

        for (int k = 0; k < 8; ++k)
        {
            const float f = notes[k];
            float phase = 0.0f;

            for (int i = 0; i < perNote; ++i)
            {
                const int idx = k * perNote + i;
                if (idx >= n) break;

                const float t = (float) i / (float) sr;
                phase += f / (float) sr;
                if (phase >= 1.0f) phase -= 1.0f;

                const float saw = 2.0f * phase - 1.0f;
                const float env = std::exp (-t * 5.0f) * (1.0f - std::exp (-t * 800.0f));
                const float click = (t < 0.004f) ? (rng.nextFloat() * 2.0f - 1.0f) * 0.6f : 0.0f;

                const float s = 0.45f * (saw * env + click);
                b.setSample (0, idx, s);
                b.setSample (1, idx, s);
            }
        }

        juce::File out { juce::String (argv[2]) };
        out.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> os (out.createOutputStream());
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, 2, 24, {}, 0));
        if (w != nullptr) { os.release(); w->writeFromAudioSampleBuffer (b, 0, n); }
        std::printf ("wrote %s\\n", out.getFullPathName().toRawUTF8());
        return 0;
    }

    if (argc > 1 && juce::String (argv[1]) == "render" && argc > 3)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (juce::File (juce::String (argv[2]))));

        if (rd == nullptr) { std::printf ("cannot read\\n"); return 1; }

        const int len = (int) juce::jmin (rd->lengthInSamples, (juce::int64) (rd->sampleRate * 30.0));
        const int nch = (int) juce::jmin ((unsigned) 2, rd->numChannels);
        juce::AudioBuffer<float> buf (nch, len);
        rd->read (&buf, 0, len, 0, true, true);

        HyperareaAudioProcessor p2;
        setParam (p2, "mode",   argc > 4 ? (float) std::atof (argv[4]) : 0.0f);
        setParam (p2, "octave", argc > 5 ? (float) std::atof (argv[5]) : 2.0f);
        setParam (p2, "shift",  argc > 6 ? (float) std::atof (argv[6]) : 0.0f);
        setParam (p2, "glide",  argc > 7 ? (float) std::atof (argv[7]) : 15.0f);
        setParam (p2, "mix",    argc > 8 ? (float) std::atof (argv[8]) : 100.0f);
        setParam (p2, "vowel",  argc > 9 ? (float) std::atof (argv[9]) : 0.0f);
        setParam (p2, "fmt",    argc > 10 ? (float) std::atof (argv[10]) : 0.0f);
        setParam (p2, "sharp",  argc > 11 ? (float) std::atof (argv[11]) : 30.0f);
        setParam (p2, "chord",  argc > 12 ? (float) std::atof (argv[12]) : 0.0f);
        setParam (p2, "root",   argc > 13 ? (float) std::atof (argv[13]) : 0.0f);
        setParam (p2, "scale",  argc > 14 ? (float) std::atof (argv[14]) : 3.0f);
        setParam (p2, "drive",  argc > 15 ? (float) std::atof (argv[15]) : 0.0f);
        p2.prepareToPlay (rd->sampleRate, 512);

        for (int pos = 0; pos < len; pos += 512)
        {
            const int n = juce::jmin (512, len - pos);
            juce::AudioBuffer<float> chunk (nch, n);
            for (int ch = 0; ch < nch; ++ch) chunk.copyFrom (ch, 0, buf, ch, pos, n);
            juce::MidiBuffer midi;
            p2.processBlock (chunk, midi);
            for (int ch = 0; ch < nch; ++ch) buf.copyFrom (ch, pos, chunk, ch, 0, n);
        }

        juce::File out { juce::String (argv[3]) };
        out.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> os (out.createOutputStream());
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), rd->sampleRate, (unsigned) nch, 24, {}, 0));
        if (w != nullptr) { os.release(); w->writeFromAudioSampleBuffer (buf, 0, len); }
        std::printf ("wrote %s\\n", out.getFullPathName().toRawUTF8());
        return 0;
    }

    Report res;
    HyperareaAudioProcessor proc;

    std::printf ("\nHyperarea self-test\n\n");

    //-- 1. pitch detection --------------------------------------------------
    {
        setParam (proc, "mix", 100.0f);
        setParam (proc, "root", 9.0f);        // A
        setParam (proc, "scale", 0.0f);       // chromatic
        setParam (proc, "octave", 2.0f);      // 0
        setParam (proc, "mode", 0.0f);        // snap to scale
        proc.prepareToPlay (kSampleRate, kBlock);

        std::vector<float> in ((size_t) kSampleRate * 2);
        for (size_t i = 0; i < in.size(); ++i)
            in[i] = 0.4f * std::sin (2.0f * juce::MathConstants<float>::pi * 55.0f
                                     * (float) i / (float) kSampleRate);

        auto out = render (proc, in);

        res.check (proc.getDetectedMidi() == 33, "detects A1 from 55 Hz",
                   "detected " + HyperareaAudioProcessor::noteName (proc.getDetectedMidi()));

        res.check (proc.getTargetMidi() == 33, "snaps A1 to A1 (scale already has it)",
                   "target " + HyperareaAudioProcessor::noteName (proc.getTargetMidi()));

        const float r = rms (out, 40000);
        res.check (std::isfinite (r) && r > 0.001f, "hyperarea produces output",
                   juce::String (20.0f * std::log10 (r + 1e-9f), 1) + " dBFS");
    }

    //-- 2. spectral snap moves an off-scale partial onto the scale ---------
    {
        setParam (proc, "root", 9.0f);
        setParam (proc, "scale", 3.0f);      // A minor pentatonic
        setParam (proc, "mix", 100.0f);
        proc.prepareToPlay (kSampleRate, kBlock);

        std::vector<float> in ((size_t) kSampleRate * 2);
        for (size_t i = 0; i < in.size(); ++i)
            in[i] = 0.4f * std::sin (2.0f * juce::MathConstants<float>::pi * 480.0f
                                     * (float) i / (float) kSampleRate);

        auto out = render (proc, in);

        const int order = 13;
        const int fftSize = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> data ((size_t) fftSize * 2, 0.0f);

        const int start2 = juce::jmax (0, (int) out.size() - fftSize * 2);

        for (int i = 0; i < fftSize; ++i)
            data[(size_t) i] = out[(size_t) (start2 + i)]
                                 * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi
                                                            * (float) i / (float) (fftSize - 1)));

        fft.performFrequencyOnlyForwardTransform (data.data());

        int peak = 1;
        for (int k = 2; k < fftSize / 2; ++k)
            if (data[(size_t) k] > data[(size_t) peak])
                peak = k;

        const double f = (double) peak * kSampleRate / (double) fftSize;
        const int midi = (int) std::lround (69.0 + 12.0 * std::log2 (f / 440.0));

        res.check (HyperareaAudioProcessor::isInScale (midi, 9, 3),
                   "spectral snap lands on the scale",
                   "480 Hz -> " + HyperareaAudioProcessor::noteName (midi)
                     + " (" + juce::String (f, 1) + " Hz)");
    }
    //-- 2b. EQ high shelf boost / cut --------------------------------------
    {
        const auto topRms = [&] (float gain)
        {
            HyperareaAudioProcessor p2;
            setParam (p2, "root", 0.0f);
            setParam (p2, "scale", 0.0f);
            setParam (p2, "mix", 100.0f);
            setParam (p2, "eq4f", 4000.0f);
            setParam (p2, "eq4g", gain);
            p2.prepareToPlay (kSampleRate, kBlock);

            std::vector<float> in ((size_t) kSampleRate * 2);
            for (size_t i = 0; i < in.size(); ++i)
                in[i] = 0.35f * std::sin (2.0f * juce::MathConstants<float>::pi * 10000.0f
                                          * (float) i / (float) kSampleRate);

            auto out = render (p2, in);
            return rms (out, 60000);
        };

        const float flat = topRms (0.0f);
        const float lift = topRms (12.0f);
        const float cut  = topRms (-12.0f);

        res.check (lift > flat * 1.5f && cut < flat * 0.6f,
                   "EQ high shelf boosts and cuts",
                   juce::String (20.0f * std::log10 (lift / flat), 1) + " dB / "
                     + juce::String (20.0f * std::log10 (cut / flat), 1) + " dB");
    }

    //-- 3. mix = 0 is a passthrough ---------------------------------------
    {
        setParam (proc, "mix", 0.0f);
        setParam (proc, "mode", 1.0f);
        proc.prepareToPlay (kSampleRate, kBlock);

        std::vector<float> in ((size_t) 8192);
        for (size_t i = 0; i < in.size(); ++i)
            in[i] = 0.3f * std::sin (0.01f * (float) i);

        auto out = render (proc, in);

        const int lat = proc.getLatencySamples();
        float maxErr = 0.0f;
        for (size_t i = (size_t) lat; i < in.size(); ++i)
            maxErr = juce::jmax (maxErr, std::abs (out[i] - in[i - (size_t) lat]));

        res.check (maxErr < 1.0e-6f, "Mix = 0 passes the input untouched",
                   "max err " + juce::String (maxErr, 8));
    }

    //-- 4. extreme settings stay finite ------------------------------------
    {
        setParam (proc, "mix", 100.0f);
        setParam (proc, "reso", 120.0f);
        setParam (proc, "shift", 24.0f);
        setParam (proc, "glide", 0.0f);
        proc.prepareToPlay (kSampleRate, kBlock);

        std::vector<float> in ((size_t) 16384);
        for (size_t i = 0; i < in.size(); ++i)
            in[i] = 0.5f * std::sin (0.02f * (float) i) + 0.1f * std::sin (1.3f * (float) i);

        auto out = render (proc, in);

        bool finite = true;
        float peak = 0.0f;
        for (float v : out) { finite = finite && std::isfinite (v); peak = juce::jmax (peak, std::abs (v)); }

        res.check (finite && peak < 8.0f, "extreme settings stable",
                   "peak " + juce::String (peak, 3));
    }

    std::printf ("\n%s\n\n", res.failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    return res.failures == 0 ? 0 : 1;
}
