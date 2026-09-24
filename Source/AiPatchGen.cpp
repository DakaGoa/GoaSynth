#include "AiPatchGen.h"

namespace
{
// Musically-tasteful random choices for the variation engine.
constexpr float detuneTable[8]  = { 9.0f, 12.0f, 15.0f, 18.0f, 22.0f, 25.0f, 30.0f, 34.0f };
constexpr float delayDivTable[4] = { 1.0f, 2.0f, 3.0f, 4.0f };  // 1/8, 1/16., 1/8., 1/4
}

//==============================================================================
// Vocabulary-driven patch designer. Families stack (e.g. "dark rolling acid
// bass" = acid family + dark/bass tweaks), then keyword tweaks refine the
// result, then a seeded variation pass guarantees that no two generations are
// identical while staying musically related. Everything maps onto real
// GoaSynth parameter ids.
//==============================================================================
AiPatchGen::Result AiPatchGen::generate (const juce::String& userText, juce::uint32 seed,
                                         Var variation)
{
    juce::Random rng (seed == 0 ? (juce::uint64) juce::Time::getHighResolutionTicks()
                                : (juce::uint64) seed);
    Patch p;
    const juce::String t = (userText.toLowerCase() + "  ").trim();
    // Variation-strength flags: visible to the family templates, the variation
    // pass and the groove generators alike.
    const bool subtle = variation == Var::Subtle;
    const bool wild   = variation == Var::Wild;

    // ---- 1) requested families -------------------------------------------------
    bool acid    = t.contains ("acid") || t.contains ("303") || t.contains ("squelch") || t.contains ("squeak");
    bool screech = t.contains ("screech") || t.contains ("razor") || t.contains ("darkpsy") || t.contains ("hi-tech")
                   || t.contains ("hitech") || t.contains ("gnarly") || t.contains ("aggressive") || t.contains ("destroy");
    bool riser   = t.contains ("riser") || t.contains ("uplift") || t.contains ("sweep") || t.contains ("buildup")
                   || t.contains ("build") || t.contains ("laser");
    bool bell    = t.contains ("bell") || t.contains ("crystal") || t.contains ("chime") || t.contains ("sparkle")
                   || t.contains ("pixel") || t.contains ("pixie");
    bool pluck   = t.contains ("pluck") || t.contains ("arp") || t.contains ("arpegg") || t.contains ("sequence")
                   || t.contains ("sequenced") || t.contains ("rolling") || t.contains ("ping");
    bool sub     = t.contains ("sub") || t.contains ("deep bass") || t.contains ("lowend") || t.contains ("low end")
                   || t.contains ("808");
    bool pad     = t.contains ("pad") || t.contains ("atmos") || t.contains ("swell") || t.contains ("drone")
                   || t.contains ("forest") || t.contains ("background") || t.contains ("soundscape");
    bool hoo     = t.contains ("hoover") || t.contains ("rusk") || t.contains ("menac") || t.contains ("evil")
                   || t.contains ("growl") || t.contains ("dominator");
    bool noise   = t.contains ("noise") || t.contains ("hiss") || t.contains ("wind") || t.contains ("texture");
    bool saw     = t.contains ("saw") || t.contains ("supersaw") || t.contains ("lead") || t.contains ("stab")
                   || t.contains ("full-on") || t.contains ("fullon") || t.contains ("melodic") || t.contains ("anthem");

    const int hits = (acid?1:0) + (screech?1:0) + (riser?1:0) + (bell?1:0) + (pluck?1:0)
                   + (sub?1:0) + (pad?1:0) + (hoo?1:0) + (noise?1:0) + (saw?1:0);
    if (hits == 0)
        saw = true; // sensible default: a big goa lead

    // ---- 2) family patches ------------------------------------------------------
    if (acid)   // 303-style: mono, glide, screaming resonant LP, short env
    {
        p["osc1Wave"] = 0.0f; p["osc1Oct"] = -1.0f; p["osc1Level"] = 0.9f;
        p["subWave"] = 0.0f;  p["subLevel"] = 0.35f;
        p["filterType"] = 1.0f; p["cutoff"] = 420.0f; p["reso"] = 0.75f;
        p["envAmt"] = 2.4f; p["keytrack"] = 0.2f; p["drive"] = 0.4f;
        p["filtA"] = 0.002f; p["filtD"] = 0.22f; p["filtS"] = 0.05f; p["filtR"] = 0.15f;
        p["ampA"] = 0.002f; p["ampD"] = 0.30f; p["ampS"] = 0.15f; p["ampR"] = 0.12f;
        p["glide"] = 80.0f; p["voicing"] = 1.0f;
    }
    if (screech) // detuned HP scream with phaser movement
    {
        p["osc1Wave"] = 0.0f; p["osc1Level"] = 0.8f;
        p["osc2Wave"] = 0.0f; p["osc2Fine"] = 14.0f; p["osc2Level"] = 0.55f;
        p["fmAmount"] = 0.15f; p["subLevel"] = 0.1f;
        p["uniVoices"] = 3.0f; p["uniDetune"] = 25.0f; p["uniSpread"] = 1.0f;
        p["filterType"] = 2.0f; p["cutoff"] = 1800.0f; p["reso"] = 0.85f;
        p["envAmt"] = 2.5f; p["drive"] = 0.5f;
        p["phRate"] = 1.2f; p["phDepth"] = 0.6f; p["phMix"] = 0.3f;
        p["delaySync"] = 3.0f; p["delayFb"] = 0.45f; p["delayMix"] = 0.18f;
        p["bendRange"] = 7.0f;
    }
    if (riser)   // long filter sweep, pitch-bend playground
    {
        p["osc1Wave"] = 0.0f; p["osc1Oct"] = 1.0f; p["osc1Level"] = 0.7f;
        p["osc2Wave"] = 4.0f; p["osc2Oct"] = 2.0f; p["osc2Level"] = 0.4f;
        p["fmAmount"] = 0.3f;
        p["filterType"] = 1.0f; p["cutoff"] = 300.0f; p["reso"] = 0.85f; p["envAmt"] = 4.5f;
        p["filtA"] = 3.5f; p["filtD"] = 3.0f; p["filtS"] = 0.95f; p["filtR"] = 1.0f;
        p["ampA"] = 2.5f; p["ampD"] = 2.0f; p["ampS"] = 0.9f; p["ampR"] = 0.8f;
        p["delayMix"] = 0.25f; p["revMix"] = 0.35f; p["bendRange"] = 12.0f;
    }
    if (bell)    // FM ping with dotted delays and long tail
    {
        p["osc1Wave"] = 4.0f; p["osc1Oct"] = 1.0f; p["osc1Level"] = 0.7f; p["osc1Pan"] = 0.38f;
        p["osc2Wave"] = 4.0f; p["osc2Oct"] = -1.0f; p["osc2Level"] = 0.7f; p["osc2Pan"] = 0.62f;
        p["fmAmount"] = 0.55f;
        p["filterType"] = 1.0f; p["cutoff"] = 5000.0f; p["reso"] = 0.1f;
        p["ampA"] = 0.002f; p["ampD"] = 0.8f; p["ampS"] = 0.2f; p["ampR"] = 2.5f;
        p["delaySync"] = 2.0f; p["delayFb"] = 0.55f; p["delayMix"] = 0.35f;
        p["revSize"] = 0.8f; p["revMix"] = 0.35f; p["chorusMix"] = 0.25f;
    }
    if (pluck)   // tight sequenced pluck, 1/16 delay throws
    {
        p["osc1Wave"] = 0.0f; p["osc1Level"] = 0.85f;
        p["osc2Wave"] = 0.0f; p["osc2Fine"] = 7.0f; p["osc2Level"] = 0.5f;
        p["subLevel"] = 0.2f;
        p["uniVoices"] = 3.0f; p["uniDetune"] = 9.0f; p["uniSpread"] = 0.8f;
        p["filterType"] = 1.0f; p["cutoff"] = 2800.0f; p["reso"] = 0.35f; p["envAmt"] = 1.1f;
        p["filtA"] = 0.001f; p["filtD"] = 0.11f; p["filtS"] = 0.1f; p["filtR"] = 0.2f;
        p["ampA"] = 0.002f; p["ampD"] = 0.14f; p["ampS"] = 0.1f; p["ampR"] = 0.25f;
        p["delaySync"] = 1.0f; p["delayFb"] = 0.4f; p["delayMix"] = 0.22f;
    }
    if (sub)     // deep clean sine/square lowend
    {
        p["osc1Wave"] = 4.0f; p["osc1Oct"] = -1.0f; p["osc1Level"] = 0.7f;
        p["subWave"] = 1.0f; p["subOct"] = -1.0f; p["subLevel"] = 0.6f;
        p["filterType"] = 0.0f; p["cutoff"] = 900.0f; p["reso"] = 0.1f;
        p["envAmt"] = 0.3f; p["keytrack"] = 0.3f; p["drive"] = 0.05f;
        p["ampA"] = 0.008f; p["ampD"] = 0.3f; p["ampS"] = 0.9f; p["ampR"] = 0.3f;
        p["voicing"] = 1.0f; p["glide"] = 25.0f;
    }
    if (pad)     // slow swirling analog pad
    {
        p["osc1Wave"] = 2.0f; p["osc1Level"] = 0.55f;
        p["osc2Wave"] = 3.0f; p["osc2Fine"] = -8.0f; p["osc2Level"] = 0.5f;
        p["fmAmount"] = 0.08f; p["subLevel"] = 0.25f;
        p["uniVoices"] = 4.0f; p["uniDetune"] = 15.0f; p["drift"] = 0.12f;
        p["filterType"] = 0.0f; p["cutoff"] = 1400.0f; p["reso"] = 0.45f; p["envAmt"] = 1.0f;
        p["filtA"] = 1.8f; p["filtD"] = 2.5f; p["filtS"] = 0.6f; p["filtR"] = 3.0f;
        p["ampA"] = 1.8f; p["ampD"] = 1.5f; p["ampS"] = 0.85f; p["ampR"] = 3.5f;
        p["lfo1Rate"] = 0.4f; p["lfo1Target"] = 1.0f; p["lfo1Depth"] = 0.15f;
        p["lfo2Rate"] = 0.23f; p["lfo2Target"] = 0.0f; p["lfo2Depth"] = 0.06f;
        p["phRate"] = 0.3f; p["phDepth"] = 0.7f; p["phMix"] = 0.45f;
        p["chorusMix"] = 0.55f; p["revSize"] = 0.85f; p["revMix"] = 0.4f;
    }
    if (hoo)     // dark detuned hoover growl
    {
        p["osc1Wave"] = 0.0f; p["osc1Level"] = 0.8f;
        p["osc2Wave"] = 2.0f; p["osc2Fine"] = -12.0f; p["osc2Level"] = 0.7f;
        p["fmAmount"] = 0.22f;
        p["uniVoices"] = 5.0f; p["uniDetune"] = 34.0f; p["uniSpread"] = 1.0f;
        p["filterType"] = 0.0f; p["cutoff"] = 1400.0f; p["envAmt"] = 1.0f;
        p["ampA"] = 0.05f; p["ampD"] = 0.5f; p["ampS"] = 0.85f; p["ampR"] = 0.9f;
        p["delaySync"] = 2.0f; p["delayMix"] = 0.2f; p["revMix"] = 0.3f;
    }
    if (noise)   // airy texture: noise through HP, oscillators off
    {
        p["noiseLevel"] = 0.35f;
        p["osc1Level"] = 0.0f; p["osc2Level"] = 0.0f; p["subLevel"] = 0.0f;
        p["filterType"] = 2.0f; p["cutoff"] = 4000.0f; p["reso"] = 0.2f;
        p["ampA"] = 0.5f; p["ampD"] = 0.1f; p["ampS"] = 0.8f; p["ampR"] = 1.5f;
        p["revMix"] = 0.3f;
    }
    if (saw)     // supersaw lead / stab
    {
        p["osc1Wave"] = 0.0f; p["osc1Level"] = 0.8f;
        p["osc2Wave"] = 0.0f; p["osc2Fine"] = 12.0f; p["osc2Level"] = 0.7f;
        p["subLevel"] = 0.2f;
        p["uniVoices"] = 7.0f; p["uniDetune"] = 30.0f; p["uniSpread"] = 1.0f;
        p["filterType"] = 0.0f; p["cutoff"] = 4500.0f; p["reso"] = 0.1f; p["envAmt"] = 1.0f;
        p["ampA"] = 0.008f; p["ampD"] = 0.25f; p["ampS"] = 0.85f; p["ampR"] = 0.45f;
        p["chorusMix"] = 0.4f; p["delaySync"] = 4.0f; p["delayMix"] = 0.3f;
        p["bendRange"] = 2.0f;
    }

    // ---- 3) DJ / Goa subgenre tweaks --------------------------------------------
    if (t.contains ("full-on") || t.contains ("fullon") || t.contains ("morning") || t.contains ("sunrise"))
    {
        p["delaySync"] = 3.0f; p["delayMix"] = 0.28f; p["cutoff"] = 6000.0f; p["revMix"] = 0.3f; p["chorusMix"] = 0.3f;
    }
    if (t.contains ("darkpsy") || t.contains ("hi-tech") || t.contains ("hitech"))
    {
        p["uniDetune"] = 40.0f; p["phMix"] = 0.5f; p["phDepth"] = 0.8f; p["drive"] = 0.6f; p["revMix"] = 0.2f;
    }
    if (t.contains ("forest"))
    {
        p["lfo2Wave"] = 3.0f; p["lfo2Rate"] = 0.3f; p["lfo2Target"] = 0.0f; p["lfo2Depth"] = 0.1f;
        p["revSize"] = 0.9f; p["revMix"] = 0.45f;
    }
    if (t.contains ("night"))
    {
        p["cutoff"] = 900.0f; p["reso"] = 0.3f;
    }
    if (t.contains ("wet") || t.contains ("spacey") || t.contains ("spacy") || t.contains ("dubby"))
    {
        p["delayMix"] = 0.30f; p["delayFb"] = 0.5f; p["delaySync"] = 3.0f; p["revMix"] = 0.30f; p["chorusMix"] = 0.3f;
    }
    if (t.contains ("dry") || t.contains ("club") || t.contains ("tight mix"))
    {
        p["delayMix"] = 0.0f; p["revMix"] = 0.05f; p["chorusMix"] = 0.0f;
    }
    if (t.contains ("wide") || t.contains ("stereo") || t.contains ("hugen"))
    {
        p["uniSpread"] = 1.0f; p["chorusMix"] = 0.35f; p["osc1Pan"] = 0.35f; p["osc2Pan"] = 0.65f;
    }
    if (t.contains ("mono"))
        p["voicing"] = 1.0f;
    if (t.contains ("bass") || t.contains ("low"))
    {
        p["voicing"] = 1.0f; p["osc1Oct"] = -1.0f;
    }
    if (t.contains ("smooth") || t.contains ("slow attack") || t.contains ("swell"))
    {
        p["ampA"] = 0.8f; p["filtA"] = 0.8f;
    }
    if (t.contains ("punchy") || t.contains ("plink") || t.contains ("stabby"))
    {
        p["ampA"] = 0.001f; p["ampD"] = 0.15f; p["ampS"] = 0.1f; p["ampR"] = 0.15f;
        p["filtD"] = 0.12f; p["filtS"] = 0.0f;
    }

    // ---- 3.5) variation pass -------------------------------------------------
    // Two layers, so every generation is different but always on-style:
    //   a) character dice: waveform flavours, octave placement, FX timing
    //   b) humanisation: small relative jitter on the already-set values
    // The variation-strength control scales both: SUBTLE keeps the family's
    // core choices and grooves and only breathes on the details; WILD
    // redesigns waves, octaves and FX ranges wholesale.
    {
        const bool isBass = t.contains ("bass") || t.contains ("sub") || t.contains ("low");
        const bool isPad  = t.contains ("pad") || t.contains ("atmos") || t.contains ("drone");

        // Base waveforms (only where the family didn't already make a choice;
        // SUBTLE never second-guesses the family's wave selection).
        if (p.count ("osc1Wave") == 0)
            p["osc1Wave"] = (float) rng.nextInt (5);          // any ana wave
        if (p.count ("osc2Wave") == 0 && rng.nextBool())
            p["osc2Wave"] = (float) rng.nextInt (5);

        // Octave placement: basses sit low, leads mostly stay, sometimes +1.
        if (p.count ("osc1Oct") == 0)
        {
            if (isBass) p["osc1Oct"] = (float) (-2 + rng.nextInt (2));
            else        p["osc1Oct"] = (float) (rng.nextInt (4) == 0 ? 1 : 0);
        }

        // Unison character: detune spread + voice count + width roll.
        if (p.count ("uniDetune") != 0)
            p["uniDetune"] = detuneTable[rng.nextInt (8)];
        if (p.count ("uniVoices") != 0)
            p["uniVoices"] = (float) (3 + rng.nextInt (5));   // 3..7
        if (p.count ("uniSpread") != 0)
            p["uniSpread"] = isPad ? 1.0f : (rng.nextBool() ? 1.0f : 0.85f);

        // Filter starting point: +/- a major third (half an octave when WILD).
        if (auto it = p.find ("cutoff"); it != p.end() && it->second > 100.0f)
            it->second = juce::jlimit (80.0f, 18000.0f,
                it->second * std::exp2 ((rng.nextFloat() - 0.5f)
                                        * (wild ? 1.32f : 0.66f)));

        // Stereo pan spread of the two oscillators.
        if (p.count ("osc1Pan") == 0 && p.count ("osc2Pan") == 0 && rng.nextBool())
        {
            const float l = 0.30f + rng.nextFloat() * 0.15f;  // 0.30..0.45
            p["osc1Pan"] = l;
            p["osc2Pan"] = 1.0f - l;
        }

        // Delay timing: any musical division (SUBTLE keeps the family's),
        // feedback 0.30-0.50, or 0.25-0.65 when WILD.
        if (p.count ("delaySync") != 0 && ! subtle)
            p["delaySync"] = delayDivTable[rng.nextInt (4)];
        if (auto it = p.find ("delayFb"); it != p.end())
            it->second = wild ? (0.25f + rng.nextFloat() * 0.40f)
                              : (0.30f + rng.nextFloat() * 0.20f);

        // Reverb size wobble within a usable band; LFO rate +/- 20%.
        if (auto it = p.find ("revSize"); it != p.end())
            it->second = 0.55f + rng.nextFloat() * 0.35f;
        for (const char* id : { "lfo1Rate", "lfo2Rate" })
            if (auto it = p.find (id); it != p.end())
                it->second *= 0.8f + rng.nextFloat() * 0.4f;

        // (b) humanisation: relative jitter, never creating new entries.
        // SUBTLE halves the jitter; WILD doubles it.
        const float js = subtle ? 0.5f : (wild ? 2.0f : 1.0f);
        auto jitter = [&] (const char* id, float rel, float lo, float hi)
        {
            if (auto it = p.find (id); it != p.end())
                it->second = juce::jlimit (lo, hi,
                    it->second * (1.0f + (rng.nextFloat() - 0.5f) * 2.0f * rel * js));
        };
        jitter ("reso",      0.10f, 0.0f, 1.0f);
        jitter ("drive",    0.12f, 0.0f, 1.0f);
        jitter ("envAmt",   0.12f, 0.0f, 9.0f);
        jitter ("fmAmount", 0.12f, 0.0f, 1.0f);
        jitter ("ampA",     0.25f, 0.001f, 8.0f);
        jitter ("ampD",     0.20f, 0.01f, 8.0f);
        jitter ("ampR",     0.20f, 0.02f, 8.0f);
        jitter ("filtD",    0.20f, 0.01f, 8.0f);
        jitter ("delayMix", 0.18f, 0.0f, 1.0f);
        jitter ("revMix",   0.18f, 0.0f, 1.0f);
        jitter ("chorusMix",0.15f, 0.0f, 1.0f);

        // OTT: leads/pads/basses get a squeeze with band balance variation;
        // sparse textures (plucks, FX) usually stay clean. Rolling a band
        // depth that the family did not set keeps generations distinct.
        const bool squeezeFamily = t.contains ("lead") || t.contains ("pad")
                                || t.contains ("bass") || t.contains ("acid")
                                || t.contains ("supersaw") || t.contains ("saw");
        if (squeezeFamily || rng.nextFloat() < 0.35f)
        {
            p["ottDepth"] = 0.45f + rng.nextFloat() * 0.4f;
            p["ottLow"]   = 0.6f  + rng.nextFloat() * 0.4f;
            p["ottMid"]   = 0.7f  + rng.nextFloat() * 0.3f;
            p["ottHigh"]  = 0.75f + rng.nextFloat() * 0.25f;
            if (! squeezeFamily)
                p["ottDepth"] *= 0.5f;                       // gentle on sparse textures
        }
    }

    if (t.contains ("gate") || t.contains ("gated") || t.contains ("chop")
        || t.contains ("trancegate"))
    {
        p["ampR"] = 0.04f; p["ampS"] = 0.6f;
        // A seeded trancegate groove with guaranteed breath (start on, no
        // 3-offs in a row). SUBTLE keeps the family's division and lays a
        // classic sparse 3-on/1-off grid; NORMAL/WILD re-roll everything.
        p["gateDepth"] = 0.75f + rng.nextFloat() * 0.2f;
        if (subtle)
        {
            p["gateSync"] = 1.0f;                              // 1/16
            for (int i = 0; i < 16; ++i)
                p[juce::String ("gate") + juce::String (i + 1)] = (i % 4 == 3) ? 0.0f : 1.0f;
        }
        else
        {
            p["gateSync"] = (float) rng.nextInt (6);
            const int onPct = wild ? (35 + rng.nextInt (55))   // 35..89% on
                                   : (50 + rng.nextInt (35));  // 50..84% on
            for (int i = 0; i < 16; ++i)
                p[juce::String ("gate") + juce::String (i + 1)]
                    = (rng.nextInt (100) < onPct) ? 1.0f : 0.0f;
            p[juce::String ("gate") + juce::String (1)] = 1.0f; // never silent bar start
            for (int i = 2; i < 16; ++i)
            {
                auto on = [&p] (int k)
                { return p[juce::String ("gate") + juce::String (k + 1)] > 0.5f; };
                if (! on (i) && ! on (i - 1) && ! on (i - 2))   // 3 offs in a row
                    p[juce::String ("gate") + juce::String (i + 1)] = 1.0f;
            }
            if (t.contains ("16") || t.contains ("roller"))
                p["gateSync"] = 0.0f;                   // 1/32 rollers
            if (t.contains ("slow") || t.contains ("1/8"))
                p["gateSync"] = 3.0f;                   // 1/8 chops
        }
    }
    if (t.contains ("arp") || t.contains ("arpegg") || t.contains ("sequence")
        || t.contains ("sequenced") || t.contains ("rolling") || t.contains ("run"))
    {
        // A seeded rolling run: root motion, triad shape and accents are all
        // dice-thrown, so no two runs come out alike (choice values: 0=Rest,
        // 1..12 = +0..+11 semitones). SUBTLE lays the classic minor-triad
        // 16th roller; NORMAL/WILD re-roll triad shape, air and range.
        p["arpSync"] = (float) (subtle ? 1 : (1 + rng.nextInt (2)));  // 1/16 or 1/8
        p["arpOct"]  = (float) (subtle ? 1 : rng.nextInt (3));        // 0..2 range
        const bool minor  = subtle || rng.nextBool();
        const bool add9   = ! subtle && rng.nextBool();
        const int  third  = minor ? 3 : 4;
        const int  airPct = subtle ? 12 : (wild ? 40 : 25);
        int run[16] = {};
        for (int i = 0; i < 16; ++i)
        {
            if (rng.nextInt (100) < airPct && i != 0)
                continue;                                          // air
            const int pos = i % 4;
            run[i] = pos == 0 ? 1                                  // root
                   : pos == 1 ? 1 + third                          // third
                   : pos == 2 ? 8                                  // octave
                              : (add9 ? 10 : 1 + 7);               // 9th or fifth
        }
        run[0] = 1;                                                 // root downbeat
        for (int i = 0; i < 16; ++i)
        {
            p[juce::String ("arp") + juce::String (i + 1)] = (float) run[i];
            const float accent = (i % 4 == 0) ? 1.0f
                               : (i % 2 == 0 ? 0.6f + rng.nextFloat() * 0.15f
                                             : 0.35f + rng.nextFloat() * 0.2f);
            p[juce::String ("arpVel") + juce::String (i + 1)] = accent;
        }
    }
    if (t.contains ("bright") || t.contains ("airy"))
    {
        p["cutoff"] = 6000.0f; p["drive"] = 0.1f;
    }
    if (t.contains ("dark") || t.contains ("muffled") || t.contains ("muted"))
    {
        p["cutoff"] = 700.0f; p["reso"] = 0.25f;
    }
    if (t.contains ("detune"))
    {
        p["uniVoices"] = 7.0f; p["uniDetune"] = 35.0f;
    }
    if (t.contains ("fm") || t.contains ("metal") || t.contains ("alien") || t.contains ("robot"))
    {
        p["fmAmount"] = 0.7f;
    }
    if (t.contains ("glide") || t.contains ("portamento") || t.contains ("slide"))
    {
        p["glide"] = 120.0f;
    }
    if (t.contains ("bpm") || t.contains ("tempo") || t.contains ("synced") || t.contains ("sync"))
    {
        p["delaySync"] = 3.0f;
        if (p.count ("delayMix") == 0 || p["delayMix"] < 0.05f)
            p["delayMix"] = 0.22f;
    }

    // ---- 4) title + description ---------------------------------------------------
    juce::String title = "AI SUPERSAW";
    if      (acid)    title = "AI ACID DRIVE";
    else if (screech) title = "AI SCREECH LEAD";
    else if (riser)   title = "AI LASER RISER";
    else if (bell)    title = "AI CRYSTAL BELL";
    else if (pluck)   title = "AI PLUCK SEQ";
    else if (sub)     title = "AI DEEP SUB";
    else if (pad)     title = "AI SWIRL PAD";
    else if (hoo)     title = "AI HOOVER";
    else if (noise)   title = "AI NOISE TEXTURE";

    // Variant suffix so repeated generations of the same brief are easy to
    // tell apart in the preset browser.
    {
        static constexpr const char* variants[] =
            { "", " II", " NEO", " PRIME", " V2", " X" };
        title += variants[rng.nextInt (6)];
    }

    juce::StringArray tags;
    if (acid)    tags.add ("acid");
    if (screech) tags.add ("screech");
    if (riser)   tags.add ("riser");
    if (bell)    tags.add ("FM bell");
    if (pluck)   tags.add ("pluck");
    if (sub)     tags.add ("deep sub");
    if (pad)     tags.add ("pad");
    if (hoo)     tags.add ("hoover");
    if (noise)   tags.add ("noise");
    if (saw)     tags.add ("supersaw");
    if ((int) p["voicing"] == 1 && p.count ("glide") > 0 && p["glide"] > 0.0f)
        tags.add ("mono glide");
    if ((int) p["voicing"] == 2)
        tags.add ("legato");
    if (p.count ("delayMix") > 0 && p["delayMix"] > 0.05f)
        tags.add ("tempo delay");
    if (p.count ("revMix") > 0 && p["revMix"] > 0.2f)
        tags.add ("space");

    return { title, tags.joinIntoString (" \u2022 "), p };
}

AiPatchGen::Result AiPatchGen::generate (const juce::String& userText, Var variation)
{
    // Seed 0 = random seed from the clock, so the same prompt never yields
    // the same patch twice in a row.
    return generate (userText, 0, variation);
}
