#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include "SynthEngine.h"
#include "Parameters.h"

// Editor/processor shared colour theme (defined in PluginEditor.h).
namespace goaui
{
enum Theme : int;
extern Theme themeForNextEditor;   // set by setStateInformation, read by editor
}

// Tempo-synced step sequencer clock: derives everything from the absolute
    // transport position (ppqTime), so the trancegate and arp are phase-locked to
    // the bar (start playback mid-pattern and it's already on the right step).
    struct StepClock
    {
        // quartersPerBeat: quarters per musical beat (1.0 in ppq terms for 4/4).
        // stepBeats: step length in beats (0.25 = 1/16, 0.5 = 1/8, ...).
        void advance (double ppq, double quartersPerBeat, double stepBeats,
                      double bpm, int numSamples, double sampleRate,
                      std::atomic<float>* playhead01);

        int    step            = 0;
        double stepStartSample = -1.0;   // sample offset of next boundary, or -1
    };

class GoaSynthAudioProcessor : public juce::AudioProcessor
{
public:
    GoaSynthAudioProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "GoaSynth"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    void uiNoteOn (int note, float velocity);
    void uiNoteOff (int note);
    void panic();

    // Microtuning: parse a .scl file into the engine table and remember the
    // path across sessions (a copy lands in %APPDATA%\GoaSynth\tuning.scl).
    // Returns false when the file is not a valid Scala scale.
    bool loadScalaFile (const juce::File& sclFile);
    void clearScala();

    // Current microtuning status (for the UI's SCL button).
    bool scalaLoaded() const { return synth.scala.isLoaded(); }
    juce::String scalaName() const { return synth.scala.getName(); }
    int scalaDegrees() const { return synth.scala.getDegreeCount(); }

    // Stuck-note rescue: the on-screen keyboard releases its note on mouse-up,
    // but a swallowed mouse-up (overlay, focus change, window closed mid-note)
    // would leave the voice ringing forever. The processor tracks UI-originated
    // note-ons; releaseAllUiNotes() queues note-offs for any that were never
    // matched, delivered by the audio thread on the next processed block.
    int uiHeldCount() const;
    void releaseAllUiNotes();

    // Full state (params + drawn user waves) as XML, for user preset files.
    std::unique_ptr<juce::XmlElement> stateToXml();
    void applyStateXml (const juce::XmlElement& xml);

    // Drawable wavetable editor support (OSC A / OSC B "User" wave mode).
    goa::UserWave* wavetable (int oscIndex) noexcept
        { return synth.userWave (oscIndex); }
    void commitWaves()
    {
        // Publish the edit surface only if it was actually touched, so a
        // commit for one oscillator never clobbers the other's table.
        if (synth.userA.isEditDirty())
            synth.userA.publish();
        if (synth.userB.isEditDirty())
            synth.userB.publish();
        synth.commitWaves();
    }

    juce::AudioProcessorValueTreeState apvts;

    // Sequencer UI feedback (audio thread writes, editor polls).
    std::atomic<float> gatePlayhead { -1.0f };  // 0..1 through the step, -1 idle
    std::atomic<int>   gateStep    { -1 };      // currently sounding gate step

    // Smoothed output level 0..1 (audio thread writes, editor backdrop reads).
    std::atomic<float> uiLevel { 0.0f };

    // ---- licensing -----------------------------------------------------------
    // Unlicensed (and trial-expired) instances output pure silence; during the
    // 24 h trial everything works (see processBlock).
    bool licensed = false;                      // real license, checked at construction
    bool trial = false;                         // inside the 24 h trial window
    std::atomic<bool> licensedFlag { false };   // lock-free read on the audio thread
    juce::String sessionSerial;                 // serial this session was licensed with

    // Synth engine (public: the test harness drives it directly to verify
    // that mod-matrix routing actually reaches the audio path).
    goa::GoaSynth synth;

private:
    static juce::AudioProcessor::BusesProperties busProps();
    float computeDelaySamples();

    // OTT-style multiband compressor: three bands (Butterworth crossovers at
    // 220 Hz and 2 kHz) each driving a fast upward+downward compressor whose
    // fixed threshold is blended against unity by DEPTH (0 = transparent,
    // 1 = the classic aggressive psy squeeze).
    struct Ott
    {
        void prepare (double sampleRate, int maxBlock);
        void reset();
        void process (juce::AudioBuffer<float>& buffer, float depth01,
                      float low01, float mid01, float high01, float outDb);

        bool prepared = false;

    private:
        struct Band
        {
            // Cascaded 2nd-order sections per channel (4th-order slopes).
            juce::dsp::IIR::Filter<float> l1, l2, r1, r2;
            // Fast compressor state (shared stereo envelope per band).
            float envL = 0.0f, envR = 0.0f;
        };

        double sr = 48000.0;
        Band low, mid, high;
        juce::AudioBuffer<float> bLow, bMid, bHigh;
    };

    Ott ott;
    juce::dsp::Chorus<float> chorus;
    juce::dsp::Phaser<float> phaser;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayL, delayR;
    juce::dsp::Reverb reverb;
    juce::dsp::Gain<float> masterGain;
    juce::dsp::Limiter<float> limiter;
    juce::SmoothedValue<float> delayTimeSmoothed;
    float fbL = 0.0f, fbR = 0.0f;                // damped delay-feedback state
    StepClock gateClock, arpClock, pumpClock;
    std::atomic<float> pumpPlayhead { -1.0f };  // 0..1 through the pump period
    juce::Array<int> arpNotesHeld;              // most-recent-first, for the injector
    juce::MidiBuffer uiMidi;
    juce::CriticalSection uiMidiLock;
    juce::Array<int> uiHeldNotes;               // UI note-ons awaiting their note-off (uiMidiLock)    // Panic hands this to the audio thread: delay/reverb buffers must be
    // cleared there, never from the UI thread (data race with processBlock).
    std::atomic<bool> fxResetPending { false };
    juce::Array<int> uiRescueNotes;             // held notes to release on the audio thread
    std::atomic<bool> uiRescuePending { false };// uiRescueNotes is ready for pickup
    double currentSR = 48000.0;
    int maxDelaySamples = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GoaSynthAudioProcessor)
};
