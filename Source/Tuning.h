#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <cmath>
#include <string>
#include <vector>

// Tuning helpers shared by the engine and the arp sequencer:
//
//  1. Scale quantizer — snaps MIDI notes to a musical scale relative to a
//     root pitch class (Phrygian, harmonic minor, Hungarian minor... the
//     Goa-trance staples).
//  2. Scala (.scl) microtuning — parses the classic Scala text format and
//     provides a lock-free note->frequency table the audio thread can read
//     while the message thread swaps in a new tuning.
namespace tuning
{

//==============================================================================
// Scale tables as semitone offsets from the root pitch class. Index 0 is
// CHROMATIC = no quantization.
struct Scale
{
    const char* name;
    std::vector<int> semis;
};

inline const std::vector<Scale>& scales()
{
    static const std::vector<Scale> s {
        { "CHROMATIC", { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } },
        { "MINOR",     { 0, 2, 3, 5, 7, 8, 10 } },
        { "PHRYGIAN",  { 0, 1, 3, 5, 7, 8, 10 } },
        { "HARM MIN",  { 0, 2, 3, 5, 7, 8, 11 } },
        { "HUNG MIN",  { 0, 2, 3, 6, 7, 8, 11 } },
        { "DBL HARM",  { 0, 1, 4, 5, 7, 8, 11 } },
        { "DORIAN",    { 0, 2, 3, 5, 7, 9, 10 } },
        { "MAJOR",     { 0, 2, 4, 5, 7, 9, 11 } },
        { "PENTA MIN", { 0, 3, 5, 7, 10 } },
    };
    return s;
}

inline int numScales() { return (int) scales().size(); }

// Nearest in-scale note (ties snap down). scaleIdx 0 = chromatic = unchanged.
inline int nearestScaleNote (int midiNote, int scaleIdx, int rootPc)
{
    const auto& table = scales();
    if (scaleIdx <= 0 || scaleIdx >= (int) table.size())
        return midiNote;

    const auto& semis = table[(size_t) scaleIdx].semis;

    for (int off = 0; off <= 6; ++off)
    {
        for (int dir = 0; dir < (off == 0 ? 1 : 2); ++dir)
        {
            // Ties snap down: darker, and matches how Musicians correct by ear.
            const int cand = midiNote + (dir == 0 ? -off : off);
            const int cpc  = ((cand - rootPc) % 12 + 12) % 12;
            for (int s : semis)
                if (s == cpc)
                    return juce::jlimit (0, 127, cand);
        }
    }
    return midiNote;
}

//==============================================================================
// Parse Scala text: lines starting with '!' are comments; then a description
// line, a note count, and N degree lines — cents ("204.0"), ratios ("6/5")
// or decimal ratios ("1.25"). Unison (0 cents) is implicit. Returns false on
// malformed input.
inline bool parseScl (const juce::String& text, juce::String& description,
                      std::vector<double>& degreeCents)
{
    degreeCents.clear();
    description = {};

    const auto lines = juce::StringArray::fromLines (text);
    auto isNoise = [] (const juce::String& l)
    {
        const auto t = l.trim();
        return t.isEmpty() || t.startsWithChar ('!');
    };

    int i = 0;
    while (i < lines.size() && isNoise (lines[i])) ++i;
    if (i >= lines.size()) return false;
    description = lines[i++].trim().substring (0, 48);
    while (i < lines.size() && isNoise (lines[i])) ++i;
    if (i >= lines.size()) return false;

    const int n = lines[i++].trim().getIntValue();
    if (n < 2 || n > 64)
        return false;                        // unison-only or absurd scales

    // SCL convention: the count INCLUDES the octave entry (a 12-note 12-TET
    // file lists 12 degrees ending at 1200.0). Unison is implicit as degree 0.
    degreeCents.assign ((size_t) n + 1, 0.0);   // [0] = unison, [1..n] from file
    for (int d = 1; d <= n; ++d)
    {
        while (i < lines.size() && isNoise (lines[i])) ++i;
        if (i >= lines.size()) return false;
        auto tok = lines[i++].trim();
        const int sep = tok.indexOfChar (';');
        if (sep >= 0) tok = tok.substring (0, sep);      // trailing comments

        double cents = 0.0;
        if (tok.containsChar ('.'))
        {
            cents = tok.getDoubleValue();                // cents line
        }
        else if (tok.containsChar ('/'))
        {
            const auto parts = juce::StringArray::fromTokens (tok, "/", "");
            if (parts.size() != 2) return false;
            const double num = parts[0].getDoubleValue();
            const double den = parts[1].getDoubleValue();
            if (num <= 0.0 || den <= 0.0) return false;
            cents = 1200.0 * std::log2 (num / den);
        }
        else
        {
            const double ratio = tok.getDoubleValue();
            if (ratio <= 0.0) return false;
            cents = 1200.0 * std::log2 (ratio);
        }
        if (! std::isfinite (cents))
            return false;
        degreeCents[(size_t) d] = cents;
    }
    return true;
}

// Frequency (Hz) of a MIDI note under degreeCents (N degrees per octave),
// octave-wrapped, anchored so A4 (note 69) stays at 440 Hz *and* a fine
// offset in cents is applied. baseHz overrides the 440 anchor (432 Hz tuning
// etc.). Empty table = 12-TET.
inline double freqForNote (const std::vector<double>& degreeCents, int midiNote,
                           double fineCents = 0.0, double baseHz = 440.0)
{
    const int n = juce::jlimit (0, 127, midiNote);
    const double tet = 1200.0 * std::log2 (std::pow (2.0, (n - 69) / 12.0));
    if (degreeCents.size() < 2)
        return baseHz * std::pow (2.0, (tet + fineCents) / 1200.0);

    const int N = (int) degreeCents.size() - 1;   // degrees per octave (period)
    const int oct = (int) std::floor ((double) n / (double) N);
    const int deg = n - oct * N;
    const double cents = degreeCents[(size_t) deg] + 1200.0 * oct;

    const int anchorOct  = 69 / N;
    const int anchorDeg  = 69 - anchorOct * N;   // degree index of A4 in-scale
    const double anchor  = degreeCents[(size_t) anchorDeg] + 1200.0 * anchorOct;

    return baseHz * std::pow (2.0, (cents - anchor + fineCents) / 1200.0);
}

//==============================================================================
// Lock-free note->frequency table for the audio thread: the message thread
// fills the hidden buffer and flips an atomic index; renderNextBlock only
// ever reads the published one.
class ScalaTable
{
public:
    // Returns false when parsing failed (audio keeps whatever was active).
    bool loadFromText (const juce::String& sclText, double fineCents = 0.0,
                       double baseHz = 440.0)
    {
        juce::String desc;
        std::vector<double> deg;
        if (! parseScl (sclText, desc, deg))
            return false;

        const int idx = 1 - active.load (std::memory_order_relaxed);
        for (int m = 0; m < 128; ++m)
            buffers[(size_t) idx][(size_t) m] = (float) freqForNote (deg, m, fineCents, baseHz);

        name = desc;
        count = (int) deg.size() - 1;        // degrees per octave (excl. unison slot)
        active.store (idx, std::memory_order_release);
        loaded.store (true, std::memory_order_release);
        return true;
    }

    void clear()
    {
        loaded.store (false, std::memory_order_release);
        name = {};
    }

    bool isLoaded() const { return loaded.load (std::memory_order_acquire); }
    const juce::String& getName() const { return name; }
    int getDegreeCount() const { return count; }

    // Audio thread: frequency for a MIDI note; 12-TET when nothing is loaded.
    float frequencyForNote (int midiNote) const
    {
        if (! loaded.load (std::memory_order_acquire))
            return (float) (440.0 * std::pow (2.0, (midiNote - 69) / 12.0));
        const int idx = active.load (std::memory_order_acquire);
        return buffers[(size_t) idx][(size_t) juce::jlimit (0, 127, midiNote)];
    }

private:
    std::array<std::array<float, 128>, 2> buffers {};
    std::atomic<int> active { 0 };
    std::atomic<bool> loaded { false };
    juce::String name;
    int count = 0;
};

} // namespace tuning
