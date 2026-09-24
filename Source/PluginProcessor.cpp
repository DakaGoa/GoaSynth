#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "License.h"
#include "UserPresets.h"

GoaSynthAudioProcessor::GoaSynthAudioProcessor()
    : AudioProcessor (busProps()),
      apvts (*this, nullptr, "STATE", createParameterLayout())
{
    // Licensing: an unlicensed (or trial-expired) instance renders pure
    // silence; within the 24 h trial everything works.
    licensed = goa::License::isLicensed();
    trial = (! licensed) && goa::License::trialActive();
    licensedFlag.store (licensed || trial, std::memory_order_relaxed);
    sessionSerial = licensed ? goa::License::storedSerial() : juce::String();
#define BIND(x) synth.p.x = apvts.getRawParameterValue (param::x)
    BIND (osc1Wave); BIND (osc1Oct); BIND (osc1Fine); BIND (osc1Level);
    BIND (osc2Wave); BIND (osc2Oct); BIND (osc2Fine); BIND (osc2Level);
    BIND (osc1WtPos); BIND (osc2WtPos);
    BIND (fmAmount); BIND (subWave); BIND (subOct); BIND (subLevel); BIND (noiseLevel);
    BIND (uniVoices); BIND (uniDetune); BIND (uniSpread); BIND (drift);
    BIND (filterType); BIND (cutoff); BIND (reso); BIND (envAmt);
    BIND (keytrack); BIND (drive); BIND (modDepth);
    BIND (filter2Type); BIND (cutoff2); BIND (reso2); BIND (filterRoute);
    BIND (filtA); BIND (filtD); BIND (filtS); BIND (filtR);
    BIND (ampA); BIND (ampD); BIND (ampS); BIND (ampR);
    BIND (lfo1Rate); BIND (lfo1Wave); BIND (lfo1Target); BIND (lfo1Depth);
    BIND (lfo1Unit); BIND (lfo1Div);
    BIND (lfo2Rate); BIND (lfo2Wave); BIND (lfo2Target); BIND (lfo2Depth);
    BIND (lfo2Unit); BIND (lfo2Div);
    BIND (chorusRate); BIND (chorusDepth); BIND (chorusMix);
    BIND (phRate); BIND (phDepth); BIND (phMix);
    BIND (delaySync); BIND (delayTime); BIND (delayFb); BIND (delayMix);
    BIND (revSize); BIND (revDamp); BIND (revMix);
    BIND (masterGain); BIND (glide); BIND (voicing); BIND (polyMax); BIND (bendRange);
    BIND (arpScale); BIND (arpRoot); BIND (scaleLock);
    BIND (vowelOn); BIND (vowelMorph); BIND (vowelRes); BIND (vowelMix);
    BIND (tuningFine);
    BIND (pumpSync); BIND (pumpDepth);
    BIND (analogAmt);
    BIND (fDrive); BIND (fFeedback);
    BIND (masterHQ);
#undef BIND

    // Restore the microtuning (.scl) from the last session, if any.
    auto savedTuning = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                           .getChildFile ("GoaSynth").getChildFile ("tuning.scl");
    if (savedTuning.existsAsFile())
        synth.scala.loadFromText (savedTuning.loadFileAsString());
}

juce::AudioProcessor::BusesProperties GoaSynthAudioProcessor::busProps()
{
    return BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true);
}

bool GoaSynthAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet().isDisabled()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void GoaSynthAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSR = sampleRate;
    synth.setCurrentPlaybackSampleRate (sampleRate);

    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) juce::jmax (1, samplesPerBlock), 2 };
    chorus.prepare (spec); chorus.reset();
    phaser.prepare (spec); phaser.reset();
    reverb.prepare (spec); reverb.reset();
    masterGain.prepare (spec); masterGain.reset();
    masterGain.setRampDurationSeconds (0.03);
    limiter.prepare (spec); limiter.reset();
    limiter.setThreshold (-0.5f);
    limiter.setRelease (80.0f);

    ott.prepare (sampleRate, juce::jmax (1, samplesPerBlock));
    ott.reset();

    maxDelaySamples = (int) (2.5 * sampleRate) + 8;
    const juce::dsp::ProcessSpec mono { sampleRate, (juce::uint32) juce::jmax (1, samplesPerBlock), 1 };
    delayL.setMaximumDelayInSamples (maxDelaySamples); delayL.prepare (mono); delayL.reset();
    delayR.setMaximumDelayInSamples (maxDelaySamples); delayR.prepare (mono); delayR.reset();
    delayTimeSmoothed.reset (sampleRate, 0.06);
    delayTimeSmoothed.setCurrentAndTargetValue (computeDelaySamples());
}

//==============================================================================
// Everything is derived from the absolute transport position (ppq is in
// quarter notes), so the pattern is phase-locked to the bar and never drifts.
void StepClock::advance (double ppq, double quartersPerBeat,
                         double stepBeats, double bpm,
                         int numSamples, double sampleRate,
                         std::atomic<float>* playhead01)
{
    const double stepQuarters = juce::jmax (1.0e-9, stepBeats * quartersPerBeat);
    const double posSteps = ppq / stepQuarters;
    const double floorSteps = std::floor (posSteps);

    if (playhead01 != nullptr)
        *playhead01 = (float) (posSteps - floorSteps);
    step = (int) floorSteps;
    stepStartSample = -1.0;

    // Sample offset of the next step boundary, if it lands inside this block.
    const double deltaPpq = (floorSteps + 1.0) * stepQuarters - ppq;
    const double spq = bpm > 0.5 ? 60.0 / bpm : 0.5;   // seconds per quarter note
    const double boundarySample = deltaPpq * spq * sampleRate;
    if (boundarySample >= 0.0 && boundarySample < (double) numSamples)
        stepStartSample = boundarySample;
}

float GoaSynthAudioProcessor::computeDelaySamples()
{
    const float ms = goa::ld (synth.p.delayTime);
    const int sync = (int) goa::ld (synth.p.delaySync);

    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (pos->getBpm().hasValue())
                bpm = *pos->getBpm();

    static constexpr float beatsPer[] = { 0.0f, 0.25f, 0.75f, 0.5f, 1.0f };
    const float beats = (sync >= 1 && sync <= 4) ? beatsPer[sync] : 0.0f;
    const float seconds = beats > 0.0f ? (float) (beats * 60.0 / bpm) : ms * 0.001f;
    return juce::jlimit (8.0f, (float) juce::jmax (16, maxDelaySamples - 2), seconds * (float) currentSR);
}

//==============================================================================
// Microtuning: load a Scala .scl scale and persist a copy so the tuning
// survives sessions. The audio thread only ever reads the published table.
bool GoaSynthAudioProcessor::loadScalaFile (const juce::File& sclFile)
{
    if (! sclFile.existsAsFile())
        return false;
    if (! synth.scala.loadFromText (sclFile.loadFileAsString()))
        return false;

    auto pf = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                  .getChildFile ("GoaSynth").getChildFile ("tuning.scl");
    pf.getParentDirectory().createDirectory();
    sclFile.copyFileTo (pf);
    return true;
}

void GoaSynthAudioProcessor::clearScala()
{
    synth.scala.clear();
    auto pf = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                  .getChildFile ("GoaSynth").getChildFile ("tuning.scl");
    pf.deleteFile();
}

//==============================================================================
// OTT-style multiband compressor. Crossovers: LR4 at 220 Hz and 2 kHz built
// from cascaded Butterworth IIR sections. Each band runs a fast envelope
// follower with a fixed -6 dB floor threshold and 6:1 curve; the band gain is
// interpolated between unity and the computed gain by DEPTH (0 = transparent,
// 1 = full OTT squeeze). Output stage is a soft trim in dB.
void GoaSynthAudioProcessor::Ott::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    const juce::dsp::IIR::Coefficients<float>::Ptr lp220 =
        juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate, 220.0f, 0.7071f);
    const juce::dsp::IIR::Coefficients<float>::Ptr lp2k =
        juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate, 2000.0f, 0.7071f);
    const juce::dsp::IIR::Coefficients<float>::Ptr hp220 =
        juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, 220.0f, 0.7071f);
    const juce::dsp::IIR::Coefficients<float>::Ptr hp2k =
        juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, 2000.0f, 0.7071f);

    // Low band: LP220 x2. Mid: HP220 -> LP2k. High: HP2k x2.
    low.l1.coefficients = lp220;  low.l2.coefficients = lp220;
    low.r1.coefficients = lp220;  low.r2.coefficients = lp220;
    mid.l1.coefficients = hp220;  mid.l2.coefficients = lp2k;
    mid.r1.coefficients = hp220;  mid.r2.coefficients = lp2k;
    high.l1.coefficients = hp2k;  high.l2.coefficients = hp2k;
    high.r1.coefficients = hp2k;  high.r2.coefficients = hp2k;

    bLow.setSize (2, juce::jmax (1, maxBlock), false, false, true);
    bMid.setSize (2, juce::jmax (1, maxBlock), false, false, true);
    bHigh.setSize (2, juce::jmax (1, maxBlock), false, false, true);
    prepared = true;
}

void GoaSynthAudioProcessor::Ott::reset()
{
    low.l1.reset(); low.l2.reset(); low.r1.reset(); low.r2.reset();
    mid.l1.reset(); mid.l2.reset(); mid.r1.reset(); mid.r2.reset();
    high.l1.reset(); high.l2.reset(); high.r1.reset(); high.r2.reset();
    low.envL = low.envR = mid.envL = mid.envR = high.envL = high.envR = 0.0f;
}

//==============================================================================
// OTT-style multiband compressor. Crossovers: LR4 at 220 Hz and 2 kHz built
// from cascaded Butterworth IIR sections. Each band runs a fast envelope
// follower with a fixed -6 dB floor threshold; gain moves toward the threshold
// both for quiet (upward) and loud (downward) content, blended in by DEPTH
// (0 = transparent, 1 = full OTT squeeze). Output stage is a trim in dB.
void GoaSynthAudioProcessor::Ott::process (juce::AudioBuffer<float>& buffer,
                                           float depth01, float low01, float mid01,
                                           float high01, float outDb)
{
    const int n = buffer.getNumSamples();
    if (n <= 0 || buffer.getNumChannels() < 1)
        return;
    if (depth01 < 0.001f)                          // DSP bypass: dead cheap
        return;

    const float* inL = buffer.getReadPointer (0);
    const float* inR = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : nullptr;
    float* loL = bLow.getWritePointer (0);  float* loR = bLow.getWritePointer (1);
    float* mdL = bMid.getWritePointer (0);  float* mdR = bMid.getWritePointer (1);
    float* hiL = bHigh.getWritePointer (0); float* hiR = bHigh.getWritePointer (1);

    // --- split: filter into three bands (in-place on the temp buffers) ------
    for (int i = 0; i < n; ++i)
    {
        const float l = inL[i];
        const float r = inR != nullptr ? inR[i] : l;
        loL[i] = low.l1.processSample (l);
        loR[i] = low.r1.processSample (r);
        mdL[i] = mid.l1.processSample (l);
        mdR[i] = mid.r1.processSample (r);
        hiL[i] = high.l1.processSample (l);
        hiR[i] = high.r1.processSample (r);
    }
    for (int i = 0; i < n; ++i)
    {
        loL[i] = low.l2.processSample (loL[i]);
        loR[i] = low.r2.processSample (loR[i]);
        mdL[i] = mid.l2.processSample (mdL[i]);
        mdR[i] = mid.r2.processSample (mdR[i]);
        hiL[i] = high.l2.processSample (hiL[i]);
        hiR[i] = high.r2.processSample (hiR[i]);
    }

    // --- compress each band: fast attack / slow release, fixed threshold ----
    // One shared stereo envelope per band keeps the image intact.
    const float srF = (float) sr;
    const float atk = std::exp (-1.0f / (0.004f * srF));    // ~4 ms
    const float rel = std::exp (-1.0f / (0.120f * srF));    // ~120 ms
    constexpr float threshold = 0.5f;                        // -6 dB
    constexpr float slope = 1.0f - 1.0f / 6.0f;              // 6:1 ratio

    const float bandDepth[3] = { low01, mid01, high01 };

    for (int i = 0; i < n; ++i)
    {
        const float* bandsL[3] = { loL, mdL, hiL };
        const float* bandsR[3] = { loR, mdR, hiR };
        float outL = 0.0f, outR = 0.0f;
        for (int b = 0; b < 3; ++b)
        {
            Band& band = b == 0 ? low : (b == 1 ? mid : high);
            const float depth = bandDepth[b] * depth01;
            const float peak = juce::jmax (std::abs (bandsL[b][i]),
                                           std::abs (bandsR[b][i]));
            const float target = peak > band.envL ? atk : rel;
            band.envL += (peak - band.envL) * target;

            float g = 1.0f;
            const float x = juce::jmax (band.envL, 1.0e-6f);
            if (band.envL > threshold)
                g = threshold / x;                       // downward: 6:1 above -6 dB
            else                                         // upward: lift toward the floor
                g = juce::jmin (4.0f, 1.0f + slope * (threshold - band.envL) / x);

            const float applied = 1.0f + juce::jlimit (0.0f, 1.0f, depth) * (g - 1.0f);
            outL += bandsL[b][i] * applied;
            outR += bandsR[b][i] * applied;
        }
        buffer.getWritePointer (0)[i] = outL;
        if (inR != nullptr)
            buffer.getWritePointer (1)[i] = outR;
    }

    // Output trim.
    if (std::abs (outDb) > 0.01f)
        buffer.applyGain (std::pow (10.0f, outDb / 20.0f));
}

//==============================================================================
void GoaSynthAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    // Unlicensed (or trial-expired): every sample of every block is zero.
    // MIDI is swallowed so no voice state builds up either.
    if (! licensedFlag.load (std::memory_order_relaxed))
    {
        buffer.clear();
        midi.clear();
        return;
    }

    // Pick up wavetable edits from the UI (lock-free snapshot handoff).
    synth.commitWaves();

    double bpm = 120.0;
    double ppq = 0.0;
    bool playing = false;
    {
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
            {
                if (pos->getBpm().hasValue())
                    bpm = *pos->getBpm();
                if (pos->getPpqPosition().hasValue())
                    ppq = *pos->getPpqPosition();
                playing = pos->getIsPlaying();
            }
        synth.currentBpm.store (bpm, std::memory_order_relaxed);
    }

    {
        const juce::ScopedLock sl (uiMidiLock);
        if (! uiMidi.isEmpty())
        {
            midi.addEvents (uiMidi, 0, uiMidi.getLastEventTime() + 1, 0);
            uiMidi.clear();
        }
    }

    auto& p = synth.p;

    // ---- modulation matrix: rebuild the engine-side slot snapshot ----------
    // Choice/int params -> ModMatrix slots; done once per block before voices
    // render. Slot 0 amount means "slot muted" and skips work in the voice.
    {
        for (int i = 0; i < param::modSlots; ++i)
        {
            auto& s = synth.mod.slots[(size_t) i];
            s.src = (int) goa::ld (apvts.getRawParameterValue (param::modSrc (i)));
            s.dst = (int) goa::ld (apvts.getRawParameterValue (param::modDst (i)));
            s.amt = goa::ld (apvts.getRawParameterValue (param::modAmt (i)));
        }
    }

    // ---- 16-step arp sequencer: injects transposed notes from the held key ----
    // Track physically held notes from the incoming MIDI stream (UI keyboard
    // events arrive here too, so one list covers both sources).
    // Stuck-note rescue: note-offs queued from the UI thread are delivered
    // here, where the synth lives. Plain note-offs (not panic) so voices
    // release through their natural envelope and the arp's own bookkeeping in
    // the same stream stays consistent.
    if (uiRescuePending.load (std::memory_order_acquire))
    {
        juce::Array<int> rescue;
        {
            const juce::ScopedLock sl (uiMidiLock);
            rescue.swapWith (uiRescueNotes);
        }
        uiRescuePending.store (false, std::memory_order_release);
        for (const int n : rescue)
            midi.addEvent (juce::MidiMessage::noteOff (1, n), 0);
        midi.addEvent (juce::MidiMessage::controllerEvent (1, 123, 0), 0); // CC123: clear pedal/sustain bookkeeping
    }

    // Panic defers the FX buffer clear to here (audio thread): a UI-thread
    // reset() would race with this function's delay/reverb processing.
    if (fxResetPending.exchange (false, std::memory_order_acq_rel))
    {
        delayL.reset();
        delayR.reset();
        reverb.reset();
    }

    for (const auto metadata : midi)
    {
        const auto m = metadata.getMessage();
        if (m.isNoteOn())
        {
            arpNotesHeld.removeFirstMatchingValue (m.getNoteNumber());
            arpNotesHeld.insert (0, m.getNoteNumber());
        }
        else if (m.isNoteOff())
        {
            arpNotesHeld.removeFirstMatchingValue (m.getNoteNumber());
        }
    }

    if (playing && ! arpNotesHeld.isEmpty())
    {
        const int sync = (int) goa::ld (apvts.getRawParameterValue (param::arpSync));
        const int octs = (int) goa::ld (apvts.getRawParameterValue (param::arpOct));
        if (octs > 0)
        {
            static constexpr double beatsPer[] =
                { 0.125, 0.25, 0.25 * 1.5, 0.5, 0.5 * (2.0 / 3.0), 1.0 };
            const double stepBeats = beatsPer[juce::jlimit (0, 5, sync)];

            arpClock.advance (ppq, 1.0, stepBeats, bpm, numSamples, currentSR, nullptr);
            const int s16 = ((arpClock.step % 16) + 16) % 16;

            // Direction: remap the physical step to the played step index.
            // UP = as programmed; DOWN = reversed; UP-DOWN = ping-pong without
            // repeating the ends; RANDOM = per-boundary dice; CONVERGE = outer
            // pair inward (0,15,1,14,...).
            const int dir = (int) goa::ld (apvts.getRawParameterValue (param::arpDir));
            int played = s16;
            switch (dir)
            {
                case 1: played = 15 - s16; break;
                case 2:
                {
                    const int span = 31;                     // 16 up + 15 down
                    const int t = s16 % span;
                    played = t < 16 ? t : span - t;
                    break;
                }
                case 3: played = juce::Random::getSystemRandom().nextInt (16); break;
                case 4: played = (s16 % 2) == 0 ? (s16 / 2) : (15 - s16 / 2); break;
                default: break;
            }

            const int semi = (int) goa::ld (apvts.getRawParameterValue (
                param::arpStep (played))) - 1;      // choice 0 = Rest
            if (semi >= 0 && arpClock.stepStartSample >= 0.0)
            {
                const int held = arpNotesHeld[0];
                int note = juce::jlimit (0, 127,
                    held + semi + 12 * (octs - 1));

                // Scale quantizer: snap the arp note into the chosen scale
                // relative to the chosen root (CHROMATIC leaves it alone).
                note = tuning::nearestScaleNote (note,
                    (int) goa::ld (apvts.getRawParameterValue (param::arpScale)),
                    (int) goa::ld (apvts.getRawParameterValue (param::arpRoot)));
                const float vel = juce::jlimit (0.05f, 1.0f, goa::ld (
                    apvts.getRawParameterValue (param::arpVel (s16))));
                const int at = (int) arpClock.stepStartSample;
                // Note lasts 3/4 of the step (gated feel, like Serum's arp)
                // instead of 1 sample — a 1-sample note is just a click.
                const int len = juce::jmax (2, juce::jmin (numSamples,
                    (int) std::lround (stepBeats * (60.0 / bpm) * currentSR * 0.75)));
                midi.addEvent (juce::MidiMessage::noteOn (1, note, vel), at);
                midi.addEvent (juce::MidiMessage::noteOff (1, note),
                               juce::jmin (numSamples - 1, at + len));
            }
        }
    }

    buffer.clear();
    synth.renderNextBlock (buffer, midi, 0, numSamples);

    // ---- trancegate: 16-step volume pattern, sample-accurate, phase-locked ---
    // Active whenever at least one step is switched off (all-on = bypass).
    {
        const int sync = (int) goa::ld (apvts.getRawParameterValue (param::gateSync));
        static constexpr double beatsPer[] =
            { 0.125, 0.25, 0.25 * 1.5, 0.5, 0.5 * (2.0 / 3.0), 1.0 };
        const double stepBeats = beatsPer[juce::jlimit (0, 5, sync)];
        const float depth = goa::ld (apvts.getRawParameterValue (param::gateDepth));
        const int on[16] =
            {
                (int) goa::ld (apvts.getRawParameterValue (param::gateStep (0))),
                (int) goa::ld (apvts.getRawParameterValue (param::gateStep (1))),
                (int) goa::ld (apvts.getRawParameterValue (param::gateStep (2))),
                (int) goa::ld (apvts.getRawParameterValue (param::gateStep (3))),
                (int) goa::ld (apvts.getRawParameterValue (param::gateStep (4))),
                (int) goa::ld (apvts.getRawParameterValue (param::gateStep (5))),
                (int) goa::ld (apvts.getRawParameterValue (param::gateStep (6))),
                (int) goa::ld (apvts.getRawParameterValue (param::gateStep (7))),
                (int) goa::ld (apvts.getRawParameterValue (param::gateStep (8))),
                (int) goa::ld (apvts.getRawParameterValue (param::gateStep (9))),
                (int) goa::ld (apvts.getRawParameterValue (param::gateStep (10))),
                (int) goa::ld (apvts.getRawParameterValue (param::gateStep (11))),
                (int) goa::ld (apvts.getRawParameterValue (param::gateStep (12))),
                (int) goa::ld (apvts.getRawParameterValue (param::gateStep (13))),
                (int) goa::ld (apvts.getRawParameterValue (param::gateStep (14))),
                (int) goa::ld (apvts.getRawParameterValue (param::gateStep (15)))
            };
        bool any = false, all = true;
        for (int i = 0; i < 16; ++i)
        {
            any |= on[i] != 0;
            all &= on[i] != 0;
        }
        if (playing && any && ! all)
        {
            gateClock.advance (ppq, 1.0, stepBeats, bpm, numSamples, currentSR,
                               &gatePlayhead);
            int s16 = ((gateClock.step % 16) + 16) % 16;
            double boundary = gateClock.stepStartSample;
            for (int i = 0; i < numSamples; ++i)
            {
                if (boundary >= 0.0 && (double) i >= boundary)
                {
                    boundary = -1.0;
                    s16 = (s16 + 1) % 16;
                }
                const float g = on[s16] != 0 ? 1.0f : 1.0f - depth;
                gateStep.store (s16, std::memory_order_relaxed);
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    buffer.getWritePointer (ch)[i] *= g;
            }
        }
        else
        {
            gatePlayhead.store (-1.0f, std::memory_order_relaxed);
            gateStep.store (-1, std::memory_order_relaxed);
        }
    }

    // ---- sidechain pump: host-synced volume dip (kick-follow feel) --------
    // Deepest duck right on each beat boundary, exponential recovery until the
    // next one; OFF unless sync > 0 and depth > 0.
    {
        const int pSync = (int) goa::ld (apvts.getRawParameterValue (param::pumpSync));
        const float pDepth = goa::ld (apvts.getRawParameterValue (param::pumpDepth));
        if (playing && pSync > 0 && pDepth > 0.001f)
        {
            static constexpr double beatsPer[] =
                { 1.0, 0.5, 0.5 * (2.0 / 3.0), 0.25, 0.25 * (2.0 / 3.0) };  // 1/4..1/16T
            const double stepBeats = beatsPer[juce::jlimit (0, 4, pSync - 1)];
            pumpClock.advance (ppq, 1.0, stepBeats, bpm, numSamples, currentSR,
                               &pumpPlayhead);
            // frac through the current step at block start (advance publishes it).
            double phase = juce::jlimit (0.0, 0.999,
                (double) pumpPlayhead.load (std::memory_order_relaxed));
            const double spq = bpm > 0.5 ? 60.0 / bpm : 0.5;
            const double inc = 1.0 / juce::jmax (2.0, stepBeats * spq * currentSR);
            for (int i = 0; i < numSamples; ++i)
            {
                phase += inc;
                if (phase >= 1.0)
                    phase -= 1.0;
                const float g = goa::pumpGain (phase, pDepth);
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    buffer.getWritePointer (ch)[i] *= g;
            }
        }
        else
        {
            pumpPlayhead.store (-1.0f, std::memory_order_relaxed);
        }
    }

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> ctx (block);

    chorus.setRate (goa::ld (p.chorusRate));
    chorus.setDepth (goa::ld (p.chorusDepth));
    chorus.setCentreDelay (18.0f);
    chorus.setFeedback (0.15f);
    chorus.setMix (goa::ld (p.chorusMix));
    chorus.process (ctx);

    // Mod matrix global-FX destinations: voices published their summed
    // amounts into mod.globalFx (largest magnitude wins across voices), so
    // one LFO can sweep the shared delay/reverb/phaser from the matrix.
    const float modPhMix = juce::jlimit (-0.5f, 0.5f,
        synth.mod.globalFx.phMix.load (std::memory_order_relaxed));
    phaser.setRate (goa::ld (p.phRate));
    phaser.setDepth (goa::ld (p.phDepth));
    phaser.setMix (juce::jlimit (0.0f, 1.0f, goa::ld (p.phMix) + modPhMix));
    phaser.process (ctx);

    // OTT multiband squeeze sits after the phaser, before the delay/reverb.
    ott.process (buffer,
                 goa::ld (apvts.getRawParameterValue (param::ottDepth)),
                 goa::ld (apvts.getRawParameterValue (param::ottLow)),
                 goa::ld (apvts.getRawParameterValue (param::ottMid)),
                 goa::ld (apvts.getRawParameterValue (param::ottHigh)),
                 goa::ld (apvts.getRawParameterValue (param::ottOut)));

    {
        // Matrix destinations scale delay length (a * 2^oct), feedback and
        // mix around the knob values; everything clamped to legal ranges.
        const float modDt = synth.mod.globalFx.delayTime.load (std::memory_order_relaxed);
        const float modDm = synth.mod.globalFx.delayMix.load (std::memory_order_relaxed);
        const float modDf = synth.mod.globalFx.delayFb.load (std::memory_order_relaxed);
        const float mix = juce::jlimit (0.0f, 1.0f, goa::ld (p.delayMix) + modDm);
        const float fb = juce::jlimit (0.0f, 0.95f, goa::ld (p.delayFb) + modDf);
        // Feedback lowpass (one-pole at ~5 kHz): each repeat loses the
        // top octave, like analog BBD delays — stops harsh fizz buildup
        // at high feedback.
        const float g = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * 5000.0 / currentSR);
        delayTimeSmoothed.setTargetValue (computeDelaySamples() * std::exp2 (modDt * 2.0f));
        float* left = buffer.getWritePointer (0);
        float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : left;
        for (int i = 0; i < numSamples; ++i)
        {
            const float t = delayTimeSmoothed.getNextValue();
            delayL.setDelay (t);
            delayR.setDelay (t);
            const float dl = delayL.popSample (0);
            const float dr = delayR.popSample (0);
            fbL += (dr - fbL) * g;   // damp the feedback path, not the wet tap
            fbR += (dl - fbR) * g;
            delayL.pushSample (0, left[i] + fbL * fb);
            delayR.pushSample (0, right[i] + fbR * fb);
            left[i] += dr * mix;
            right[i] += dl * mix;
        }
    }

    juce::dsp::Reverb::Parameters rp;
    const float modRs = synth.mod.globalFx.revSize.load (std::memory_order_relaxed);
    const float modRm = synth.mod.globalFx.revMix.load (std::memory_order_relaxed);
    rp.roomSize = juce::jlimit (0.0f, 1.0f, goa::ld (p.revSize) + modRs);
    rp.damping = goa::ld (p.revDamp);
    rp.wetLevel = juce::jlimit (0.0f, 1.0f, goa::ld (p.revMix) + modRm);
    rp.dryLevel = juce::jlimit (0.0f, 1.0f, 1.0f - (goa::ld (p.revMix) + modRm));
    rp.width = 0.9f;
    rp.freezeMode = 0.0f;
    reverb.setParameters (rp);
    reverb.process (ctx);

    masterGain.setGainDecibels (goa::ld (p.masterGain));
    masterGain.process (ctx);
    limiter.process (ctx);

    // Smoothed peak follower for the UI backdrop: fast attack, slow release,
    // normalised against full scale so the animation breathes with the mix.
    {
        float peak = 0.0f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const float* x = buffer.getReadPointer (ch);
            for (int i = 0; i < numSamples; ++i)
                peak = juce::jmax (peak, std::abs (x[i]));
        }
        const float cur = uiLevel.load (std::memory_order_relaxed);
        const float target = juce::jlimit (0.0f, 1.0f, peak);
        const float a = target > cur ? 0.35f : 0.045f;   // fast attack, slow release
        uiLevel.store (cur + a * (target - cur), std::memory_order_relaxed);
    }
}

juce::AudioProcessorEditor* GoaSynthAudioProcessor::createEditor()
{
    return new GoaSynthAudioProcessorEditor (*this);
}

void GoaSynthAudioProcessor::uiNoteOn (int note, float velocity)
{
    const juce::ScopedLock sl (uiMidiLock);
    uiMidi.addEvent (juce::MidiMessage::noteOn (1, note, velocity), 0);
    uiHeldNotes.removeFirstMatchingValue (note);   // re-press without release
    uiHeldNotes.add (note);
}

void GoaSynthAudioProcessor::uiNoteOff (int note)
{
    const juce::ScopedLock sl (uiMidiLock);
    uiMidi.addEvent (juce::MidiMessage::noteOff (1, note), 0);
    uiHeldNotes.removeFirstMatchingValue (note);
}

int GoaSynthAudioProcessor::uiHeldCount() const
{
    const juce::ScopedLock sl (uiMidiLock);
    return uiHeldNotes.size();
}

void GoaSynthAudioProcessor::releaseAllUiNotes()
{
    juce::Array<int> toRelease;
    {
        const juce::ScopedLock sl (uiMidiLock);
        toRelease.swapWith (uiHeldNotes);
    }

    // Deliver the note-offs from the audio thread (next processed block): the
    // synth lives there, and note-offs injected into the stream release the
    // voices through their natural amp envelope — unlike panic(), a hard cut.
    if (! toRelease.isEmpty())
    {
        const juce::ScopedLock sl (uiMidiLock);
        uiRescueNotes.addArray (toRelease);
        uiRescuePending.store (true, std::memory_order_release);
    }
}

void GoaSynthAudioProcessor::panic()
{
    {
        const juce::ScopedLock sl (uiMidiLock);
        uiMidi.clear();
        uiHeldNotes.clearQuick();       // nothing left awaiting a note-off
        uiRescueNotes.clearQuick();
        uiRescuePending.store (false, std::memory_order_release);
    }
    synth.allNotesOff (1, false);       // hard cut — no release tail
    // FX tails (delay feedback loop, reverb) keep ringing for seconds after
    // every voice is gone, which reads as "it never stops". Reset them on the
    // audio thread — resetting here would race with processBlock.
    fxResetPending.store (true, std::memory_order_release);
}

std::unique_ptr<juce::XmlElement> GoaSynthAudioProcessor::stateToXml()
{
    auto xml = apvts.copyState().createXml();
    if (xml == nullptr)
        return nullptr;

    // Persist the drawn user waves (all frames) alongside the parameter state.
    for (int idx = 0; idx < 2; ++idx)
    {
        if (auto* w = wavetable (idx))
        {
            for (int f = 0; f < goa::UserWave::numFrames; ++f)
            {
                float shape[goa::UserWave::size];
                w->getFrame (f, shape, goa::UserWave::size);

                bool allZero = true;
                for (int i = 0; i < goa::UserWave::size; ++i)
                    if (std::abs (shape[i]) > 1.0e-6f) { allZero = false; break; }
                if (allZero)
                    continue;                          // don't bloat state with empty frames

                auto waveXml = std::make_unique<juce::XmlElement> ("userWave");
                waveXml->setAttribute ("osc", idx);
                waveXml->setAttribute ("frame", f);
                juce::String values;
                for (int i = 0; i < goa::UserWave::size; ++i)
                    values << shape[i] << ',';
                waveXml->setAttribute ("values", values);
                xml->addChildElement (waveXml.release());
            }
        }
    }
    return xml;
}

void GoaSynthAudioProcessor::applyStateXml (const juce::XmlElement& xml)
{
    if (! xml.hasTagName (apvts.state.getType()))
        return;

    // Restore user waves before the parameter state (wave-mode "User"
    // relies on the shape being present once the state lands).
    // beginEdit() first so frames absent from the XML keep their content.
    synth.userA.beginEdit();
    synth.userB.beginEdit();

    for (auto* waveXml : xml.getChildWithTagNameIterator ("userWave"))
    {
        const int osc = waveXml->getIntAttribute ("osc", 0);
        const int frame = waveXml->getIntAttribute ("frame", 0);
        const juce::StringArray toks = juce::StringArray::fromTokens (
            waveXml->getStringAttribute ("values"), ",", "");
        if (auto* w = wavetable (juce::jlimit (0, 1, osc)))
        {
            float shape[goa::UserWave::size] {};
            const int n = juce::jmin (goa::UserWave::size, toks.size());
            for (int i = 0; i < n; ++i)
                shape[i] = toks[i].getFloatValue();
            w->setFrame (frame, shape, goa::UserWave::size);
        }
    }
    commitWaves();
    apvts.replaceState (juce::ValueTree::fromXml (xml));
}

void GoaSynthAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = stateToXml())
    {
        // Skin choice + the serial this session is licensed with ride along
        // with the session state (not in presets). A session file that was
        // licensed on another machine will refuse to re-activate here.
        xml->setAttribute ("uiTheme", (int) goaui::activeTheme);
        xml->setAttribute ("uiSerial", sessionSerial);
        copyXmlToBinary (*xml, destData);
    }
}

void GoaSynthAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        goaui::themeForNextEditor = goaui::themeFromIndex (
            xml->getIntAttribute ("uiTheme", (int) goaui::themeUv));

        // The serial tag is provenance only: a session saved on a licensed
        // machine can never activate another machine, because activation only
        // ever happens through License::activate() (signature + machine id +
        // one-machine ledger). Unlicensed hosts stay silent regardless.
        (void) xml->getStringAttribute ("uiSerial");

        applyStateXml (*xml);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GoaSynthAudioProcessor();
}
