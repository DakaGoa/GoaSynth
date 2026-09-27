#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <vector>

namespace param
{
inline constexpr const char* osc1Wave   = "osc1Wave";
inline constexpr const char* osc1Oct    = "osc1Oct";
inline constexpr const char* osc1Fine   = "osc1Fine";
inline constexpr const char* osc1Level  = "osc1Level";
inline constexpr const char* osc1Pan    = "osc1Pan";
inline constexpr const char* osc1Phase  = "osc1Phase";
inline constexpr const char* osc1PRand  = "osc1PRand";
inline constexpr const char* osc1WtPos  = "osc1WtPos";
inline constexpr const char* osc2Wave   = "osc2Wave";
inline constexpr const char* osc2Oct    = "osc2Oct";
inline constexpr const char* osc2Fine   = "osc2Fine";
inline constexpr const char* osc2Level  = "osc2Level";
inline constexpr const char* osc2Pan    = "osc2Pan";
inline constexpr const char* osc2Phase  = "osc2Phase";
inline constexpr const char* osc2PRand  = "osc2PRand";
inline constexpr const char* osc2WtPos  = "osc2WtPos";
inline constexpr const char* fmAmount   = "fmAmount";
inline constexpr const char* subWave    = "subWave";
inline constexpr const char* subOct     = "subOct";
inline constexpr const char* subLevel   = "subLevel";
inline constexpr const char* noiseLevel = "noiseLevel";
inline constexpr const char* uniVoices  = "uniVoices";
inline constexpr const char* uniDetune  = "uniDetune";
inline constexpr const char* uniSpread  = "uniSpread";
inline constexpr const char* drift      = "drift";

// Supersaw character: how the unison detune/pan field is shaped.
// CLASSIC = linear spread, PHASED = voices bunch at the centre with hollow
// edges, HYPER = exaggerated outer voices (fatter, wider stack).
inline constexpr const char* uniMode = "uniMode";

// Chord memory: each note-on also sounds scale-snapped chord intervals
// (poly voicing only). 0 = off.
inline constexpr const char* chordMode = "chordMode";

// Performance macros: plain 0..1 knobs meant as modulation-matrix SOURCES
// (matrix source ids 9/10) and MIDI-learnable to CC 14 / CC 15 out of the box.
inline constexpr const char* macroA = "macroA";
inline constexpr const char* macroB = "macroB";

inline constexpr const char* filterType = "filterType";
inline constexpr const char* cutoff     = "cutoff";
inline constexpr const char* reso       = "reso";
inline constexpr const char* envAmt     = "envAmt";
inline constexpr const char* keytrack   = "keytrack";
inline constexpr const char* drive      = "drive";
inline constexpr const char* modDepth   = "modDepth";

// FILTER B: independent type/cutoff/reso plus a routing mode
// (SERIAL = A into B, PARALLEL = A+B summed, SPLIT = A low / B high).
inline constexpr const char* filter2Type = "filter2Type";
inline constexpr const char* cutoff2     = "cutoff2";
inline constexpr const char* reso2       = "reso2";
inline constexpr const char* filterRoute = "filterRoute";

inline constexpr const char* filtA = "filtA";
inline constexpr const char* filtD = "filtD";
inline constexpr const char* filtS = "filtS";
inline constexpr const char* filtR = "filtR";

inline constexpr const char* ampA = "ampA";
inline constexpr const char* ampD = "ampD";
inline constexpr const char* ampS = "ampS";
inline constexpr const char* ampR = "ampR";

inline constexpr const char* lfo1Rate   = "lfo1Rate";
inline constexpr const char* lfo1Wave   = "lfo1Wave";
inline constexpr const char* lfo1Target = "lfo1Target";
inline constexpr const char* lfo1Depth  = "lfo1Depth";
inline constexpr const char* lfo1Unit   = "lfo1Unit";
inline constexpr const char* lfo1Div    = "lfo1Div";
inline constexpr const char* lfo2Rate   = "lfo2Rate";
inline constexpr const char* lfo2Wave   = "lfo2Wave";
inline constexpr const char* lfo2Target = "lfo2Target";
inline constexpr const char* lfo2Depth  = "lfo2Depth";
inline constexpr const char* lfo2Unit   = "lfo2Unit";
inline constexpr const char* lfo2Div    = "lfo2Div";

inline constexpr const char* chorusRate  = "chorusRate";
inline constexpr const char* chorusDepth = "chorusDepth";
inline constexpr const char* chorusMix   = "chorusMix";

inline constexpr const char* phRate  = "phRate";
inline constexpr const char* phDepth = "phDepth";
inline constexpr const char* phMix   = "phMix";

inline constexpr const char* delaySync = "delaySync";
inline constexpr const char* delayTime = "delayTime";
inline constexpr const char* delayFb   = "delayFb";
inline constexpr const char* delayMix  = "delayMix";

inline constexpr const char* revSize = "revSize";
inline constexpr const char* revDamp = "revDamp";
inline constexpr const char* revMix  = "revMix";

// Shimmer: an octave-up pitch-shifted copy of the reverb tail, blended back
// in (0 = off). Classic trance "sky" reverb.
inline constexpr const char* revShimmer = "revShimmer";

// FX duck: ducks the delay + reverb wet level with the dry signal's peak
// envelope, so leads stay clear while the tail swells in the gaps.
inline constexpr const char* duckAmt = "duckAmt";

// OTT-style multiband compressor: per-band depth, crossBand crossover and
// output level. Depth 0 = transparent, 1 = the classic aggressive upward+downward
// psy squeeze. Bands are hard-gated to 0 dB when depth is 0 (DSP bypass).
inline constexpr const char* ottDepth = "ottDepth";
inline constexpr const char* ottLow   = "ottLow";
inline constexpr const char* ottMid   = "ottMid";
inline constexpr const char* ottHigh  = "ottHigh";
inline constexpr const char* ottOut   = "ottOut";

// Filter B: independent type/cutoff/reso plus a routing mode

inline constexpr const char* masterGain = "masterGain";
inline constexpr const char* glide      = "glide";
inline constexpr const char* voicing    = "voicing";
inline constexpr const char* polyMax    = "polyMax";
inline constexpr const char* bendRange  = "bendRange";

inline constexpr const char* gateSync  = "gateSync";   // step length (tempo sync)
inline constexpr const char* gateDepth = "gateDepth";  // silence depth on "off" steps
inline constexpr const char* arpSync   = "arpSync";    // arp step length
inline constexpr const char* arpOct    = "arpOct";     // base octave shift
// gate1..gate16 (bool), arp1..arp16 (choice 0=Rest, 1..12=semitones) and
// arpVel1..arpVel16 (float 0..1 velocity per step) are generated in
// createParameterLayout(); gateStep() / arpStep() / arpVel() build their ids.

// The step params are laid out contiguously as gate1..gate16 / arp1..arp16 /
// arpVel1..arpVel16, so the audio side can index them without string lookups.
inline juce::String gateStep (int i) { return juce::String ("gate") + juce::String (i + 1); }
inline juce::String arpStep  (int i) { return juce::String ("arp")  + juce::String (i + 1); }
inline juce::String arpVel   (int i) { return juce::String ("arpVel") + juce::String (i + 1); }

// Arp playback direction: UP, DOWN, UP-DOWN, RANDOM, CONVERGE.
inline constexpr const char* arpDir = "arpDir";

// Scale quantizer: arp notes (and live playing when scaleLock is on) snap to
// the chosen scale relative to the chosen root. arpScale indexes
// tuning::scales() (0 = CHROMATIC = off), arpRoot is a pitch class 0..11.
inline constexpr const char* arpScale  = "arpScale";
inline constexpr const char* arpRoot   = "arpRoot";
inline constexpr const char* scaleLock = "scaleLock";

// Vowel/formant filter: a morphing 3-formant bank (AH EH EE OH OO) inserted
// after the main filters. vowelOn enables it, vowelMorph walks the vowel
// sequence (LFO target 6 = VOWEL also moves it), vowelRes sharpens the
// formants, vowelMix blends dry/filtered.
inline constexpr const char* vowelOn    = "vowelOn";
inline constexpr const char* vowelMorph = "vowelMorph";
inline constexpr const char* vowelRes   = "vowelRes";
inline constexpr const char* vowelMix   = "vowelMix";

// Global microtuning: fine offset in cents (432 Hz = about -32), plus the
// .scl file loaded from the UI (persisted by the processor, not a parameter).
inline constexpr const char* tuningFine = "tuningFine";

// Host-synced sidechain pump: dips the output at a chosen beat division so
// the synth sits under a kick without DAW sidechain routing.
inline constexpr const char* pumpSync  = "pumpSync";   // OFF / 1/4 / 1/8 / 1/8T / 1/16 / 1/16T
inline constexpr const char* pumpDepth = "pumpDepth";  // dip depth 0..1

// Analog character: slow tape-style wow + flutter on voice pitch (0 = off).
inline constexpr const char* analogAmt = "analogAmt";

// Pre-filter saturation and a one-sample resonant feedback path into the
// main filter (the screech/scream character at high settings).
inline constexpr const char* fDrive    = "fDrive";
inline constexpr const char* fFeedback = "fFeedback";

// Master quality: renders the oscillator + filter path at 2x oversampling
// (halves aliasing in the audible band) at the cost of extra CPU.
inline constexpr const char* masterHQ = "masterHQ";

// Modulation matrix: 8 slots, each a source + destination + bipolar amount,
// plus a per-slot curve (LIN/EXP/SIN) and lag (slew) amount. A second "B"
// bank stores an alternate src/dst/amt set per slot; the modBank switch
// chooses which bank feeds the engine (curve/lag are shared).
inline constexpr int modSlots = 8;
inline juce::String modSrc (int i) { return juce::String ("mod") + juce::String (i + 1) + "Src"; }
inline juce::String modDst (int i) { return juce::String ("mod") + juce::String (i + 1) + "Dst"; }
inline juce::String modAmt (int i) { return juce::String ("mod") + juce::String (i + 1) + "Amt"; }
inline juce::String modCurve (int i) { return juce::String ("mod") + juce::String (i + 1) + "Curve"; }
inline juce::String modLag  (int i) { return juce::String ("mod") + juce::String (i + 1) + "Lag"; }
inline juce::String modBSrc (int i) { return juce::String ("modB") + juce::String (i + 1) + "Src"; }
inline juce::String modBDst (int i) { return juce::String ("modB") + juce::String (i + 1) + "Dst"; }
inline juce::String modBAmt (int i) { return juce::String ("modB") + juce::String (i + 1) + "Amt"; }
inline constexpr const char* modBank = "modBank";   // false = bank A, true = bank B

// Modulation sources (matrix + existing fixed rows share this vocabulary).
// 0 = OFF, 1 = LFO 1, 2 = LFO 2, 3 = ENV F, 4 = ENV A, 5 = velocity,
// 6 = modwheel, 7 = sample & hold (block-rate dice), 8 = aftertouch
// (channel pressure), 9/10 = MACRO A/B (also MIDI CC 14/15).
inline const juce::StringArray& modSourceName()
{
    static const juce::StringArray names { "OFF", "LFO 1", "LFO 2", "ENV F",
                                           "ENV A", "VELOCITY", "MODWHEEL",
                                           "S&H", "AFTERTOUCH", "MACRO A", "MACRO B" };
    return names;
}

// Destinations are expressed as a param id + a human name + a scale: the
// mod amount (-1..1) is multiplied by `range` to get engine units.
struct ModDest
{
    const char* param;      // target parameter id (also the state key)
    const char* label;
    float       range;      // full-scale deflection for amount = +1
};

// Destination list indices (engine and UI share these).
// Order matters: everything before mdDelayTime is applied per voice (sources
// are that voice's LFOs/envelopes/velocity/modwheel), everything from
// mdDelayTime on is a global FX destination applied once per block.
enum ModDestId
{
    mdOff = 0, mdCutoff, mdFin1, mdFin2, mdLvl1, mdLvl2,
    mdWt1, mdWt2, mdReso, mdDrive, mdPan1, mdPan2,
    mdLfo1Rate, mdLfo2Rate,
    mdDelayTime, mdDelayFb, mdRevSize, mdDelayMix, mdPhMix, mdRevMix,
    mdNumDests
};

inline const std::array<ModDest, (size_t) mdNumDests>& modDestList()
{
    static const std::array<ModDest, (size_t) mdNumDests> list {{
        { "",           "OFF",        0.0f   },
        { "cutoff",     "CUTOFF",     4.0f   },   // octaves
        { "osc1Fine",   "OSC A FIN",  1200.0f},   // cents
        { "osc2Fine",   "OSC B FIN",  1200.0f},
        { "osc1Level",  "OSC A LVL",  1.0f   },
        { "osc2Level",  "OSC B LVL",  1.0f   },
        { "osc1WtPos",  "WT POS A",   1.0f   },
        { "osc2WtPos",  "WT POS B",   1.0f   },
        { "reso",       "RESO",       1.0f   },
        { "drive",      "DRIVE",      1.0f   },
        { "osc1Pan",    "PAN A",      1.0f   },
        { "osc2Pan",    "PAN B",      1.0f   },
        { "lfo1Rate",   "LFO 1 RATE", 6.0f   },   // Hz
        { "lfo2Rate",   "LFO 2 RATE", 6.0f   },
        { "delayTime",  "DELAY TIME", 1.0f   },   // +/- 2 octaves of delay length
        { "delayFb",    "DELAY FB",   0.6f   },
        { "revSize",    "REV SIZE",   0.4f   },
        { "delayMix",   "DELAY MIX",  0.6f   },
        { "phMix",      "PHASER MIX", 0.6f   },
        { "revMix",     "REV MIX",    0.6f   },
    }};
    return list;
}
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
