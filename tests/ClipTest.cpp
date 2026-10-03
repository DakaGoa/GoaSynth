// Master limiter test. goa::MasterLimiter replaced juce::dsp::Limiter, whose
// final stage was a ±1.0 hard clipper that squared off hot peaks — the
// "clipping" hot patches produced (full OTT squeeze, runaway delay feedback,
// MASTER +6). The replacement promises two things, and this test pins both:
//
//   1. the loudness curve is the same one the old limiter had (4:1 above
//      -10 dB plus its +4.26 dB makeup), so moderate material is unchanged;
//   2. nothing leaves the plugin above the -0.5 dB ceiling, however hot the
//      patch — and without the square-edge signature of a hard clipper.
//
// The curve checks feed constant-level buffers straight into the limiter (a
// steady level settles the ballistics exactly, no envelope wobble to
// tolerate); the end-to-end check renders the real plugin headless.
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <cmath>
#include <cstdio>

#include "PluginProcessor.h"

static int fails = 0;

#define EXPECT(cond, msg)                                                      \
    do                                                                         \
    {                                                                          \
        if (! (cond))                                                          \
        {                                                                      \
            std::printf ("FAIL: %s\n", (msg));                                 \
            ++fails;                                                           \
        }                                                                      \
    } while (false)

// juce::dsp::Limiter's static curve, in dB: the 4:1 stage above -10 dB plus
// its +4.26 dB makeup. Below about +13 dB of input this was the old limiter's
// whole behaviour; above it, the old limiter hard-clipped instead.
static double expectedCurveDb (double inDb)
{
    const double over = juce::jmax (0.0, inDb + 10.0);
    return inDb - 0.75 * over + 4.2567;
}

static float linFromDb (double db) { return (float) std::pow (10.0, db / 20.0); }

// Feed a constant-level stereo buffer through the limiter and return the
// output peak over the tail (the first 0.5 s lets the ballistics settle).
static float steadyOutputPeak (float amplitude, double seconds = 2.0)
{
    goa::MasterLimiter lim;
    lim.prepare (48000.0);

    constexpr int block = 512;
    juce::AudioBuffer<float> buf (2, block);
    const int blocks = (int) (seconds * 48000.0 / block);
    float peak = 0.0f;
    for (int b = 0; b < blocks; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < block; ++i)
                buf.setSample (ch, i, amplitude);
        lim.process (buf, block);
        if (b >= blocks / 2)   // tail only: ballistics settled
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                    peak = juce::jmax (peak, std::abs (buf.getSample (ch, i)));
    }
    return peak;
}

int main()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);

    // ---- softClip: transparent below the knee, bounded above, C1 at it ----
    {
        EXPECT (goa::softClip (0.5f) == 0.5f, "softClip leaves quiet signal untouched");
        EXPECT (goa::softClip (0.9f) == 0.9f, "softClip leaves the knee itself untouched");
        EXPECT (goa::softClip (0.95f) > 0.9f && goa::softClip (0.95f) < 0.95f,
                "softClip rounds just above the knee, and downwards");
        EXPECT (goa::softClip (1.5f) < 1.0f && goa::softClip (1.5f) > 0.99f,
                "softClip bounds a big overshoot just under full scale");
        EXPECT (goa::softClip (-1.5f) < -0.99f && goa::softClip (-1.5f) > -1.0f,
                "softClip is symmetric in sign");
        EXPECT (goa::softClip (1.0e6f) <= 1.0f,
                "softClip never exceeds full scale, even for absurd input");
        bool monotonic = true;
        for (int k = 0; k < 200; ++k)
            if (goa::softClip ((float) k / 100.0f) > goa::softClip ((float) (k + 1) / 100.0f))
                monotonic = false;
        EXPECT (monotonic, "softClip is monotonic across the knee");
    }

    // ---- the kept loudness curve, at steady levels ------------------------
    // -3, 0 and +6 dBFS in: the old limiter was clean here, and the output
    // must match its curve (±0.3 dB). +14 dBFS in: where the old limiter's
    // output crossed full scale and its hard clipper squared it off — the
    // new one rides it to the -0.5 dB ceiling instead.
    {
        struct Case { double inDb; double wantDb; bool atCeiling; };
        const Case cases[] = { { -3.0, -3.0, false }, { 0.0, 0.0, false },
                               { 6.0, 6.0, false }, { 14.0, 14.0, true } };
        for (const auto& c : cases)
        {
            const float want = c.atCeiling ? 0.94406f : linFromDb (expectedCurveDb (c.inDb));
            const float got = steadyOutputPeak (linFromDb (c.inDb));
            const double gotDb = 20.0 * std::log10 ((double) juce::jmax (got, 1.0e-6f));
            const double wantDb = c.atCeiling ? -0.5 : expectedCurveDb (c.wantDb);
            char msg[256];
            std::snprintf (msg, sizeof (msg),
                           "steady %.0f dBFS in: out %.2f dB, want %.2f dB",
                           c.inDb, gotDb, wantDb);
            EXPECT (std::abs (gotDb - wantDb) < (c.atCeiling ? 0.3 : 0.3), msg);
        }

        // The old limiter at +14 dBFS pinned most samples at exactly ±1.0
        // (a square edge). The new one must never touch full scale.
        goa::MasterLimiter lim;
        lim.prepare (48000.0);
        constexpr int block = 512;
        juce::AudioBuffer<float> buf (2, block);
        int atFullScale = 0;
        for (int b = 0; b < 188; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                    buf.setSample (ch, i, 5.0f);   // +14 dBFS, sustained
            lim.process (buf, block);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                    if (std::abs (buf.getSample (ch, i)) >= 0.9995f)
                        ++atFullScale;
        }
        EXPECT (atFullScale == 0, "a sustained +14 dBFS level never reaches full scale");

        // And the peak stage releases: after the loud level stops, the
        // output must return to the curve's value for a quiet one.
        for (int b = 0; b < 94; ++b)   // 1 s at -20 dBFS
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                    buf.setSample (ch, i, linFromDb (-20.0));
            lim.process (buf, block);
        }
        float peak = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < block; ++i)
                peak = juce::jmax (peak, std::abs (buf.getSample (ch, i)));
        const double gotDb = 20.0 * std::log10 ((double) peak);
        EXPECT (std::abs (gotDb - expectedCurveDb (-20.0)) < 0.3,
                "after a loud burst, a quiet level comes back up to the curve");
    }

    // ---- end to end: a deliberately over-hot patch through the plugin ----
    // OTT full squeeze + its output at +6 dB + MASTER +6 + a fat supersaw
    // with resonant filter and runaway delay feedback — the recipe that used
    // to push the limiter's input past +13 dB and come out square-clipped.
    {
        GoaSynthAudioProcessor r;
        r.prepareToPlay (48000.0, 256);

        auto set = [&] (const char* id, float v)
        {
            if (auto* p = r.apvts.getParameter (id))
                p->setValueNotifyingHost (p->convertTo0to1 (v));
        };
        set ("ottDepth", 1.0f);
        set ("ottOut", 6.0f);
        set ("masterGain", 6.0f);
        set ("drive", 1.0f);
        set ("osc1Level", 1.0f);
        set ("osc2Level", 1.0f);
        set ("osc2Wave", 1.0f);
        set ("uniVoices", 7.0f);
        set ("subLevel", 0.8f);
        set ("reso", 1.0f);
        set ("delayFb", 0.95f);
        set ("delayMix", 0.6f);

        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer midi;
        r.uiNoteOn (48, 1.0f);
        r.uiNoteOn (55, 1.0f);
        r.uiNoteOn (60, 1.0f);
        r.uiNoteOn (67, 1.0f);

        float peak = 0.0f;
        bool allFinite = true;
        int atFullScale = 0;
        for (int blk = 0; blk < 352; ++blk)   // ~3 s, past every attack
        {
            buf.clear();
            r.processBlock (buf, midi);
            midi.clear();
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < buf.getNumSamples(); ++i)
                {
                    const float s = buf.getSample (ch, i);
                    if (! std::isfinite (s))
                        allFinite = false;
                    peak = juce::jmax (peak, std::abs (s));
                    if (std::abs (s) >= 0.9995f)
                        ++atFullScale;
                }
        }

        EXPECT (allFinite, "over-hot patch renders only finite samples");
        EXPECT (peak <= 0.95f, "over-hot patch stays under the -0.5 dB ceiling");
        EXPECT (peak >= 0.5f, "over-hot patch is still loud (the limiter is not just a pad)");
        EXPECT (atFullScale == 0,
                "over-hot patch has no square-clip signature at full scale");
    }

    std::printf (fails == 0 ? "ALL PASSED\n" : "%d FAILURE(S)\n", fails);
    return fails == 0 ? 0 : 1;
}
