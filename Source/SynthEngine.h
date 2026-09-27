#pragma once

#include <atomic>
#include <array>
#include <vector>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "Tuning.h"

namespace goa
{

constexpr int maxUnison = 7;

// Multi-frame sketchable wavetable for OSC A / OSC B (Serum-style WT editor).
// 8 frames on a [0, 1) phase axis, amplitude in [-1, 1]; the WT position
// morphs linearly between adjacent frames. The UI sketches into an edit
// surface and publishes a complete frame set; the audio thread reads the
// currently published set lock-free.
struct UserWave
{
    static constexpr int size = 256;
    static constexpr int numFrames = 8;
    std::atomic<bool> updateRequested { false };
    std::atomic<juce::uint64> dirtyMask { 0 };      // bit i = frame i changed

    // --- edit surface (message thread) ---
    void beginEdit() noexcept;                      // copy published -> edit
    float* editFrame (int f) noexcept
    {
        return edit[(size_t) juce::jlimit (0, numFrames - 1, f)].data();
    }
    void write (int frame, float x01, float value, int radius) noexcept;
    void clearEditFrame (int frame) noexcept;       // frame inherits nothing (flat 0)
    void publish() noexcept;                        // make the edit visible to audio
    bool isEditDirty() const noexcept;
    void markEditDirty() noexcept;

    // --- persistence (message thread, reads the published set) ---
    void getFrame (int frame, float* dst, int n) const noexcept;
    void setFrame (int frame, const float* src, int n) noexcept;  // edit surface
    void reset();                                   // saw in frame 0, others empty

    // --- frame transforms & generators (message thread, edit surface) ---
    enum class ShapeKind { pulse25, pulse50, formant, spikes };

    void smoothFrame (int frame) noexcept;          // 1-2-1 binomial blur
    void flipFrame (int frame) noexcept;            // invert polarity
    void normalizeFrame (int frame) noexcept;       // remove DC, peak to 1.0
    void generateFrame (int frame, ShapeKind kind) noexcept;

    // --- audio / DSP side ---
    const float* frameData (int frame) const noexcept;
    bool consumeUpdate (juce::uint64& maskOut) noexcept
    {
        maskOut = dirtyMask.exchange (0, std::memory_order_acq_rel);
        return updateRequested.exchange (false, std::memory_order_acq_rel);
    }

    void buildTable (std::vector<float>& table, int tableSize, float pos01) const;
    float sample (float phase01, float pos01) const noexcept;      // UI: morphed
    float sampleFrame (float phase01, int frame) const noexcept;   // UI: one frame

private:
    std::array<std::array<float, size>, numFrames> edit {};
    std::array<std::array<float, size>, numFrames> pub[2] {};
    std::atomic<int> active { 0 };
    std::atomic<bool> editDirty { false };          // edit surface has unsent changes
};

// Band-limited mip chain per frame, built from a UserWave via FFT.
class WavetableBank
{
public:
    static constexpr int tableSize = 2048;
    static constexpr int mipCount  = 9;            // 2048, 1024, ... 4
    static constexpr int frameCount = UserWave::numFrames;

    void rebuild (const UserWave& wave, juce::uint64 frameMask);   // audio thread
    int numMips() const noexcept { return mipCount; }
    const float* mip (int n) const noexcept { return mips[(size_t) n].data(); }
    const float* mipFrame (int mip, int frame) const noexcept
    {
        return mips[(size_t) (mip * frameCount + frame)].data();
    }

private:
    // index = mip * frameCount + frame
    std::array<std::vector<float>, mipCount * frameCount> mips;
};

inline float ld (const std::atomic<float>* a) noexcept
{
    return a != nullptr ? a->load (std::memory_order_relaxed) : 0.0f;
}

// Sidechain pump dip gain at fractional phase through the pump period
// (0 = deepest, right on the beat boundary; ->1 = fully recovered). Unity
// when depth is 0; the dip is hard-capped at 95% (-26 dB) for safety.
inline float pumpGain (double phase, float depth) noexcept
{
    if (depth <= 0.001f)
        return 1.0f;
    const double p = juce::jlimit (0.0, 1.0, phase);
    const float dip = depth * (float) std::pow (1.0 - p, 2.5);
    return 1.0f - juce::jlimit (0.0f, 0.95f, dip);
}

struct EngineParams
{
    std::atomic<float>* osc1Wave = nullptr;
    std::atomic<float>* osc1Oct = nullptr;
    std::atomic<float>* osc1Fine = nullptr;
    std::atomic<float>* osc1Level = nullptr;
    std::atomic<float>* osc1Pan = nullptr;
    std::atomic<float>* osc1Phase = nullptr;
    std::atomic<float>* osc1PRand = nullptr;
    std::atomic<float>* osc1WtPos = nullptr;       // wavetable position (morph)
    UserWave* osc1User = nullptr;
    std::atomic<float>* osc2Wave = nullptr;
    std::atomic<float>* osc2Oct = nullptr;
    std::atomic<float>* osc2Fine = nullptr;
    std::atomic<float>* osc2Level = nullptr;
    std::atomic<float>* osc2Pan = nullptr;
    std::atomic<float>* osc2Phase = nullptr;
    std::atomic<float>* osc2PRand = nullptr;
    std::atomic<float>* osc2WtPos = nullptr;
    UserWave* osc2User = nullptr;
    std::atomic<float>* fmAmount = nullptr;
    std::atomic<float>* subWave = nullptr;
    std::atomic<float>* subOct = nullptr;
    std::atomic<float>* subLevel = nullptr;
    std::atomic<float>* noiseLevel = nullptr;
    std::atomic<float>* uniVoices = nullptr;
    std::atomic<float>* uniDetune = nullptr;
    std::atomic<float>* uniSpread = nullptr;
    std::atomic<float>* drift = nullptr;
    std::atomic<float>* filterType = nullptr;
    std::atomic<float>* cutoff = nullptr;
    std::atomic<float>* reso = nullptr;
    std::atomic<float>* filter2Type = nullptr;
    std::atomic<float>* cutoff2 = nullptr;
    std::atomic<float>* reso2 = nullptr;
    std::atomic<float>* filterRoute = nullptr;
    std::atomic<float>* envAmt = nullptr;
    std::atomic<float>* keytrack = nullptr;
    std::atomic<float>* drive = nullptr;
    std::atomic<float>* modDepth = nullptr;
    std::atomic<float>* filtA = nullptr;
    std::atomic<float>* filtD = nullptr;
    std::atomic<float>* filtS = nullptr;
    std::atomic<float>* filtR = nullptr;
    std::atomic<float>* ampA = nullptr;
    std::atomic<float>* ampD = nullptr;
    std::atomic<float>* ampS = nullptr;
    std::atomic<float>* ampR = nullptr;
    std::atomic<float>* lfo1Rate = nullptr;
    std::atomic<float>* lfo1Wave = nullptr;
    std::atomic<float>* lfo1Target = nullptr;
    std::atomic<float>* lfo1Depth = nullptr;
    std::atomic<float>* lfo1Unit = nullptr;
    std::atomic<float>* lfo1Div = nullptr;
    std::atomic<float>* lfo2Rate = nullptr;
    std::atomic<float>* lfo2Wave = nullptr;
    std::atomic<float>* lfo2Target = nullptr;
    std::atomic<float>* lfo2Depth = nullptr;
    std::atomic<float>* lfo2Unit = nullptr;
    std::atomic<float>* lfo2Div = nullptr;
    std::atomic<float>* chorusRate = nullptr;
    std::atomic<float>* chorusDepth = nullptr;
    std::atomic<float>* chorusMix = nullptr;
    std::atomic<float>* phRate = nullptr;
    std::atomic<float>* phDepth = nullptr;
    std::atomic<float>* phMix = nullptr;
    std::atomic<float>* delaySync = nullptr;
    std::atomic<float>* delayTime = nullptr;
    std::atomic<float>* delayFb = nullptr;
    std::atomic<float>* delayMix = nullptr;
    std::atomic<float>* revSize = nullptr;
    std::atomic<float>* revDamp = nullptr;
    std::atomic<float>* revMix = nullptr;
    std::atomic<float>* masterGain = nullptr;
    std::atomic<float>* glide = nullptr;
    std::atomic<float>* voicing = nullptr;
    std::atomic<float>* polyMax = nullptr;
    std::atomic<float>* bendRange = nullptr;

    // Scale quantizer: arpScale indexes tuning::scales(), arpRoot is a pitch
    // class 0..11; scaleLock extends the snap to live playing in noteOn().
    std::atomic<float>* arpScale  = nullptr;
    std::atomic<float>* arpRoot   = nullptr;
    std::atomic<float>* scaleLock = nullptr;

    // Analog character: tape-style wow + flutter plus slow per-voice pitch
    // drift. 0 = off, 1 = strongly audible (about +-25 cents combined).
    std::atomic<float>* analogAmt = nullptr;

    // Filter character: pre-filter saturation + one-sample resonant feedback
    // into the main filter (scream/screech at high settings).
    std::atomic<float>* fDrive    = nullptr;
    std::atomic<float>* fFeedback = nullptr;

    // Master quality: renders the osc -> filter path at 2x oversampling
    // (halves in-band aliasing from the band-limited oscillators, wavetable
    // playback and the saturating stages) at the cost of extra CPU.
    std::atomic<float>* masterHQ = nullptr;

    // Vowel/formant filter (morphing AH-EH-EE-OH-OO bank after the filters).
    std::atomic<float>* vowelOn    = nullptr;
    std::atomic<float>* vowelMorph = nullptr;
    std::atomic<float>* vowelRes   = nullptr;
    std::atomic<float>* vowelMix   = nullptr;

    // FX duck (0 = off) and reverb shimmer (0 = off). Duck is consumed in
    // the processor's FX chain; shimmer is a processor-side send.
    std::atomic<float>* duckAmt    = nullptr;
    std::atomic<float>* revShimmer = nullptr;

    // Performance macros (0..1 knobs; also MIDI CC 14 / CC 15) and the
    // A/B matrix bank switch.
    std::atomic<float>* macroA = nullptr;
    std::atomic<float>* macroB = nullptr;
    std::atomic<float>* modBank = nullptr;

    // Sidechain pump (processor-side: choice sync + depth; OFF by default).
    std::atomic<float>* pumpSync  = nullptr;
    std::atomic<float>* pumpDepth = nullptr;

    // Global microtuning fine offset in cents (-100..+100); the .scl table
    // itself lives in GoaSynth::scala (loaded from the UI, not a parameter).
    std::atomic<float>* tuningFine = nullptr;

    // Supersaw character (uniMode: 0 CLASSIC, 1 PHASED, 2 HYPER) and chord
    // memory (chordMode: 0 OFF, 1 5TH, 2 MINOR, 3 MAJOR, 4 OCT).
    std::atomic<float>* uniMode   = nullptr;
    std::atomic<float>* chordMode = nullptr;
};

struct PolyOsc
{
    double phase = 0.0;
    int wave = 0;
    const WavetableBank* userBank = nullptr;

    void resetPhase (float p01) noexcept { phase = (double) p01; }

    static float polyBlep (float t, float dt) noexcept;

    float next (float dt, float fmCycles, float pwmWidth) noexcept;
    float nextUser (float dt, float wtPos01) noexcept;   // wavetable playback, mip-mapped, morphing

    // Band-limited square for the sub oscillator (polyBLEP on the raw square).
    float nextSquare (float dt) noexcept
    {
        dt = juce::jmin (juce::jmax (dt, 1.0e-6f), 0.5f);
        phase += (double) dt;
        phase -= std::floor (phase);
        const float p = (float) phase;
        float v = p < 0.5f ? 1.0f : -1.0f;
        v -= polyBlep (p, dt);
        v += polyBlep (std::fmod (p + 0.5f, 1.0f), dt);
        return v;
    }
};

struct Lfo
{
    double phase = 0.0;
    int waveType = 0;
    float rate = 1.0f;
    float sh = 0.0f;
    juce::Random rng;

    float next (float sr) noexcept;
};

//==============================================================================
// Modulation matrix: 8 slots routing LFOs, envelopes, velocity and modwheel
// onto parameter destinations. Slots are rebuilt per block from the choice/int
// params; the per-voice renderer applies each destination's summed amount.
struct ModMatrix
{
    struct Slot
    {
        int   src = 0;        // modSourceName() index (0 = OFF)
        int   dst = 0;        // modDestList() index (0 = OFF)
        float amt = 0.0f;     // -1..+1, scaled by ModDest::range
        int   curve = 0;      // 0 LIN, 1 EXP, 2 SIN — shapes |source| response
        float lag = 0.0f;     // 0..1 slew: 0 = none, 1 = ~0.5 s one-pole lag
    };

    Slot slots[param::modSlots];

    // Summed, range-scaled modulation on every destination for one voice at
    // one sample: one pass over the slots. New sources: 7 = sample & hold
    // (block-rate dice via engine->shValue), 8 = channel aftertouch,
    // 9/10 = MACRO A/B. Curve shapes, lag slews per slot (memory lives in
    // the owning voice's LagState, so polyphony keeps one ramp per voice).
    struct Values
    {
        float oct = 0.0f;       // mdCutoff (octaves)
        float cents1 = 0.0f, cents2 = 0.0f;
        float lvl1 = 0.0f, lvl2 = 0.0f;   // 0..1 add/mul on the knob value
        float wt1 = 0.0f, wt2 = 0.0f;
        float reso = 0.0f, drive = 0.0f;
        float pan1 = 0.0f, pan2 = 0.0f;   // -1..1
        float lfoRate1 = 0.0f, lfoRate2 = 0.0f; // Hz
    };

    // Per-voice lag memory: one smoothed value per slot. Zero-initialized =
    // no lag applied on the first block (sources start at 0 anyway).
    struct LagState
    {
        float v[param::modSlots] = {};
        void reset() noexcept { for (auto& x : v) x = 0.0f; }
    };

    void computeAll (Values& out, float lfo1v, float lfo2v,
                     float envF, float envA, float vel, float mw,
                     float at, float sh, float macA, float macB,
                     LagState& lag, float sr, double dt) const noexcept
    {
        int idx = 0;
        for (const auto& s : slots)
        {
            if (s.dst != 0 && s.src != 0 && s.amt != 0.0f)
            {
                float v = 0.0f;
                switch (s.src)
                {
                    case 1: v = lfo1v; break;
                    case 2: v = lfo2v; break;
                    case 3: v = envF;  break;
                    case 4: v = envA;  break;
                    case 5: v = vel;   break;
                    case 6: v = mw;    break;
                    case 7: v = sh;    break;
                    case 8: v = at;    break;
                    case 9: v = macA;  break;
                    case 10: v = macB; break;
                    default: break;
                }
                // Curve shaping (bipolar-preserving: |x|^p keeps the sign).
                const float av = std::abs (v);
                if      (s.curve == 1) v = std::copysign (av * av, v);
                else if (s.curve == 2) v = std::sin (av * 1.5707963f)
                                          * (v < 0.0f ? -1.0f : 1.0f);
                float a = s.amt * v;
                // Lag: one-pole slew on the final scaled amount, per slot.
                if (s.lag > 0.001f && sr > 0.0f)
                {
                    const float g = 1.0f - (float) std::exp (-dt / juce::jmax (1.0e-4f,
                        0.15f * s.lag * s.lag));
                    float& mem = lag.v[idx];
                    mem += g * (a - mem);
                    a = mem;
                }
                switch (s.dst)
                {
                    case 1:  out.oct += a * 4.0f; break;
                    case 2:  out.cents1 += a * 1200.0f; break;
                    case 3:  out.cents2 += a * 1200.0f; break;
                    case 4:  out.lvl1 += a; break;
                    case 5:  out.lvl2 += a; break;
                    case 6:  out.wt1 += a; break;
                    case 7:  out.wt2 += a; break;
                    case 8:  out.reso += a; break;
                    case 9:  out.drive += a; break;
                    case 10: out.pan1 += a; break;
                    case 11: out.pan2 += a; break;
                    case 12: out.lfoRate1 += a * 6.0f; break;
                    case 13: out.lfoRate2 += a * 6.0f; break;
                    default: break;   // global FX destinations: handled per block
                }
            }
            ++idx;
        }
    }

    // ---- global FX destinations (ModDest rows mdDelayTime..mdRevMix) -------
    // FX like the delay run once for the whole mix, not per voice, so voices
    // publish their summed amounts into these atomics (largest magnitude wins
    // — "the deepest modulation drives the shared FX") and processBlock reads
    // them once per block. Voices publish every 64 samples: macro FX movement
    // needs no more resolution, and 6 relaxed stores per voice per 64 samples
    // is nothing next to the audio math.
    struct Global
    {
        std::atomic<float> delayTime { 0.0f }, delayFb { 0.0f }, revSize { 0.0f },
                           delayMix { 0.0f }, phMix { 0.0f }, revMix { 0.0f };
    };
    Global globalFx;

    void publishGlobal (float lfo1v, float lfo2v,
                        float envF, float envA, float vel, float mw,
                        float at, float sh, float macA, float macB) noexcept
    {
        float dt = 0.0f, df = 0.0f, rs = 0.0f, dm = 0.0f, pm = 0.0f, rm = 0.0f;
        for (const auto& s : slots)
        {
            if (s.dst < 14 || s.src == 0 || s.amt == 0.0f)
                continue;
            float v = 0.0f;
            switch (s.src)
            {
                case 1: v = lfo1v; break;
                case 2: v = lfo2v; break;
                case 3: v = envF;  break;
                case 4: v = envA;  break;
                case 5: v = vel;   break;
                case 6: v = mw;    break;
                case 7: v = sh;    break;
                case 8: v = at;    break;
                case 9: v = macA;  break;
                case 10: v = macB; break;
                default: break;
            }
            const float a = s.amt * v;
            // Range multipliers mirror modDestList(): delay time ±2 octaves of
            // length, the mix/fb/size knobs ±0.6/±0.4 of their own travel.
            switch (s.dst)
            {
                case 14: { const float m = a;         if (std::abs (m) > std::abs (dt)) dt = m; break; }
                case 15: { const float m = a * 0.6f;  if (std::abs (m) > std::abs (df)) df = m; break; }
                case 16: { const float m = a * 0.4f;  if (std::abs (m) > std::abs (rs)) rs = m; break; }
                case 17: { const float m = a * 0.6f;  if (std::abs (m) > std::abs (dm)) dm = m; break; }
                case 18: { const float m = a * 0.6f;  if (std::abs (m) > std::abs (pm)) pm = m; break; }
                case 19: { const float m = a * 0.6f;  if (std::abs (m) > std::abs (rm)) rm = m; break; }
                default: break;
            }
        }
        globalFx.delayTime.store (dt, std::memory_order_relaxed);
        globalFx.delayFb.store  (df, std::memory_order_relaxed);
        globalFx.revSize.store  (rs, std::memory_order_relaxed);
        globalFx.delayMix.store (dm, std::memory_order_relaxed);
        globalFx.phMix.store    (pm, std::memory_order_relaxed);
        globalFx.revMix.store   (rm, std::memory_order_relaxed);
    }
};

class GoaSynth;

class GoaSound : public juce::SynthesiserSound
{
public:
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

class GoaVoice : public juce::SynthesiserVoice
{
public:
    explicit GoaVoice (GoaSynth* engine);

    bool canPlaySound (juce::SynthesiserSound* sound) override;
    void startNote (int midiNote, float velocity, juce::SynthesiserSound*, int pitchWheel) override;
    void glideTo (int midiNote, float frequencyHz);
    void retrig();
    int getCurrentNote() const noexcept { return currentNote; }
    void stopNote (float velocity, bool allowTailOff) override;
    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}
    bool isVoiceActive() const override;
    void renderNextBlock (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) override;

private:
    void resetVoice();

    GoaSynth* engine = nullptr;
    PolyOsc osc1[maxUnison];
    PolyOsc osc2[maxUnison];
    PolyOsc subOsc;
    Lfo lfo1, lfo2;
    juce::ADSR ampEnv, filtEnv;
    juce::dsp::StateVariableTPTFilter<float> svfL1, svfR1, svfL2, svfR2;
    juce::dsp::StateVariableTPTFilter<float> vowL[3], vowR[3]; // vowel/formant bank

    // 2x oversampling (masterHQ): the odd sub-sample passes through this
    // second filter-state set so both sub-samples keep independent filter
    // history; results are averaged into the output.
    juce::dsp::StateVariableTPTFilter<float> svfL1b, svfR1b, svfL2b, svfR2b;
    juce::dsp::StateVariableTPTFilter<float> vowLb[3], vowRb[3];
    juce::Random noiseRnd;
    float velocity = 1.0f;

    // Modulation matrix: recomputed once per block from the previous block's
    // latched per-sample sources (LFOs/envelopes), then consumed per sample.
    // One block of latency on the mod path; the envelopes being 0 on the very
    // first block after note-on is inaudible.
    ModMatrix::Values mv;
    ModMatrix::LagState modLag;
    float latchedL1 = 0.0f, latchedL2 = 0.0f;
    float latchedFe = 0.0f, latchedAe = 0.0f;
    float targetFreq = 440.0f;
    float glideFreq = 440.0f;
    float cutoffSmooth = 1000.0f;
    float cutoffLog = -1.0f;          // log-domain smoother state (log2 Hz)
    float cutoff2Smooth = 8000.0f;    // FILTER B smoothed cutoff (Hz)
    float dcX1L = 0.0f, dcY1L = 0.0f; // DC blocker (post-drive), left
    float dcX1R = 0.0f, dcY1R = 0.0f; // ... and right
    float dcX1Lb = 0.0f, dcY1Lb = 0.0f; // DC blocker, 2x odd sub-sample path
    float dcX1Rb = 0.0f, dcY1Rb = 0.0f;
    float lastSR = 0.0f;
    float driftOffset = 0.0f;
    float wowPhase = 0.0f;            // analog character: wow LFO phase
    float flutterPhase = 0.0f;        // analog character: flutter phase
    float filtFbL = 0.0f;             // filter feedback memory (left)
    float filtFbR = 0.0f;             // filter feedback memory (right)
    float filtFbLb = 0.0f;            // filter feedback memory, odd sub-sample
    float filtFbRb = 0.0f;
    int currentNote = 60;
    int curFiltType = -1;
    int curFilt2Type = -1;
    bool vowActive = false;              // vowel filter enabled this block
    float vowMixS = 0.0f;                // vowel dry/wet
    float lastVowRes = -1.0f;            // vowel resonance change tracker
    float detune1F[maxUnison] = {};
    float detune2F[maxUnison] = {};
    float panL[maxUnison] = {};
    float panR[maxUnison] = {};
};

class GoaSynth : public juce::Synthesiser
{
public:
    EngineParams p;
    std::atomic<float> pitchBend { 0.0f };
    std::atomic<float> modWheel { 0.0f };
    std::atomic<float> lastFreq { 440.0f };
    std::atomic<double> currentBpm { 120.0 };

    // Live mod-source snapshots for the editor's animated mod dots (indicator
    // only: whichever voice writes last wins, ordering doesn't matter for a
    // pulsing dot).
    std::atomic<float> uiLfo1 { 0.0f }, uiLfo2 { 0.0f }, uiEnvF { 0.0f },
                       uiEnvA { 0.0f }, uiVelocity { 0.0f },
                       uiAt { 0.0f }, uiSH { 0.0f },
                       uiMacroA { 0.0f }, uiMacroB { 0.0f };

    // Block-rate sample & hold for matrix source 7: one random -1..1 value
    // per audio block (all voices share the same dice).
    std::atomic<float> shValue { 0.0f };

    // Performance macros (0..1), also driven by MIDI CC 14 / CC 15.
    std::atomic<float> macroA { 0.0f }, macroB { 0.0f };

    // Modulation matrix snapshot, rebuilt from the choice/int params each block
    // (host side) and read per-voice (audio side). Plain struct: slots are
    // rebuilt before renderNextBlock, no torn reads in practice.
    ModMatrix mod;

    GoaSynth();

    // Frequency of a MIDI note: the loaded .scl table when present, else
    // 12-TET; the global fine-tune (cents, tuningFine param) applies on top.
    float noteFreq (int midiNote) const
    {
        return scala.frequencyForNote (midiNote) * std::exp2 (ld (p.tuningFine) / 1200.0f);
    }

    // Rebuild audio-side wavetables from the current user shapes (call from UI
    // after edits; cheap, no allocations after the first build).
    void commitWaves();

    UserWave userA, userB;
    WavetableBank bankA, bankB;
    tuning::ScalaTable scala;            // microtuning table (.scl from the UI)

    // Edit surfaces for the wavetable editor UI (message thread only).
    UserWave* userWave (int oscIndex) noexcept
    {
        return oscIndex == 0 ? &userA : &userB;
    }

    void noteOn (int midiChannel, int midiNoteNumber, float velocity) override;
    void noteOff (int midiChannel, int midiNoteNumber, float velocity, bool allowTailOff) override;
    void handleController (int midiChannel, int controllerNumber, int controllerValue) override;
    void handlePitchWheel (int midiChannel, int wheelValue) override;
    void handleChannelPressure (int midiChannel, int channelPressureValue) override;
    void allNotesOff (int midiChannel, bool allowTailOff) override;

private:
    void pedalUp();

    // A note can be sounding because it is held directly *and* because it is a
    // chord-memory interval of another held key. It is reference counted so the
    // voice is only stopped once the LAST claim on it is released - releasing a
    // key that is also another key's chord tone must not silence that tone.
    // (A count, not a set, because a key can be pressed twice while still
    // claimed; JUCE's Synthesiser keeps at most one voice per note, so the
    // note-off is sent on the transition to zero.)
    // Indexed by MIDI note number, so this costs no allocation on the audio thread.
    void addHeldNote (int note);
    bool releaseHeldNote (int note);   // true when the last claim was released

    bool pedalDown = false;
    juce::Array<int> sustainedNotes;
    juce::Array<int> heldNotes; // most-recent-first tracking for mono/legato return
    int heldRefs[128] = {};

    // Chord tones noteOn actually started for each held root (-1 = none), so
    // noteOff releases exactly those instead of recomputing them from the live
    // CHORD/SCALE parameters, which may have changed while the key was down.
    int chordTones[128][3];
};

} // namespace goa
