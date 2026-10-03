#include "SynthEngine.h"

namespace goa
{

//==============================================================================
// UserWave: 8-frame morphable wavetable. The UI sketches into an edit surface
// and publish() flips it visible for the audio thread (double buffered).
//==============================================================================
void UserWave::beginEdit() noexcept
{
    const int a = active.load (std::memory_order_acquire);
    for (int f = 0; f < numFrames; ++f)
        edit[(size_t) f] = pub[(size_t) a][(size_t) f];
}

void UserWave::write (int frame, float x01, float value, int radius) noexcept
{
    auto* shape = editFrame (frame);
    if (shape == nullptr)
        return;

    const int centre = (int) std::round (juce::jlimit (0.0f, 1.0f, x01) * (float) (size - 1));
    const int r = juce::jlimit (0, size / 4, radius);
    const float v = juce::jlimit (-1.0f, 1.0f, value);

    for (int d = -r; d <= r; ++d)
    {
        const int i = juce::jlimit (0, size - 1, centre + d);
        const float x = (float) std::abs (d) / (float) (r + 1);
        const float w = 1.0f - x * x;                       // cosine falloff
        shape[i] = shape[i] + (v - shape[i]) * w;           // pull toward stroke value
    }

    markEditDirty();
}

void UserWave::clearEditFrame (int frame) noexcept
{
    edit[(size_t) juce::jlimit (0, numFrames - 1, frame)].fill (0.0f);
    markEditDirty();
}

void UserWave::publish() noexcept
{
    const int next = 1 - active.load (std::memory_order_relaxed);
    for (int f = 0; f < numFrames; ++f)
        pub[(size_t) next][(size_t) f] = edit[(size_t) f];
    active.store (next, std::memory_order_release);
    updateRequested.store (true, std::memory_order_release);
    dirtyMask.store ((juce::uint64) (1 << numFrames) - 1, std::memory_order_release);
    editDirty.store (false, std::memory_order_release);
}

void UserWave::getFrame (int frame, float* dst, int n) const noexcept
{
    const int f = juce::jlimit (0, numFrames - 1, frame);
    const auto& src = pub[(size_t) active.load (std::memory_order_acquire)][(size_t) f];
    if (n >= size)
    {
        for (int i = 0; i < size; ++i) dst[i] = src[(size_t) i];
        for (int i = size; i < n; ++i) dst[i] = src[(size_t) (size - 1)];
    }
    else
    {
        for (int i = 0; i < n; ++i)
        {
            const float t = (float) i / (float) n * (float) size;
            const int i0 = (int) t;
            const int i1 = juce::jmin (i0 + 1, size - 1);
            dst[i] = src[(size_t) i0] + (src[(size_t) i1] - src[(size_t) i0]) * (t - (float) i0);
        }
    }
}

void UserWave::setFrame (int frame, const float* src, int n) noexcept
{
    auto* shape = editFrame (frame);
    if (shape == nullptr || src == nullptr || n <= 0)
        return;
    for (int i = 0; i < size; ++i)
        shape[i] = (i < n) ? juce::jlimit (-1.0f, 1.0f, src[i]) : src[n - 1];
    markEditDirty();
}

void UserWave::smoothFrame (int frame) noexcept
{
    auto& s = edit[(size_t) juce::jlimit (0, numFrames - 1, frame)];
    std::array<float, size> out {};
    for (int i = 0; i < size; ++i)
    {
        const float a = s[(size_t) ((i + size - 1) % size)];
        const float b = s[(size_t) i];
        const float c = s[(size_t) ((i + 1) % size)];
        out[(size_t) i] = 0.25f * a + 0.5f * b + 0.25f * c;   // binomial, wraparound
    }
    s = out;
    markEditDirty();
}

void UserWave::flipFrame (int frame) noexcept
{
    auto& s = edit[(size_t) juce::jlimit (0, numFrames - 1, frame)];
    for (auto& v : s)
        v = -v;
    markEditDirty();
}

void UserWave::normalizeFrame (int frame) noexcept
{
    auto& s = edit[(size_t) juce::jlimit (0, numFrames - 1, frame)];
    double sum = 0.0;
    for (float v : s) sum += (double) v;
    const float dc = (float) (sum / (double) size);
    for (auto& v : s) v -= dc;

    float peak = 1.0e-9f;
    for (float v : s) peak = juce::jmax (peak, std::fabs (v));
    const float g = 1.0f / peak;
    for (auto& v : s) v *= g;
    markEditDirty();
}

void UserWave::generateFrame (int frame, ShapeKind kind) noexcept
{
    auto& s = edit[(size_t) juce::jlimit (0, numFrames - 1, frame)];

    switch (kind)
    {
        case ShapeKind::pulse25:
        case ShapeKind::pulse50:
        {
            const float pw = kind == ShapeKind::pulse50 ? 0.5f : 0.25f;
            for (int i = 0; i < size; ++i)
            {
                const float t = (float) i / (float) size;
                s[(size_t) i] = t < pw ? 1.0f : -1.0f;
            }
            break;
        }
        case ShapeKind::formant:
        {
            for (int i = 0; i < size; ++i)
            {
                const float t = (float) i / (float) size;
                // band-passed resonance burst: carrier modulated by a sharp
                // sinc-like envelope that completes its cycle at 4x pitch
                const float carrier = std::sin (juce::MathConstants<float>::twoPi * t);
                const float env = std::sin (juce::MathConstants<float>::pi * t);
                const float ring = std::sin (juce::MathConstants<float>::twoPi * 4.0f * t);
                s[(size_t) i] = carrier * env * ring;
            }
            break;
        }
        case ShapeKind::spikes:
        {
            for (int i = 0; i < size; ++i)
            {
                const float t = (float) i / (float) size;
                float v = std::sin (juce::MathConstants<float>::twoPi * t) * 0.2f;
                for (int k = 1; k <= 4; ++k)
                    v += std::cos (juce::MathConstants<float>::twoPi
                                   * (float) (8 * k) * t) / (float) k;
                s[(size_t) i] = v * 0.4f;   // keep within +/-1; NORM restores level
            }
            break;
        }
    }
    markEditDirty();
}

void UserWave::reset()
{
    for (int i = 0; i < size; ++i)
        edit[0][(size_t) i] = 2.0f * (float) i / (float) (size - 1) - 1.0f;
    for (int f = 1; f < numFrames; ++f)
        edit[(size_t) f].fill (0.0f);
    publish();
}

bool UserWave::isEditDirty() const noexcept
{
    return editDirty.load (std::memory_order_acquire);
}

void UserWave::markEditDirty() noexcept
{
    editDirty.store (true, std::memory_order_release);
}

const float* UserWave::frameData (int frame) const noexcept
{
    const int f = juce::jlimit (0, numFrames - 1, frame);
    return pub[(size_t) active.load (std::memory_order_acquire)][(size_t) f].data();
}

void UserWave::buildTable (std::vector<float>& table, int tableSize, float pos01) const
{
    const float pf = juce::jlimit (0.0f, 1.0f, pos01) * (float) (numFrames - 1);
    const int   f0 = (int) pf;
    const int   f1 = juce::jmin (f0 + 1, numFrames - 1);
    const float fr = pf - (float) f0;

    const int a = active.load (std::memory_order_acquire);
    table.resize ((size_t) tableSize);
    for (int i = 0; i < tableSize; ++i)
    {
        const float t = (float) i / (float) tableSize * (float) size;
        const int i0 = (int) t;
        const int i1 = juce::jmin (i0 + 1, size - 1);
        const auto& sa = pub[(size_t) a][(size_t) f0];
        const auto& sb = pub[(size_t) a][(size_t) f1];
        const float va = sa[(size_t) i0] + (sa[(size_t) i1] - sa[(size_t) i0]) * (t - (float) i0);
        const float vb = sb[(size_t) i0] + (sb[(size_t) i1] - sb[(size_t) i0]) * (t - (float) i0);
        table[(size_t) i] = va + (vb - va) * fr;
    }

    // Remove the DC offset a hand-drawn shape tends to accumulate, and normalise.
    double sum = 0.0;
    for (int i = 0; i < tableSize; ++i) sum += table[(size_t) i];
    const float dc = (float) (sum / (double) tableSize);
    double peak = 1.0e-9;
    for (int i = 0; i < tableSize; ++i)
    {
        table[(size_t) i] -= dc;
        peak = juce::jmax ((double) peak, (double) std::fabs (table[(size_t) i]));
    }
    const float g = 1.0f / (float) peak;
    if (g < 1.0f)
        for (int i = 0; i < tableSize; ++i) table[(size_t) i] *= g;
}

float UserWave::sampleFrame (float phase01, int frame) const noexcept
{
    const int f = juce::jlimit (0, numFrames - 1, frame);
    const auto& s = pub[(size_t) active.load (std::memory_order_acquire)][(size_t) f];
    const float t = juce::jlimit (0.0f, 0.99999f, phase01) * (float) (size - 1);
    const int i0 = (int) t;
    const int i1 = juce::jmin (i0 + 1, size - 1);
    return s[(size_t) i0] + (s[(size_t) i1] - s[(size_t) i0]) * (t - (float) i0);
}

float UserWave::sample (float phase01, float pos01) const noexcept
{
    const float pf = juce::jlimit (0.0f, 1.0f, pos01) * (float) (numFrames - 1);
    const float ff = std::floor (pf);
    const float fr = pf - ff;
    const int   f0 = (int) ff;
    const int   f1 = juce::jmin (f0 + 1, numFrames - 1);
    if (f0 == f1)
        return sampleFrame (phase01, f0);
    return sampleFrame (phase01, f0) * (1.0f - fr)
         + sampleFrame (phase01, f1) * fr;
}

//==============================================================================
// WavetableBank: per-frame band-limited mip chains. Each frame's partials are
// measured once (magnitude + phase); every mip re-synthesises the partials it
// can hold, with a raised-cosine fade over the top of its band so nothing
// aliases and drawn shapes keep their partial phases.
//==============================================================================
void WavetableBank::rebuild (const UserWave& wave, juce::uint64 frameMask)
{
    for (int f = 0; f < frameCount; ++f)
    {
        if ((frameMask & ((juce::uint64) 1 << f)) == 0)
            continue;

        // Time-domain table for this exact frame (DC-free, normalised).
        std::vector<float> full;
        wave.buildTable (full, tableSize, (float) f / (float) (frameCount - 1));

        // Measure the partials: magnitude + phase per harmonic.
        std::vector<float> mag ((size_t) (tableSize / 2 + 1), 0.0f);
        std::vector<float> pha ((size_t) (tableSize / 2 + 1), 0.0f);
        {
            const int step = juce::jmax (1, tableSize / 1024);
            const int used = (tableSize + step - 1) / step;
            for (int h = 1; h <= tableSize / 2; ++h)
            {
                double re = 0.0, im = 0.0;
                for (int i = 0; i < tableSize; i += step)
                {
                    const double ang = juce::MathConstants<double>::twoPi
                                     * (double) h * (double) i / (double) tableSize;
                    re += (double) full[(size_t) i] * std::cos (ang);
                    im -= (double) full[(size_t) i] * std::sin (ang);
                }
                mag[(size_t) h] = (float) (2.0 * std::sqrt (re * re + im * im) / (double) used);
                pha[(size_t) h] = (float) std::atan2 (im, re);
            }
        }

        // Rough harmonic count of the drawn shape (harmless to over-estimate).
        double delta = 0.0;
        for (int i = 1; i < tableSize; ++i)
            delta += std::fabs (full[(size_t) i] - full[(size_t) i - 1]);
        const int harm = juce::jlimit (4, tableSize / 4, (int) (delta * 0.5 * (double) tableSize / 4.0));

        for (int m = 0; m < mipCount; ++m)
        {
            const int n = tableSize >> m;
            auto& t = mips[(size_t) (m * frameCount + f)];
            t.assign ((size_t) n, 0.0f);

            // This mip can only hold harmonics up to n/2 - 1 without aliasing.
            const int nMax = n / 2 - 1;
            const int fadeStart = juce::jmax (1, nMax - juce::jmax (2, nMax / 8));
            const int nHarm = juce::jmin (harm, nMax);

            for (int h = 1; h <= nHarm; ++h)
            {
                const float a = mag[(size_t) h];
                if (a <= 0.0f)
                    continue;

                // Raised-cosine fade over the top eighth of this mip's band.
                const float w = h <= fadeStart ? 1.0f
                    : 0.5f * (1.0f + std::cos (juce::MathConstants<float>::pi
                        * juce::jlimit (0.0f, 1.0f, (float) (h - fadeStart)
                                                 / (float) juce::jmax (1, nMax - fadeStart))));
                const float amp = a * w;
                const float ph = pha[(size_t) h];
                const float twopiOverN = juce::MathConstants<float>::twoPi / (float) n;
                for (int i = 0; i < n; ++i)
                    t[(size_t) i] += amp * std::sin (twopiOverN * (float) h * (float) i + ph);
            }
        }
    }
}

//==============================================================================
float PolyOsc::polyBlep (float t, float dt) noexcept
{
    if (t < dt)        { const float x = t / dt;         return x + x - x * x - 1.0f; }
    if (t > 1.0f - dt) { const float x = (t - 1.0f) / dt; return x * x + x + x + 1.0f; }
    return 0.0f;
}

float PolyOsc::next (float dt, float fmCycles, float pwmWidth) noexcept
{
    dt = juce::jmin (juce::jmax (dt, 1.0e-6f), 0.5f);
    phase += (double) dt + (double) fmCycles;
    phase -= std::floor (phase);
    const float p = (float) phase;

    switch (wave)
    {
        case 1: // square
        {
            float v = p < 0.5f ? 1.0f : -1.0f;
            v -= polyBlep (p, dt);
            v += polyBlep (std::fmod (p + 0.5f, 1.0f), dt);
            return v;
        }
        case 2: // PWM
        {
            const float w = juce::jlimit (0.05f, 0.95f, pwmWidth);
            float v = p < w ? 1.0f : -1.0f;
            v -= polyBlep (p, dt);
            v += polyBlep (std::fmod (p + (1.0f - w), 1.0f), dt);
            return v;
        }
        case 3: // triangle
            return 1.0f - 4.0f * std::fabs (p - 0.5f);
        case 4: // sine
            return std::sin (juce::MathConstants<float>::twoPi * p);
        default: // saw
        {
            const float v = 2.0f * p - 1.0f;
            return v - polyBlep (p, dt);
        }
    }
}

float PolyOsc::nextUser (float dt, float wtPos01) noexcept
{
    dt = juce::jmin (juce::jmax (dt, 1.0e-6f), 0.5f);
    phase += (double) dt;
    phase -= std::floor (phase);

    if (userBank == nullptr)
        return std::sin (juce::MathConstants<float>::twoPi * (float) phase);

    // Morph position -> adjacent frames (with a short crossfade tail so the
    // band-limiting of neighbouring mips matches across the seam).
    const float pf  = juce::jlimit (0.0f, 1.0f, wtPos01) * (float) (WavetableBank::frameCount - 1);
    const int   f0  = (int) pf;
    const int   f1  = juce::jmin (f0 + 1, WavetableBank::frameCount - 1);
    const float fr  = pf - (float) f0;

    // Pick the mip whose half-period still holds ~8+ harmonics.
    int mip = 0;
    while (mip < WavetableBank::mipCount - 1 && dt > 0.25f / (float) (1 << mip))
        ++mip;

    const int      n     = WavetableBank::tableSize >> mip;
    const float    pos   = (float) phase * (float) n;
    const int      i0    = (int) pos;
    const int      i1    = (i0 + 1) & (n - 1);
    const float    frac  = pos - (float) i0;

    const float* t0 = userBank->mipFrame (mip, f0);
    const float* t1 = userBank->mipFrame (mip, f1);

    const float v0 = t0[i0] + (t0[i1] - t0[i0]) * frac;
    if (f0 == f1)
        return v0;
    const float v1 = t1[i0] + (t1[i1] - t1[i0]) * frac;
    return v0 + (v1 - v0) * fr;
}

//==============================================================================
float Lfo::next (float sr) noexcept
{
    phase += (double) rate / (double) juce::jmax (sr, 1.0f);
    if (phase >= 1.0)
    {
        phase -= std::floor (phase);
        if (waveType == 3)
            sh = rng.nextFloat() * 2.0f - 1.0f;
    }
    const float p = (float) phase;
    switch (waveType)
    {
        case 1:  return 1.0f - 4.0f * std::fabs (p - 0.5f);
        case 2:  return p < 0.5f ? 1.0f : -1.0f;
        case 3:  return sh;
        default: return std::sin (juce::MathConstants<float>::twoPi * p);
    }
}

//==============================================================================
// Vowel formant table: F1/F2/F3 in Hz with per-band gains (classic male
// vowel averages). The morph position interpolates between adjacent vowels:
// morph 0 = AH, 0.25 = EH, 0.5 = EE, 0.75 = OH, 1.0 = OO.
static constexpr float formants[5][3] =
{
    {  730.0f, 1090.0f, 2440.0f },   // AH
    {  530.0f, 1840.0f, 2480.0f },   // EH
    {  270.0f, 2290.0f, 3010.0f },   // EE
    {  570.0f,  840.0f, 2410.0f },   // OH
    {  300.0f,  870.0f, 2240.0f },   // OO
};

static constexpr float formantGains[5][3] =
{
    { 1.0f, 0.50f, 0.28f },   // AH
    { 1.0f, 0.42f, 0.26f },   // EH
    { 1.0f, 0.30f, 0.20f },   // EE
    { 1.0f, 0.55f, 0.30f },   // OH
    { 1.0f, 0.50f, 0.25f },   // OO
};

//==============================================================================
GoaVoice::GoaVoice (GoaSynth* e) : engine (e) {}

bool GoaVoice::canPlaySound (juce::SynthesiserSound* sound)
{
    return dynamic_cast<const GoaSound*> (sound) != nullptr;
}

void GoaVoice::startNote (int midiNote, float vel, juce::SynthesiserSound*, int)
{
    velocity = juce::jlimit (0.0f, 1.0f, vel);
    mv = ModMatrix::Values();
    modLag.reset();
    latchedL1 = latchedL2 = latchedFe = latchedAe = 0.0f;
    currentNote = midiNote;
    targetFreq = engine->noteFreq (midiNote);   // SCL microtuning + fine offset
    const bool glideOn = ld (engine->p.glide) > 0.5f;
    glideFreq = glideOn ? ld (&engine->lastFreq) : targetFreq;
    cutoffSmooth = juce::jlimit (20.0f, 20000.0f, ld (engine->p.cutoff));
    cutoffLog = std::log2 (cutoffSmooth);
    cutoff2Smooth = juce::jlimit (20.0f, 20000.0f, ld (engine->p.cutoff2));
    curFiltType = -1;
    curFilt2Type = -1;

    juce::Random r;
    const float base1 = ld (engine->p.osc1PRand) > 0.5f ? r.nextFloat()
        : juce::jlimit (0.0f, 0.999f, ld (engine->p.osc1Phase) / 360.0f);
    const float base2 = ld (engine->p.osc2PRand) > 0.5f ? r.nextFloat()
        : juce::jlimit (0.0f, 0.999f, ld (engine->p.osc2Phase) / 360.0f);
    for (auto& o : osc1) o.resetPhase (base1);
    for (auto& o : osc2) o.resetPhase (base2);
    subOsc.resetPhase (r.nextFloat());
    lfo1.phase = r.nextDouble();
    lfo2.phase = r.nextDouble();
    driftOffset = r.nextFloat() * 2.0f - 1.0f;

    ampEnv.reset();
    filtEnv.reset();
    ampEnv.noteOn();
    filtEnv.noteOn();

    engine->lastFreq.store (targetFreq, std::memory_order_relaxed);
}

void GoaVoice::glideTo (int midiNote, float frequencyHz)
{
    currentNote = midiNote;
    targetFreq = frequencyHz;
    if (ld (engine->p.glide) <= 0.5f)
        glideFreq = frequencyHz;
    engine->lastFreq.store (frequencyHz, std::memory_order_relaxed);
}

void GoaVoice::retrig()
{
    ampEnv.reset();
    filtEnv.reset();
    ampEnv.noteOn();
    filtEnv.noteOn();
}

void GoaVoice::stopNote (float, bool allowTailOff)
{
    if (allowTailOff)
    {
        ampEnv.noteOff();
        filtEnv.noteOff();
    }
    else
    {
        resetVoice();
    }
}

bool GoaVoice::isVoiceActive() const
{
    return ampEnv.isActive();
}

void GoaVoice::resetVoice()
{
    ampEnv.reset();
    filtEnv.reset();
    mv = ModMatrix::Values();
    latchedL1 = latchedL2 = latchedFe = latchedAe = 0.0f;
    for (float& s : pinkL) s = 0.0f;   // pink-noise state starts silent per note
    for (float& s : pinkR) s = 0.0f;
    clearCurrentNote();
}

void GoaVoice::renderNextBlock (juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    if (! isVoiceActive() || engine == nullptr)
        return;

    const float sr = (float) getSampleRate();
    if (sr <= 0.0f)
        return;

    auto& p = engine->p;

    // Master quality: 2x oversampled oscillator + filter path. Each output
    // sample renders two half-spaced sub-samples; the odd one keeps its own
    // filter/DC/feedback state and the pair is averaged on output.
    const bool hq = ld (p.masterHQ) > 0.5f;
    const float sr2 = sr * 2.0f;   // sub-sample rate (oscillator phase steps)
    const float srr = hq ? sr2 : sr;   // effective oscillator rate

    if (lastSR != sr)
    {
        cutoffLog = std::log2 (juce::jlimit (20.0f, 20000.0f, cutoffSmooth));
        ampEnv.setSampleRate (sr);
        filtEnv.setSampleRate (sr);
        const juce::dsp::ProcessSpec spec { sr, 512, 1 };
        svfL1.prepare (spec); svfR1.prepare (spec);
        svfL2.prepare (spec); svfR2.prepare (spec);
        svfL1b.prepare (spec); svfR1b.prepare (spec);
        svfL2b.prepare (spec); svfR2b.prepare (spec);
        for (int k = 0; k < 3; ++k)
        {
            vowL[k].prepare (spec); vowR[k].prepare (spec);
            vowL[k].setType (juce::dsp::StateVariableTPTFilterType::bandpass);
            vowR[k].setType (juce::dsp::StateVariableTPTFilterType::bandpass);
            vowLb[k].prepare (spec); vowRb[k].prepare (spec);
            vowLb[k].setType (juce::dsp::StateVariableTPTFilterType::bandpass);
            vowRb[k].setType (juce::dsp::StateVariableTPTFilterType::bandpass);
        }
        lastVowRes = -1.0f;
        lastSR = sr;
    }

    ampEnv.setParameters  ({ ld (p.ampA), ld (p.ampD), ld (p.ampS), ld (p.ampR) });
    filtEnv.setParameters ({ ld (p.filtA), ld (p.filtD), ld (p.filtS), ld (p.filtR) });

    const int   w1 = juce::jlimit (0, 5, (int) ld (p.osc1Wave));
    const int   w2 = juce::jlimit (0, 5, (int) ld (p.osc2Wave));
    const bool  user1 = w1 == 5 && engine->p.osc1User != nullptr;
    const bool  user2 = w2 == 5 && engine->p.osc2User != nullptr;
    const float wtPos1 = ld (p.osc1WtPos);
    const float wtPos2 = ld (p.osc2WtPos);
    const float lvl1 = juce::jlimit (0.0f, 2.0f, ld (p.osc1Level) + mv.lvl1);
    const float lvl2 = juce::jlimit (0.0f, 2.0f, ld (p.osc2Level) + mv.lvl2);
    const float fmAmt = ld (p.fmAmount) * 0.5f;
    // Manual PWM duty: the knob is the base, LFO target 2 (PWM) and the matrix
    // PULSE W destination both offset it. Shared by OSC A and OSC B so a patch
    // with both oscillators in PWM mode stays phase-coherent in width.
    const float pwBase = juce::jlimit (0.05f, 0.95f, ld (p.pulseWidth));
    const float subLvl = ld (p.subLevel);
    const float noiseLvl  = ld (p.noiseLevel);
    const float noiseKind = ld (p.noiseType);   // 0 = white, 1 = pink
    const float octF1 = std::exp2 ((float) (int) ld (p.osc1Oct));
    const float octF2 = std::exp2 ((float) (int) ld (p.osc2Oct));
    const float ampVel = 0.35f + 0.65f * velocity;
    const float envVel = 0.45f + 0.55f * velocity;
    const float baseCut = juce::jlimit (20.0f, 20000.0f, ld (p.cutoff));
    const float kt = ld (p.keytrack);
    const float ktOct = kt * (float) (currentNote - 60) / 12.0f;
    const float reso = juce::jlimit (0.5f, 15.0f, 0.5f + (ld (p.reso) + mv.reso) * 14.5f);
    // FILTER B resonance: the RESO B knob is a 0..1 unit value here (unlike
    // filter A's 0.5..15 mapping), so the matrix destination adds straight on
    // top. Clamped to a legal minimum so a negative amount cannot reach zero.
    const float reso2v = juce::jlimit (0.05f, 15.0f, ld (p.reso2) + mv.reso2);
    const float driveAmt = juce::jlimit (0.0f, 1.0f, ld (p.drive) + mv.drive);
    const float envOct = ld (p.envAmt);
    const float modOct = ld (p.modDepth) * engine->modWheel.load (std::memory_order_relaxed);
    const int uni = juce::jlimit (1, maxUnison, (int) ld (p.uniVoices));
    const float spreadC = ld (p.uniDetune);
    const float width01 = ld (p.uniSpread);
    const float fine1 = ld (p.osc1Fine) + mv.cents1;
    const float fine2 = ld (p.osc2Fine) + mv.cents2;
    const float glideMs = ld (p.glide);
    const float glideCoeff = glideMs > 0.5f ? std::exp (-1.0f / (glideMs * 0.001f * sr)) : 1.0f;
    const int bendRange = (int) ld (p.bendRange);

    const int t1 = (int) ld (p.lfo1Target);
    const int t2 = (int) ld (p.lfo2Target);
    const float d1 = ld (p.lfo1Depth);
    const float d2 = ld (p.lfo2Depth);
    auto syncedRate = [] (int unit, int div, float hz, double bpm) -> float
    {
        if (unit != 1)
            return hz;
        static constexpr float beats[] = { 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.75f };
        const float b = (div >= 0 && div < 6) ? beats[div] : 1.0f;
        return juce::jmax (0.02f, (float) (bpm / 60.0) * b);
    };
    const double bpm = engine->currentBpm.load (std::memory_order_relaxed);
    lfo1.waveType = (int) ld (p.lfo1Wave);
    // LFO rates: the base knob, moved by any matrix slot routed to
    // LFO 1/2 RATE (a slow LFO sweeping another LFO's speed = classic
    // evolving-texture trick). Rate is re-derived every block.
    lfo1.rate = syncedRate ((int) ld (p.lfo1Unit), (int) ld (p.lfo1Div),
                            ld (p.lfo1Rate) + mv.lfoRate1, bpm);
    lfo2.waveType = (int) ld (p.lfo2Wave);
    lfo2.rate = syncedRate ((int) ld (p.lfo2Unit), (int) ld (p.lfo2Div),
                            ld (p.lfo2Rate) + mv.lfoRate2, bpm);

    const int ft = (int) ld (p.filterType);
    const int ft2 = (int) ld (p.filter2Type);
    const int route = (int) ld (p.filterRoute);
    // Type 4 (NOTCH) runs the filter as a BANDPASS and the render loop subtracts
    // it from the dry signal - that subtraction is what makes it a notch. Mapping
    // it to lowpass instead (which it was) produced "dry - lowpass": a resonant
    // highpass wearing the NOTCH label, with no rejection band at all.
    using F = juce::dsp::StateVariableTPTFilterType;
    const auto svfTypeFor = [] (int type)
    {
        return type == 2 ? F::highpass : (type == 3 || type == 4) ? F::bandpass : F::lowpass;
    };

    if (ft != curFiltType)
    {
        curFiltType = ft;
        const auto t = svfTypeFor (ft);
        svfL1.setType (t); svfR1.setType (t);
        svfL1b.setType (t); svfR1b.setType (t);
        // Filter B is deliberately NOT touched here. It used to be reset to
        // lowpass in this block, so turning A's TYPE knob silently turned B's
        // bandpass into a lowpass while the UI still showed BAND PASS; nothing
        // put it back until the next note-on, so a held note stayed wrong. A's
        // update now only touches A.
    }
    if (ft2 != curFilt2Type)
    {
        curFilt2Type = ft2;
        const auto t = svfTypeFor (ft2);
        svfL2.setType (t); svfR2.setType (t);
        svfL2b.setType (t); svfR2b.setType (t);
    }
    const bool cascade = route == 0 && curFiltType == 1;   // 24 dB LP = A into A
    svfL1.setResonance (reso); svfR1.setResonance (reso);
    svfL2.setResonance (reso2v); svfR2.setResonance (reso2v);
    svfL1b.setResonance (reso); svfR1b.setResonance (reso);
    svfL2b.setResonance (reso2v); svfR2b.setResonance (reso2v);

    // Vowel/formant filter setup: per-band resonance follows the V-RES knob
    // (formant peaks sharpen towards the top), dry/wet is smoothed per block.
    vowActive = ld (p.vowelOn) > 0.5f;
    vowMixS = ld (p.vowelMix);
    if (vowActive && std::abs (ld (p.vowelRes) - lastVowRes) > 0.004f)
    {
        lastVowRes = ld (p.vowelRes);
        const float r = lastVowRes;
        vowL[0].setResonance (4.0f + 8.0f * r);  vowR[0].setResonance (4.0f + 8.0f * r);
        vowL[1].setResonance (8.0f + 12.0f * r); vowR[1].setResonance (8.0f + 12.0f * r);
        vowL[2].setResonance (6.0f + 10.0f * r); vowR[2].setResonance (6.0f + 10.0f * r);
        vowLb[0].setResonance (4.0f + 8.0f * r);  vowRb[0].setResonance (4.0f + 8.0f * r);
        vowLb[1].setResonance (8.0f + 12.0f * r); vowRb[1].setResonance (8.0f + 12.0f * r);
        vowLb[2].setResonance (6.0f + 10.0f * r); vowRb[2].setResonance (6.0f + 10.0f * r);
    }

    for (int u = 0; u < maxUnison; ++u)
    {
        osc1[u].wave = w1;
        osc2[u].wave = w2;
        osc1[u].userBank = user1 ? &engine->bankA : nullptr;
        osc2[u].userBank = user2 ? &engine->bankB : nullptr;
    }
    const int subW = (int) ld (p.subWave);
    subOsc.wave = subW == 1 ? 4 : subW == 2 ? 3 : 1; // 0 square, 1 sine, 2 triangle
    const float subFactor = std::exp2 ((float) (int) ld (p.subOct) - 1.0f);

    driftOffset = juce::jlimit (-1.0f, 1.0f, driftOffset + (noiseRnd.nextFloat() - 0.5f) * 0.06f);

    // Mod matrix: fold this block's mod into the knob values above. Sources
    // are the previous block's latched LFO/envelope values (below), so the
    // matrix adds on top of the fixed LFO/ENV rows rather than replacing them.
    mv = ModMatrix::Values();
    engine->mod.computeAll (mv, latchedL1, latchedL2,
                            latchedFe, latchedAe, velocity,
                            engine->modWheel.load (std::memory_order_relaxed),
                            engine->uiAt.load (std::memory_order_relaxed),
                            engine->shValue.load (std::memory_order_relaxed),
                            engine->macroA.load (std::memory_order_relaxed),
                            engine->macroB.load (std::memory_order_relaxed),
                            modLag, sr,
                            (double) numSamples / juce::jmax (1.0, (double) sr));
    const float driftC = driftOffset * ld (p.drift) * 14.0f;

    // Analog character: slow tape wow (~0.4 Hz) + irregular flutter, and a
    // slower per-voice pitch drift with its own random walk (uncorrelated
    // between voices = subtle chorus-free thickening).
    const float an = juce::jlimit (0.0f, 1.0f, ld (p.analogAmt) + mv.analog);
    const float wowC = an * 18.0f;      // cents
    const float fltC = an * 6.0f;
    const float drift2C = an * driftOffset * 10.0f;   // reuses the walk above

    // Unison character reshapes the detune/pan field across voices:
    // CLASSIC = linear spread, PHASED = voices bunch at the centre (hollow
    // edges, phased-chorus feel), HYPER = exaggerated outer voices (fatter,
    // wider stack).
    const int um = juce::jlimit (0, 2, (int) ld (p.uniMode));
    for (int u = 0; u < uni; ++u)
    {
        float norm = (uni > 1) ? (float) u / (float) (uni - 1) * 2.0f - 1.0f : 0.0f;
        if (um == 1 && uni > 2)
            norm *= std::abs (norm);                       // centre bunching
        else if (um == 2 && uni > 2)
            norm = std::copysign (std::pow (std::abs (norm), 0.55f), norm);
        const float cents1 = fine1 + norm * spreadC + driftC * (u % 2 == 0 ? 1.0f : -1.0f);
        const float cents2 = fine2 + norm * spreadC;
        detune1F[u] = std::exp2 (cents1 / 1200.0f);
        detune2F[u] = std::exp2 (cents2 / 1200.0f);
        const float ang = (norm * width01 * 0.5f + 0.5f) * juce::MathConstants<float>::halfPi;
        panL[u] = std::cos (ang);
        panR[u] = std::sin (ang);
    }

    const float oscPan1 = juce::jlimit (-1.0f, 1.0f, (ld (p.osc1Pan) - 0.5f) * 2.0f + mv.pan1);
    const float oscPan2 = juce::jlimit (-1.0f, 1.0f, (ld (p.osc2Pan) - 0.5f) * 2.0f + mv.pan2);
    const float pan1L = std::cos ((oscPan1 * 0.5f + 0.5f) * juce::MathConstants<float>::halfPi);
    const float pan1R = std::sin ((oscPan1 * 0.5f + 0.5f) * juce::MathConstants<float>::halfPi);
    const float pan2L = std::cos ((oscPan2 * 0.5f + 0.5f) * juce::MathConstants<float>::halfPi);
    const float pan2R = std::sin ((oscPan2 * 0.5f + 0.5f) * juce::MathConstants<float>::halfPi);

    float* outL = buffer.getWritePointer (0);
    float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        const float a = ampEnv.getNextSample();
        if (a < 1.0e-5f && ! ampEnv.isActive())
        {
            clearCurrentNote();
            return;
        }

        const float fe = filtEnv.getNextSample() * envVel;
        const float l1 = lfo1.next (sr);
        const float l2 = lfo2.next (sr);

        // Matrix bookkeeping: latch the sources for the next block's
        // computeAll pass, and publish the global-FX destinations ~64x per
        // second (see ModMatrix::Global for why per block is enough) plus the
        // live source values for the editor's animated mod dots.
        latchedL1 = l1; latchedL2 = l2;
        latchedFe = fe; latchedAe = a;
        if ((i & 63) == 0)
        {
            engine->mod.publishGlobal (l1, l2, fe, a, velocity,
                                       engine->modWheel.load (std::memory_order_relaxed),
                                       engine->uiAt.load (std::memory_order_relaxed),
                                       engine->shValue.load (std::memory_order_relaxed),
                                       engine->macroA.load (std::memory_order_relaxed),
                                       engine->macroB.load (std::memory_order_relaxed));
            engine->uiLfo1.store (l1, std::memory_order_relaxed);
            engine->uiLfo2.store (l2, std::memory_order_relaxed);
            engine->uiEnvF.store (fe, std::memory_order_relaxed);
            engine->uiEnvA.store (a, std::memory_order_relaxed);
            engine->uiVelocity.store (velocity, std::memory_order_relaxed);
        }

        // Per-voice wavetable position: base knob + "WT POS" LFO contribution
        // (LFO targets 4/5 = OSC A / OSC B position; targets 0-3 unchanged).
        const float pos1 = juce::jlimit (0.0f, 1.0f,
            wtPos1 + (t1 == 4 ? l1 * d1 : 0.0f) + (t2 == 4 ? l2 * d2 : 0.0f));
        const float pos2 = juce::jlimit (0.0f, 1.0f,
            wtPos2 + (t1 == 5 ? l1 * d1 : 0.0f) + (t2 == 5 ? l2 * d2 : 0.0f));

        float pitchSemis = 0.0f, cutOct = 0.0f, pwmMod = 0.0f, trem = 1.0f, vowMod = 0.0f;
        if (t1 == 0)      pitchSemis += l1 * d1 * 24.0f;
        else if (t1 == 1) cutOct += l1 * d1 * 3.0f;
        else if (t1 == 2) pwmMod += l1 * d1 * 0.45f;
        else if (t1 == 6) vowMod += l1 * d1 * 0.5f;   // LFO target: VOWEL
        else              trem += l1 * d1 * 0.6f;
        if (t2 == 0)      pitchSemis += l2 * d2 * 24.0f;
        else if (t2 == 1) cutOct += l2 * d2 * 3.0f;
        else if (t2 == 2) pwmMod += l2 * d2 * 0.45f;
        else if (t2 == 6) vowMod += l2 * d2 * 0.5f;   // LFO target: VOWEL
        else              trem += l2 * d2 * 0.6f;
        trem = juce::jlimit (0.0f, 1.6f, trem);

        const float bend = engine->pitchBend.load (std::memory_order_relaxed) * (float) bendRange;

        // Analog character: slow tape wow (0.4 Hz), flutter (5.3 Hz) and the
        // slow random drift, combined as cents -> semitones.
        wowPhase = std::fmod (wowPhase + 0.4f * juce::MathConstants<float>::twoPi / sr,
                              juce::MathConstants<float>::twoPi);
        flutterPhase = std::fmod (flutterPhase + 5.3f * juce::MathConstants<float>::twoPi / sr,
                                   juce::MathConstants<float>::twoPi);
        const float anSemis = (std::sin (wowPhase) * wowC
                               + std::sin (flutterPhase) * fltC + drift2C) * (1.0f / 100.0f);
        const float pitchFactor = std::exp2 ((pitchSemis + bend + anSemis) / 12.0f);
        glideFreq += (targetFreq * pitchFactor - glideFreq) * glideCoeff;
        const float freq = juce::jlimit (4.0f, 16000.0f, glideFreq);

        // Log-domain cutoff smoothing, once per output sample (both parity
        // filter sets share the same smoothed cutoff). mv.oct is the mod
        // matrix's summed contribution on CUTOFF.
        const float targetCut = juce::jlimit (20.0f, 20000.0f,
            baseCut * std::exp2 (envOct * fe + ktOct + modOct + cutOct + mv.oct));
        // One-pole smoothing in the log domain: identical character at any
        // sample rate (the old per-sample lerp at 96k settled 2x faster).
        cutoffLog += (std::log2 (targetCut) - cutoffLog) * (1.0f - std::exp (-1.0f / (0.02f * sr)));
        cutoffSmooth = std::exp2 (cutoffLog);
        const float cut2Target = juce::jlimit (20.0f, 20000.0f,
            ld (p.cutoff2) * std::exp2 (mv.oct2));
        const float log2Cut2 = std::log2 (cutoff2Smooth);
        cutoff2Smooth = std::exp2 (log2Cut2 + (std::log2 (cut2Target) - log2Cut2)
            * (1.0f - std::exp (-1.0f / (0.02f * sr))));

        // ---- oscillator + filter chain: rendered once, or twice at 2x in
        // HQ mode (every output sample is the average of two half-spaced
        // sub-samples, so aliasing folds down an octave lower). The odd
        // sub-sample keeps its own filter / DC / feedback state; modulation
        // (LFOs, envelopes, glide) is evaluated once per output sample.
        float accL = 0.0f, accR = 0.0f;
        for (int sub = 0; sub < (hq ? 2 : 1); ++sub)
        {
            const bool odd = sub == 1;

            // Parity-specific filter / DC / feedback state.
            using SVF = juce::dsp::StateVariableTPTFilter<float>;
            SVF* L1 = odd ? &svfL1b : &svfL1;
            SVF* R1 = odd ? &svfR1b : &svfR1;
            SVF* L2 = odd ? &svfL2b : &svfL2;
            SVF* R2 = odd ? &svfR2b : &svfR2;
            SVF* vowLs = odd ? &vowLb[0] : &vowL[0];
            SVF* vowRs = odd ? &vowRb[0] : &vowR[0];
            float& fbL = odd ? filtFbLb : filtFbL;
            float& fbR = odd ? filtFbRb : filtFbR;
            float& dcxL = odd ? dcX1Lb : dcX1L;
            float& dcyL = odd ? dcY1Lb : dcY1L;
            float& dcxR = odd ? dcX1Rb : dcX1R;
            float& dcyR = odd ? dcY1Rb : dcY1R;

        // OSC A / B with user-wavetable routing (drawn shapes replace the base wave).
        // OSC B is also the hard-sync master and the ring-mod partner, so its
        // per-voice value is kept for the OSC A loop below.
        float o1L = 0.0f, o1R = 0.0f, o2L = 0.0f, o2R = 0.0f, o2Sum = 0.0f;
        float ringL = 0.0f, ringR = 0.0f;
        float o2val[maxUnison] = {};
        bool  syncWrap[maxUnison] = {};
        const float ringAmt = juce::jlimit (0.0f, 1.0f, ld (p.ringMod));
        const bool  syncOn  = ld (p.oscSync) > 0.5f;
        const float pwB = juce::jlimit (0.05f, 0.95f, pwBase + mv.pulseW);

        // OSC B must keep running when it is the sync master or the ring-mod
        // partner even at zero level, or the sync would freeze with the master.
        if (lvl2 > 0.0005f || fmAmt > 0.0005f || syncOn || ringAmt > 0.0005f)
        {
            for (int u = 0; u < uni; ++u)
            {
                const double before = osc2[u].phase;
                const float o2 = user2 ? osc2[u].nextUser (freq * detune2F[u] * octF2 / srr, pos2)
                                       : osc2[u].next (freq * detune2F[u] * octF2 / srr, 0.0f, pwB);
                // A wrap is a phase that went backwards across this one sample
                // (dt is clamped positive, so phase only ever rises otherwise).
                syncWrap[u] = osc2[u].phase < before;
                o2val[u] = o2;
                o2Sum += o2;
                o2L += o2 * panL[u];
                o2R += o2 * panR[u];
            }
            const float inv = 1.0f / (float) uni;
            o2Sum *= inv; o2L *= inv; o2R *= inv;
        }

        const float fm = o2Sum * fmAmt;
        if (lvl1 > 0.0005f)
        {
            const float pwmW = juce::jlimit (0.05f, 0.95f, pwBase + pwmMod + mv.pulseW);
            for (int u = 0; u < uni; ++u)
            {
                // Hard sync: OSC A's phase snaps back to zero on every OSC B
                // cycle. Resetting *before* next() makes the first sample of the
                // new cycle land one step in, which is the classic click-free
                // sync edge rather than a duplicated zero crossing.
                if (syncOn && syncWrap[u])
                    osc1[u].resetPhase (0.0f);
                const float o1 = user1 ? osc1[u].nextUser (freq * detune1F[u] * octF1 / srr, pos1)
                                       : osc1[u].next (freq * detune1F[u] * octF1 / srr, fm, pwmW);
                o1L += o1 * panL[u];
                o1R += o1 * panR[u];
                if (ringAmt > 0.0005f)
                {
                    const float rs = o1 * o2val[u];
                    ringL += rs * panL[u];
                    ringR += rs * panR[u];
                }
            }
            const float inv = 1.0f / (float) uni;
            o1L *= inv; o1R *= inv; ringL *= inv; ringR *= inv;
        }

        // Ring modulation: crossfade the straight OSC A signal into the
        // product of the two oscillators (amount 0 = off, 1 = pure ring).
        if (ringAmt > 0.0005f)
        {
            o1L = o1L * (1.0f - ringAmt) + ringL * ringAmt;
            o1R = o1R * (1.0f - ringAmt) + ringR * ringAmt;
        }

        // Sub oscillator (square / sine / triangle), one octave down by default.
        // The square now goes through polyBLEP so the lowend doesn't alias.
        float subSig = 0.0f;
        if (subLvl > 0.0005f)
        {
            const float dtSub = juce::jlimit (1.0e-6f, 0.5f, (float) ((double) freq * (double) subFactor / srr));
            if (subW == 1)
                subSig = std::sin (juce::MathConstants<float>::twoPi * (float) subOsc.phase);
            else if (subW == 2)
            {
                subSig = 1.0f - 4.0f * std::fabs ((float) subOsc.phase - std::floor ((float) subOsc.phase + 0.5f));
                subOsc.phase = std::fmod (subOsc.phase + (double) dtSub, 1.0);
            }
            else
                subSig = subOsc.nextSquare (dtSub);
        }

        // Noise source: white, or pink through Paul Kellet's refined 7-pole
        // run (≈ ±0.05 dB down to 10 Hz). One filter state per channel so the
        // stereo image is decorrelated, not a dead mono point. Pink's −3 dB/oct
        // tilt matches cymbals/air/hibiscus-hiss far better than white; both
        // are attenuated ~3 dB so equal knob positions feel equally loud.
        float noiseL, noiseR;
        if (noiseKind > 0.5f)
        {
            const float w = noiseRnd.nextFloat() * 2.0f - 1.0f;
            float& a0 = pinkL[0]; float& a1 = pinkL[1]; float& a2 = pinkL[2];
            float& b0 = pinkL[3]; float& b1 = pinkL[4]; float& b2 = pinkL[5]; float& b3 = pinkL[6];
            a0 += 0.178339f * w; a1 += 0.296654f * a0 + 0.006f * w;
            a2 += 0.072184f * a0 + 0.029597f * a1;
            const float outP = 0.330139f * a2 + 0.164842f * a1 + 0.087153f * a0
                             + 0.010625f * b0 + 0.020982f * b1 + 0.002899f * b2 + 0.000741f * b3;
            b3 = b2; b2 = b1; b1 = b0; b0 = w;
            a0 = a1; a1 = a2; a2 = 0.0f;
            noiseL = outP * 1.4142f;

            const float w2 = noiseRnd.nextFloat() * 2.0f - 1.0f;
            float& c0 = pinkR[0]; float& c1 = pinkR[1]; float& c2 = pinkR[2];
            float& d0 = pinkR[3]; float& d1 = pinkR[4]; float& d2 = pinkR[5]; float& d3 = pinkR[6];
            c0 += 0.178339f * w2; c1 += 0.296654f * c0 + 0.006f * w2;
            c2 += 0.072184f * c0 + 0.029597f * c1;
            const float outP2 = 0.330139f * c2 + 0.164842f * c1 + 0.087153f * c0
                              + 0.010625f * d0 + 0.020982f * d1 + 0.002899f * d2 + 0.000741f * d3;
            d3 = d2; d2 = d1; d1 = d0; d0 = w2;
            c0 = c1; c1 = c2; c2 = 0.0f;
            noiseR = outP2 * 1.4142f;
        }
        else
        {
            noiseL = noiseRnd.nextFloat() * 2.0f - 1.0f;
            noiseR = noiseRnd.nextFloat() * 2.0f - 1.0f;
        }
        noiseL *= 0.7071f;
        noiseR *= 0.7071f;

        float sigL = o1L * lvl1 * pan1L + o2L * lvl2 * pan2L + subSig * subLvl + noiseL * noiseLvl;
        float sigR = o1R * lvl1 * pan1R + o2R * lvl2 * pan2R + subSig * subLvl + noiseR * noiseLvl;
        sigL *= trem;
        sigR *= trem;

        if (driveAmt > 0.001f)
        {
            const float pre = 1.0f + 4.0f * driveAmt;
            sigL = std::tanh (sigL * pre);
            sigR = std::tanh (sigR * pre);
        }

        // Filter drive: saturation immediately before the main filter, so the
        // filter shape is applied to the harmonics it creates (steeper, grittier
        // sweeps than the osc-level DRIVE). tanh chosen for musical asymmetry.
        {
            const float fd = juce::jlimit (0.0f, 1.0f, ld (p.fDrive));
            if (fd > 0.001f)
            {
                const float pre = 1.0f + 7.0f * fd;
                sigL = std::tanh (sigL * pre);
                sigR = std::tanh (sigR * pre);
            }
        }

        // Filter feedback: one-sample path from the filter output back into
        // its input; resonates with the filter's own phase shift for
        // self-oscillating screech. Hard-limited and safety-clamped.
        {
            const float fb = juce::jlimit (0.0f, 1.0f, ld (p.fFeedback));
            if (fb > 0.001f)
            {
                const float amt = 0.9f * fb;
                sigL = juce::jlimit (-4.0f, 4.0f, sigL + fbL * amt);
                sigR = juce::jlimit (-4.0f, 4.0f, sigR + fbR * amt);
            }
        }

        // DC blocker after asymmetric stages (drive, PWM) — keeps bass clean
        // and stops the limiter from riding a constant offset.
        {
            constexpr float R = 0.9975f;
            const float yl = sigL - dcxL + R * dcyL;
            dcxL = sigL; dcyL = yl;
            sigL = yl;
            const float yr = sigR - dcxR + R * dcyR;
            dcxR = sigR; dcyR = yr;
            sigR = yr;
        }

        L1->setCutoffFrequency (cutoffSmooth);
        R1->setCutoffFrequency (cutoffSmooth);
        float fl = L1->processSample (0, sigL);
        float fr = R1->processSample (0, sigR);
        {
            const float fb = juce::jlimit (0.0f, 1.0f, ld (p.fFeedback));
            fbL = juce::jlimit (-4.0f, 4.0f, fl * (fb > 0.001f ? 0.9f * fb : 0.0f));
            fbR = juce::jlimit (-4.0f, 4.0f, fr * (fb > 0.001f ? 0.9f * fb : 0.0f));
        }
        if (curFiltType == 4) // notch = dry - bandpass
        {
            fl = sigL - fl;
            fr = sigR - fr;
        }

        // FILTER B with routing:
        //   SERIAL   — B follows A (24 dB LP keeps the old cascade feel)
        //   PARALLEL — A and B summed 50/50
        //   SPLIT    — A to the left, B to the right (stereo filter spread)
        if (route == 0)
        {
            if (cascade)
            {
                L2->setCutoffFrequency (cutoffSmooth);
                R2->setCutoffFrequency (cutoffSmooth);
                float bl = L2->processSample (0, fl);
                float br = R2->processSample (0, fr);

                // NOTCH has to mean the same thing in every route: "dry minus
                // bandpass". In series the dry signal B sees is A's output, so
                // without this the NOTCH choice silently rendered a bandpass.
                if (curFilt2Type == 4) { bl = fl - bl; br = fr - br; }

                fl = bl;
                fr = br;
            }
        }
        else if (route == 1)     // parallel
        {
            L2->setCutoffFrequency (cutoff2Smooth);
            R2->setCutoffFrequency (cutoff2Smooth);
            const float pl = L2->processSample (0, sigL);
            const float pr = R2->processSample (0, sigR);
            if (curFilt2Type == 4) { fl = (fl + (sigL - pl)) * 0.5f; fr = (fr + (sigR - pr)) * 0.5f; }
            else                   { fl = (fl + pl) * 0.5f;         fr = (fr + pr) * 0.5f; }
        }
        else                     // split: A left, B right
        {
            L2->setCutoffFrequency (cutoff2Smooth);
            R2->setCutoffFrequency (cutoff2Smooth);

            // B's left-channel output is thrown away; the call only keeps B's
            // state moving so switching route mid-note stays continuous.
            L2->processSample (0, sigL);
            const float pr = R2->processSample (0, sigR);

            // Left keeps A's output as-is. A's notch (dry - bandpass) is applied
            // once, above, for both channels - re-applying it here subtracted the
            // bandpass a second time and handed back the bandpass itself, so
            // NOTCH + SPLIT sounded like a bandpass on the left channel only.
            fr = curFilt2Type == 4 ? sigR - pr : pr;
        }

        // Vowel/formant bank (post main filters): three band-passes at the
        // interpolated formant frequencies, mixed back over the dry signal.
        // Morph position = the V-MORPH knob plus the VOWEL LFO contribution;
        // 0 = pure AH, 1 = pure OO, in between glides through EH-EE-OH.
        if (vowActive)
        {
            const float pos = juce::jlimit (0.0f, 1.0f,
                ld (p.vowelMorph) + vowMod + mv.vowelMorph);
            const float seg = pos * 4.0f;
            const int vi = juce::jmin (3, (int) seg);
            const float vfrac = seg - (float) vi;
            const int vj = vi + 1;

            float wl = 0.0f, wr = 0.0f, gsum = 0.0f;
            for (int k = 0; k < 3; ++k)
            {
                const float ff = formants[(size_t) vi][k]
                               + (formants[(size_t) vj][k] - formants[(size_t) vi][k]) * vfrac;
                const float fg = formantGains[(size_t) vi][k]
                               + (formantGains[(size_t) vj][k] - formantGains[(size_t) vi][k]) * vfrac;
                vowLs[k].setCutoffFrequency (juce::jlimit (40.0f, 18000.0f, ff));
                vowRs[k].setCutoffFrequency (juce::jlimit (40.0f, 18000.0f, ff));
                wl += vowLs[k].processSample (0, fl) * fg;
                wr += vowRs[k].processSample (0, fr) * fg;
                gsum += fg;
            }
            const float norm = 0.9f / juce::jmax (0.6f, gsum);
            fl = fl + (wl * norm - fl) * vowMixS;
            fr = fr + (wr * norm - fr) * vowMixS;
        }

            accL += fl;
            accR += fr;
        }

        if (hq) { accL *= 0.5f; accR *= 0.5f; }

        const float amp = a * ampVel;
        if (outR != nullptr)
        {
            outL[i] += accL * amp;
            outR[i] += accR * amp;
        }
        else
        {
            outL[i] += (accL + accR) * 0.5f * amp;
        }
    }
}

//==============================================================================
GoaSynth::GoaSynth()
{
    addSound (new GoaSound());
    for (int i = 0; i < 16; ++i)
        addVoice (new GoaVoice (this));

    // -1 = "this root started no chord tone" (0 is a valid note number).
    for (auto& root : chordTones)
        for (auto& tone : root)
            tone = -1;

    p.osc1User = &userA;
    p.osc2User = &userB;
    userA.reset();
    userB.reset();
    commitWaves();
}

void GoaSynth::commitWaves()
{
    juce::uint64 maskA = 0, maskB = 0;
    const bool a = userA.consumeUpdate (maskA);
    const bool b = userB.consumeUpdate (maskB);
    if (a) bankA.rebuild (userA, maskA);
    if (b) bankB.rebuild (userB, maskB);
}

// Chord-memory intervals stacked above the played note, indexed by chordMode-1
// (1 5TH, 2 MINOR, 3 MAJOR, 4 OCT; -1 = unused slot). Snapped into the current
// scale at use, so the harmony follows the loaded SCALE/ROOT.
static constexpr int chordIv[4][3] =
    { { 7, -1, -1 }, { 3, 7, -1 }, { 4, 7, -1 }, { 12, -1, -1 } };

void GoaSynth::noteOn (int midiChannel, int midiNoteNumber, float velocity)
{
    const int mode = (int) ld (p.voicing); // 0 = poly, 1 = mono, 2 = legato
    midiNoteNumber = juce::jlimit (0, 127, midiNoteNumber); // heldRefs/chordTones are indexed by note

    // Scale Lock: live notes snap into the arp scale (root = arp root pitch
    // class), so a keyboardist noodling in Phrygian always sounds in key.
    if (ld (p.scaleLock) > 0.5f)
        midiNoteNumber = tuning::nearestScaleNote (midiNoteNumber,
            (int) ld (p.arpScale), (int) ld (p.arpRoot));

    const float hz = noteFreq (midiNoteNumber);   // SCL microtuning + fine

    if (mode == 0)
    {
        const int maxV = juce::jlimit (1, 16, (int) ld (p.polyMax));
        int active = 0;
        for (auto* v : voices)
            if (v->isVoiceActive()) ++active;
        while (active >= maxV)
        {
            GoaVoice* victim = nullptr;
            for (auto* v : voices)
                if (auto* gv = dynamic_cast<GoaVoice*> (v); gv != nullptr && gv->isVoiceActive())
                {
                    victim = gv;
                    break;
                }
            if (victim == nullptr)
                break;
            victim->stopNote (0.5f, true);
            --active;
        }
        addHeldNote (midiNoteNumber);
        Synthesiser::noteOn (midiChannel, midiNoteNumber, velocity);

        // Chord memory: each key also sounds scale-snapped chord intervals
        // above it (poly voicing only). Snapped so the harmony follows the
        // loaded scale rather than fighting it. The tones actually started are
        // remembered per root so noteOff can release exactly those.
        for (auto& tone : chordTones[midiNoteNumber])
            tone = -1;

        const int cm = juce::jlimit (0, 4, (int) ld (p.chordMode));
        if (cm > 0)
        {
            int slot = 0;
            for (const int semi : chordIv[cm - 1])
            {
                if (semi < 0)
                    continue;
                const int cn = tuning::nearestScaleNote (
                    juce::jlimit (0, 127, midiNoteNumber + semi),
                    (int) ld (p.arpScale), (int) ld (p.arpRoot));
                if (cn == midiNoteNumber)
                    continue;                        // snapped back onto the key
                addHeldNote (cn);
                Synthesiser::noteOn (midiChannel, cn, velocity);
                chordTones[midiNoteNumber][slot++] = cn;
            }
        }
        return;
    }

    addHeldNote (midiNoteNumber);

    for (auto* v : voices)
    {
        if (auto* gv = dynamic_cast<GoaVoice*> (v))
        {
            if (gv->isVoiceActive())
            {
                gv->glideTo (midiNoteNumber, hz);
                if (mode == 1)
                    gv->retrig();
                return;
            }
        }
    }
    Synthesiser::noteOn (midiChannel, midiNoteNumber, velocity);
}

void GoaSynth::noteOff (int midiChannel, int midiNoteNumber, float velocity, bool allowTailOff)
{
    // Mirror noteOn's Scale Lock: noteOn snaps an out-of-scale key onto the
    // scale, so noteOff has to snap identically or it would address a note that
    // was never started and leave the voice stuck on.
    midiNoteNumber = juce::jlimit (0, 127, midiNoteNumber);
    if (ld (p.scaleLock) > 0.5f)
        midiNoteNumber = tuning::nearestScaleNote (midiNoteNumber,
            (int) ld (p.arpScale), (int) ld (p.arpRoot));

    if (pedalDown)
    {
        // The key is physically up now, so drop its claim; the note keeps
        // sounding from sustainedNotes until the pedal is released.
        releaseHeldNote (midiNoteNumber);
        sustainedNotes.addIfNotAlreadyThere (midiNoteNumber);

        for (auto& tone : chordTones[midiNoteNumber])
        {
            const int cn = tone;
            tone = -1;
            if (cn >= 0)
            {
                releaseHeldNote (cn);
                sustainedNotes.addIfNotAlreadyThere (cn);
            }
        }
        return;
    }
    if ((int) ld (p.voicing) != 0) // mono / legato: return to still-held notes
    {
        releaseHeldNote (midiNoteNumber);

        GoaVoice* active = nullptr;
        for (auto* v : voices)
            if (auto* gv = dynamic_cast<GoaVoice*> (v); gv != nullptr && gv->isVoiceActive())
            {
                active = gv;
                break;
            }

        if (active == nullptr || active->getCurrentNote() != midiNoteNumber)
            return; // released a note that wasn't sounding

        if (heldNotes.isEmpty())
        {
            active->stopNote (velocity, true);
            return;
        }

        const int nextNote = heldNotes[0]; // most recent still held
        const float hz = noteFreq (nextNote);
        active->glideTo (nextNote, hz);
        if ((int) ld (p.voicing) == 1)
            active->retrig();
        return;
    }

    // Another held key may still claim this note as one of its chord tones, in
    // which case releasing it here must not silence it.
    if (releaseHeldNote (midiNoteNumber))
        Synthesiser::noteOff (midiChannel, midiNoteNumber, velocity, allowTailOff);

    // Release exactly the chord tones noteOn started for this root (remembered,
    // so changing CHORD or SCALE while the key was down cannot strand a voice).
    for (auto& tone : chordTones[midiNoteNumber])
    {
        const int cn = tone;
        tone = -1;
        if (cn >= 0 && releaseHeldNote (cn))
            Synthesiser::noteOff (midiChannel, cn, velocity, allowTailOff);
    }
}

void GoaSynth::handleController (int midiChannel, int controllerNumber, int controllerValue)
{
    if (controllerNumber == 64)
    {
        const bool down = controllerValue >= 64;
        if (pedalDown && ! down)
            pedalUp();
        pedalDown = down;
        return;
    }
    if (controllerNumber == 1)
    {
        modWheel.store ((float) controllerValue / 127.0f, std::memory_order_relaxed);
        return;
    }
    // Performance macros: MIDI-learnable out of the box on CC 14 / CC 15.
    if (controllerNumber == 14)
    {
        macroA.store ((float) controllerValue / 127.0f, std::memory_order_relaxed);
        return;
    }
    if (controllerNumber == 15)
    {
        macroB.store ((float) controllerValue / 127.0f, std::memory_order_relaxed);
        return;
    }
    Synthesiser::handleController (midiChannel, controllerNumber, controllerValue);
}

void GoaSynth::handlePitchWheel (int, int wheelValue)
{
    pitchBend.store ((float) (wheelValue - 8192) / 8192.0f, std::memory_order_relaxed);
}

// Channel aftertouch (channel pressure) is matrix source 8.
void GoaSynth::handleChannelPressure (int midiChannel, int channelPressureValue)
{
    juce::ignoreUnused (midiChannel);
    uiAt.store (juce::jlimit (0.0f, 1.0f, channelPressureValue / 127.0f),
                std::memory_order_relaxed);
}

void GoaSynth::allNotesOff (int midiChannel, bool allowTailOff)
{
    sustainedNotes.clear();
    heldNotes.clear();
    for (auto& ref : heldRefs)
        ref = 0;
    for (auto& root : chordTones)
        for (auto& tone : root)
            tone = -1;
    pedalDown = false;
    Synthesiser::allNotesOff (midiChannel, allowTailOff);
}

void GoaSynth::pedalUp()
{
    // Only notes whose key was released while the pedal was down are in here,
    // and each has a single voice (the Synthesiser keeps one per note), so one
    // note-off each lifts the pedal cleanly.
    for (auto n : sustainedNotes)
        Synthesiser::noteOff (1, n, 0.0f, true);
    sustainedNotes.clear();
}

// A note can be sounding because it is held directly *and* because it is a
// chord-memory interval of another held key, so claims on it are counted: the
// voice is only stopped once the last one is released. (JUCE's Synthesiser
// keeps at most one voice per note - noteOn replaces the existing one - so a
// single note-off on the transition to zero is what keeps starts and stops
// balanced.)
void GoaSynth::addHeldNote (int note)
{
    if (note < 0 || note > 127)
        return;
    if (heldRefs[note]++ == 0)
        heldNotes.addIfNotAlreadyThere (note);
}

bool GoaSynth::releaseHeldNote (int note)
{
    if (note < 0 || note > 127 || heldRefs[note] == 0)
        return false;

    if (--heldRefs[note] == 0)
    {
        heldNotes.removeFirstMatchingValue (note);
        return true;   // last claim gone: this is the note-off
    }

    return false;
}

//==============================================================================
// Master limiter (see the class comment in SynthEngine.h). Stage A is the
// replaced juce limiter's static curve — gain in dB = +4.26 - 0.75*max(0,
// in_dB + 10) — driven by its 2 ms / 200 ms ballistics so the compression
// breathes with the material instead of amplitude-modulating the waveform.
// Stage B is what juce's ratio-1000 stage was meant to be: an instant-attack
// peak rider with an 80 ms release that catches everything the curve leaves
// above the -0.5 dB ceiling — the region juce handed to its ±1.0 hard
// clipper, squared off at full scale.
void goa::MasterLimiter::prepare (double sampleRate)
{
    // One-pole coefficients: y += c*(x - y) reaches 63% of a step in tau.
    aAttack  = (float) (1.0 - std::exp (-1.0 / (0.002 * sampleRate)));
    aRelease = (float) (1.0 - std::exp (-1.0 / (0.200 * sampleRate)));
    // The peak stage releases multiplicatively (log-linear, the classic
    // limiter shape) and attacks instantly: envB takes the sample itself.
    bRelease = (float) std::exp (-1.0 / (0.080 * sampleRate));
    envA = envB = 0.0f;
}

void goa::MasterLimiter::reset() noexcept
{
    envA = envB = 0.0f;
}

void goa::MasterLimiter::process (juce::AudioBuffer<float>& buffer, int numSamples) noexcept
{
    auto* xl = buffer.getWritePointer (0);
    auto* xr = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    // juce::dsp::Limiter's makeup for its 4:1 first stage: +3.75 dB plus the
    // threshold compensation its outputVolume applies (+0.5 dB here).
    constexpr float makeup = 1.6322f;
    // The -0.5 dB ceiling everything is held under.
    constexpr float ceiling = 0.94406f;

    for (int i = 0; i < numSamples; ++i)
    {
        // Stereo-linked: both channels share one envelope (and so one gain),
        // which keeps the image from lurching when only one side peaks.
        const float level = juce::jmax (std::abs (xl[i]),
                                        xr != nullptr ? std::abs (xr[i]) : 0.0f);

        // Stage A: juce's first compressor — hard knee at -10 dB, 4:1, its
        // attack/release ballistics on the level detector.
        envA += (level > envA ? aAttack : aRelease) * (level - envA);
        float gA = makeup;
        {
            const float envDb = juce::Decibels::gainToDecibels (envA);
            if (envDb > -10.0f)
                gA *= std::pow (10.0f, -0.75f * (envDb + 10.0f) / 20.0f);
        }

        // Stage B: the real brickwall. envB takes this very sample (instant
        // attack) and decays over 80 ms, so the gain reduction lands on the
        // sample that asked for it — a peak can never outrun the ceiling.
        const float postA = level * gA;
        envB = juce::jmax (postA, envB * bRelease);
        const float gB = envB > ceiling ? ceiling / envB : 1.0f;

        const float g = gA * gB;
        xl[i] = softClip (xl[i] * g, 0.95f);
        if (xr != nullptr)
            xr[i] = softClip (xr[i] * g, 0.95f);
    }
}

} // namespace goa
