#include "Parameters.h"

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;

    auto addF = [&l] (const char* id, const char* name, NormalisableRange<float> range, float def)
    {
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, range, def));
    };
    auto addI = [&l] (const char* id, const char* name, int mn, int mx, int def)
    {
        l.add (std::make_unique<AudioParameterInt> (ParameterID { id, 1 }, name, mn, mx, def));
    };
    auto addB = [&l] (const char* id, const char* name, bool def)
    {
        l.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def));
    };
    auto addC = [&l] (const char* id, const char* name, const StringArray& choices, int def)
    {
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, choices, def));
    };

    const NormalisableRange<float> envRange  (0.001f, 5.0f, 0.0f, 0.3f);
    const NormalisableRange<float> relRange  (0.01f, 12.0f, 0.0f, 0.3f);
    const NormalisableRange<float> cutRange  (20.0f, 20000.0f, 0.0f, 0.2f);
    const NormalisableRange<float> rateRange (0.02f, 20.0f, 0.0f, 0.3f);
    const NormalisableRange<float> unitRange (0.0f, 1.0f, 0.0f);
    const NormalisableRange<float> fineRange (-50.0f, 50.0f, 0.0f);
    const NormalisableRange<float> panRange  (0.0f, 1.0f, 0.0f);
    const NormalisableRange<float> phaseRange(0.0f, 360.0f, 0.0f);

    const StringArray waves    { "Saw", "Square", "PWM", "Triangle", "Sine", "User" };
    const StringArray lfoWaves { "Sine", "Triangle", "Square", "Random" };
    const StringArray targets  { "Pitch", "Cutoff", "PWM", "Volume", "WT POS A", "WT POS B", "VOWEL" };
    const StringArray fTypes   { "LP 12dB", "LP 24dB", "HP 12dB", "BP 12dB", "NOTCH" };
    const StringArray syncs    { "Off", "1/16", "1/8 Dotted", "1/8", "1/4" };
    const StringArray gateSyncs{ "1/32", "1/16", "1/16 Dotted", "1/8", "1/8 T", "1/4" };
    const StringArray arpSteps { "Rest", "0", "+1", "+2", "+3", "+4", "+5", "+6", "+7", "+8", "+9", "+10", "+11" };
    const StringArray voicings { "POLY", "MONO", "LEGATO" };

    addC (param::osc1Wave,   "OSC A Wave", waves, 0);
    addI (param::osc1Oct,    "OSC A Octave", -2, 2, 0);
    addF (param::osc1Fine,   "OSC A Fine (cents)", fineRange, 0.0f);
    addF (param::osc1Level,  "OSC A Level", unitRange, 0.85f);
    addF (param::osc1Pan,    "OSC A Pan", panRange, 0.5f);
    addF (param::osc1Phase,  "OSC A Phase (deg)", phaseRange, 0.0f);
    addB (param::osc1PRand,  "OSC A Phase Random", true);
    addF (param::osc1WtPos,  "OSC A WT Position", unitRange, 0.0f);
    addC (param::osc2Wave,   "OSC B Wave", waves, 0);
    addI (param::osc2Oct,    "OSC B Octave", -2, 2, 0);
    addF (param::osc2Fine,   "OSC B Fine (cents)", fineRange, 0.0f);
    addF (param::osc2Level,  "OSC B Level", unitRange, 0.0f);
    addF (param::osc2Pan,    "OSC B Pan", panRange, 0.5f);
    addF (param::osc2Phase,  "OSC B Phase (deg)", phaseRange, 0.0f);
    addB (param::osc2PRand,  "OSC B Phase Random", true);
    addF (param::osc2WtPos,  "OSC B WT Position", unitRange, 0.0f);
    addF (param::fmAmount,   "FM Amount", unitRange, 0.0f);
    addC (param::subWave,    "Sub Wave", { "Square", "Sine", "Triangle" }, 0);
    addI (param::subOct,     "Sub Octave", -2, 0, 0);
    addF (param::subLevel,   "Sub Level", unitRange, 0.0f);
    addF (param::noiseLevel, "Noise Level", unitRange, 0.0f);
    addI (param::uniVoices,  "Unison Voices", 1, 7, 1);
    addF (param::uniDetune,  "Unison Detune (cents)", NormalisableRange<float>(0.0f, 50.0f, 0.0f), 12.0f);
    addF (param::uniSpread,  "Unison Width", unitRange, 0.7f);
    addF (param::drift,      "Analog Drift", unitRange, 0.08f);

    addC (param::filterType, "Filter Type", fTypes, 1);
    addF (param::cutoff,     "Cutoff (Hz)", cutRange, 1500.0f);
    addF (param::reso,       "Resonance", unitRange, 0.25f);
    addF (param::envAmt,     "Env Amount (oct)", NormalisableRange<float>(0.0f, 5.0f, 0.0f), 1.0f);
    addF (param::keytrack,   "Key Track", unitRange, 0.0f);
    addF (param::drive,      "Drive", unitRange, 0.15f);
    addF (param::modDepth,   "ModWheel > Cutoff (oct)", NormalisableRange<float>(0.0f, 3.0f, 0.0f), 1.0f);

    // FILTER B: independent filter + A/B routing (SERIAL / PARALLEL / SPLIT).
    addC (param::filter2Type, "Filter B Type", fTypes, 0);
    addF (param::cutoff2,     "Cutoff B (Hz)", cutRange, 8000.0f);
    addF (param::reso2,       "Resonance B", unitRange, 0.2f);
    addC (param::filterRoute, "Filter Routing", { "SERIAL", "PARALLEL", "SPLIT" }, 0);

    addF (param::filtA, "Filter Attack",  envRange, 0.002f);
    addF (param::filtD, "Filter Decay",   envRange, 0.2f);
    addF (param::filtS, "Filter Sustain", unitRange, 0.1f);
    addF (param::filtR, "Filter Release", relRange, 0.25f);

    addF (param::ampA, "Attack",  envRange, 0.005f);
    addF (param::ampD, "Decay",   envRange, 0.25f);
    addF (param::ampS, "Sustain", unitRange, 0.85f);
    addF (param::ampR, "Release", relRange, 0.4f);

    const StringArray units { "HZ", "BPM" };
    const StringArray divs   { "1/1", "1/2", "1/4", "1/8", "1/16", "3/16" };

    addF (param::lfo1Rate,   "LFO 1 Rate (Hz)", rateRange, 4.0f);
    addC (param::lfo1Wave,   "LFO 1 Wave", lfoWaves, 0);
    addC (param::lfo1Target, "LFO 1 Target", targets, 1);
    addF (param::lfo1Depth,  "LFO 1 Depth", unitRange, 0.0f);
    addC (param::lfo1Unit,   "LFO 1 Unit", units, 0);
    addC (param::lfo1Div,    "LFO 1 Division", divs, 2);
    addF (param::lfo2Rate,   "LFO 2 Rate (Hz)", rateRange, 0.25f);
    addC (param::lfo2Wave,   "LFO 2 Wave", lfoWaves, 0);
    addC (param::lfo2Target, "LFO 2 Target", targets, 1);
    addF (param::lfo2Depth,  "LFO 2 Depth", unitRange, 0.0f);
    addC (param::lfo2Unit,   "LFO 2 Unit", units, 0);
    addC (param::lfo2Div,    "LFO 2 Division", divs, 2);

    addF (param::chorusRate,  "Chorus Rate",  NormalisableRange<float>(0.05f, 8.0f, 0.0f, 0.3f), 0.6f);
    addF (param::chorusDepth, "Chorus Depth", unitRange, 0.35f);
    addF (param::chorusMix,   "Chorus Mix",   unitRange, 0.0f);

    addF (param::phRate,  "Phaser Rate",  NormalisableRange<float>(0.02f, 8.0f, 0.0f, 0.3f), 0.6f);
    addF (param::phDepth, "Phaser Depth", unitRange, 0.5f);
    addF (param::phMix,   "Phaser Mix",   unitRange, 0.0f);

    addC (param::delaySync, "Delay Sync", syncs, 0);
    addF (param::delayTime, "Delay Time (ms)", NormalisableRange<float>(10.0f, 2000.0f, 0.0f, 0.5f), 375.0f);
    addF (param::delayFb,   "Delay Feedback", NormalisableRange<float>(0.0f, 0.92f, 0.0f), 0.45f);
    addF (param::delayMix,  "Delay Mix", unitRange, 0.0f);

    addF (param::revSize, "Reverb Size", unitRange, 0.6f);
    addF (param::revDamp, "Reverb Damp", unitRange, 0.5f);
    addF (param::revMix,  "Reverb Mix",  unitRange, 0.0f);

    // OTT multiband compressor: 0..1 depth per band (0 = transparent), output trim.
    addF (param::ottDepth, "OTT Depth", unitRange, 0.0f);
    addF (param::ottLow,   "OTT Low Band",  unitRange, 1.0f);
    addF (param::ottMid,   "OTT Mid Band",  unitRange, 1.0f);
    addF (param::ottHigh,  "OTT High Band", unitRange, 1.0f);
    addF (param::ottOut,   "OTT Output (dB)", NormalisableRange<float>(-12.0f, 6.0f, 0.0f, 0.5f), 0.0f);

    addF (param::masterGain, "Master (dB)", NormalisableRange<float>(-60.0f, 6.0f, 0.0f, 0.4f), -3.0f);
    addF (param::glide,      "Glide (ms)", NormalisableRange<float>(0.0f, 1000.0f, 0.0f, 0.5f), 0.0f);
    addC (param::voicing,    "Voicing", voicings, 0);
    addI (param::polyMax,    "Max Voices", 1, 16, 16);
    addI (param::bendRange,  "Bend Range (semitones)", 0, 12, 2);

    // Tempo-synced trancegate: step length + chop depth + 16 on/off steps.
    addC (param::gateSync,  "Gate Step",  gateSyncs, 1);
    addF (param::gateDepth, "Gate Depth", unitRange, 0.85f);
    for (int i = 0; i < 16; ++i)
        addB (param::gateStep (i).toRawUTF8(),
              ("Gate " + String (i + 1)).toRawUTF8(),
              true);                            // all-on = gate bypassed by default

    // 16-step arp: step length, octave range, per-step semitone (Rest + 0..11)
    // and per-step velocity (0..1, 1.0 = accent).
    addC (param::arpSync,  "Arp Step", gateSyncs, 1);
    addI (param::arpOct,   "Arp Octaves", 0, 3, 1);
    for (int i = 0; i < 16; ++i)
        addC (param::arpStep (i).toRawUTF8(),
              ("Arp " + String (i + 1)).toRawUTF8(),
              arpSteps, 0);                    // all Rest = arp silent by default
    for (int i = 0; i < 16; ++i)
        addF (param::arpVel (i).toRawUTF8(),
              ("Arp Vel " + String (i + 1)).toRawUTF8(),
              unitRange, 0.9f);

    // Arp playback direction.
    addC (param::arpDir, "Arp Direction", { "UP", "DOWN", "UP-DOWN", "RANDOM", "CONVERGE" }, 0);

    // Scale quantizer: snaps arp notes (and, with Scale Lock, live playing)
    // into a musical scale relative to a root pitch class.
    addC (param::arpScale, "Arp Scale",
          { "CHROMATIC", "MINOR", "PHRYGIAN", "HARM MIN", "HUNG MIN",
            "DBL HARM", "DORIAN", "MAJOR", "PENTA MIN" }, 0);
    addC (param::arpRoot, "Arp Root",
          { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0);
    addB (param::scaleLock, "Scale Lock (live play)", false);

    // Vowel/formant filter: morphing AH-EH-EE-OH-OO bank after the filters.
    addB (param::vowelOn,    "Vowel Filter", false);
    addF (param::vowelMorph, "Vowel Morph", unitRange, 0.0f);
    addF (param::vowelRes,   "Vowel Res", unitRange, 0.5f);
    addF (param::vowelMix,   "Vowel Mix", unitRange, 0.7f);

    // Global microtuning fine offset (432 Hz = about -32 cents); the .scl
    // file itself is loaded from the UI and persisted by the processor.
    addF (param::tuningFine, "Fine Tune (cents)",
          NormalisableRange<float>(-100.0f, 100.0f, 0.0f), 0.0f);

    // Host-synced sidechain pump: OFF by default; dip falls on the beat.
    addC (param::pumpSync, "Sidechain Pump Sync",
          { "Off", "1/4", "1/8", "1/8 T", "1/16", "1/16 T" }, 0);
    addF (param::pumpDepth, "Sidechain Pump Depth", unitRange, 0.0f);

    // Analog character: tape-style wow + flutter and slow per-voice drift.
    addF (param::analogAmt, "Analog Character", unitRange, 0.0f);

    // Filter character: pre-filter drive and one-sample feedback into the
    // main filter (self-oscillation character at high settings).
    addF (param::fDrive, "Filter Drive", unitRange, 0.0f);
    addF (param::fFeedback, "Filter Feedback", unitRange, 0.0f);

    // Master quality: 2x oversampled oscillator + filter path (antialiasing).
    addB (param::masterHQ, "Master Quality (2x oversampling)", false);

    // Modulation matrix: 8 routed slots (source -> destination, bipolar amount).
    for (int i = 0; i < param::modSlots; ++i)
    {
        const String n = String (i + 1);
        addC (param::modSrc (i).toRawUTF8(), ("Mod " + n + " Source").toRawUTF8(),
              param::modSourceName(), 0);
        // Destinations are stored as the destination list index (a plain int
        // param; the UI maps index <-> ModDest row).
        addI (param::modDst (i).toRawUTF8(),
              ("Mod " + n + " Dest").toRawUTF8(), 0,
              (int) param::modDestList().size() - 1, 0);
        addF (param::modAmt (i).toRawUTF8(), ("Mod " + n + " Amount").toRawUTF8(),
              NormalisableRange<float>(-1.0f, 1.0f, 0.0f), 0.0f);
    }

    return l;
}
