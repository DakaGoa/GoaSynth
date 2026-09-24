#pragma once

#include <juce_core/juce_core.h>
#include <map>

// Offline "AI" patch designer: turns a natural-language brief into a complete
// parameter map. Vocabulary is biased toward Goa trance and DJ/club terms
// (acid, screech, rolling, full-on, darkpsy, forest, gate, wet, wide, ...).
class AiPatchGen
{
public:
    using Patch = std::map<juce::String, float>;

    struct Result
    {
        juce::String title;
        juce::String description;
        Patch patch;
    };

    // How far a re-roll may stray from the family template: SUBTLE keeps the
    // family's core choices and grooves, NORMAL re-rolls character freely,
    // WILD may redesign waves, octaves and FX ranges wholesale.
    enum class Var { Subtle, Normal, Wild };

    // Random-seeded: the same prompt produces a different (but musically
    // related) patch every time; variation strength is selectable.
    static Result generate (const juce::String& userText,
                            Var variation = Var::Normal);

    // Deterministic: same prompt + same seed + same mode = same patch (tests).
    static Result generate (const juce::String& userText, juce::uint32 seed,
                            Var variation = Var::Normal);
};
