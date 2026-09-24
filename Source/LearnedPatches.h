#pragma once

#include <juce_core/juce_core.h>
#include <algorithm>
#include <map>
#include <memory>
#include <utility>
#include <vector>

// Learned-patch memory for the offline designer: every successful cloud AI
// patch is archived (brief + patch) on disk, and later LOCAL generations are
// augmented with the best-matching archive entries. Over time the offline
// designer inherits the character of the cloud models the user actually
// liked, even with no network and no API key.
//
// Storage: one XML file per patch in %APPDATA%\GoaSynth\Learned — briefs and
// patch values only, never credentials or raw cloud replies.
namespace learned
{

struct Entry
{
    juce::String brief;       // original prompt (matched lowercased)
    juce::String title;       // ALL-CAPS model title
    juce::String engine;      // GEMINI / OPENAI / CUSTOM (provenance)
    juce::String model;       // model id that produced it
    juce::String saved;       // human-readable timestamp
    std::map<juce::String, float> patch;
};

// Directory that holds the learned bank (created on demand). Tests may
// redirect it to a scratch directory via setDirOverride().
inline std::unique_ptr<juce::File>& dirOverride()
{
    static std::unique_ptr<juce::File> p;
    return p;
}

inline void setDirOverride (const juce::File* d)
{
    dirOverride() = d != nullptr ? std::make_unique<juce::File> (*d) : nullptr;
}

inline juce::File learnedDir()
{
    if (dirOverride() != nullptr)
    {
        if (! dirOverride()->exists())
            dirOverride()->createDirectory();
        return *dirOverride();
    }

    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("GoaSynth").getChildFile ("Learned");
    if (! dir.exists())
        dir.createDirectory();
    return dir;
}

// Archive a successful cloud patch. Best-effort: returns false when the patch
// is empty or the write failed; never throws.
inline bool record (const juce::String& brief, const juce::String& title,
                    const std::map<juce::String, float>& patch,
                    const juce::String& engine, const juce::String& model)
{
    if (patch.empty())
        return false;

    auto f = learnedDir().getChildFile ("lp_" + juce::Uuid().toString() + ".xml");
    auto xml = std::make_unique<juce::XmlElement> ("LearnedPatch");
    xml->setAttribute ("brief", brief.substring (0, 300));
    xml->setAttribute ("title", title.substring (0, 40));
    xml->setAttribute ("engine", engine.substring (0, 16));
    xml->setAttribute ("model", model.substring (0, 60));
    xml->setAttribute ("saved", juce::Time::getCurrentTime().toString (true, true));

    auto* params = xml->createNewChildElement ("Params");
    for (const auto& [id, v] : patch)
        params->setAttribute (id, juce::String (v, 6));

    return f.replaceWithText (xml->toString());
}

// Load every archived patch. Corrupt or empty files are skipped silently.
// Only the most recently modified maxFiles entries are considered, which
// bounds disk IO as the bank grows and keeps the flavour recent.
inline std::vector<Entry> loadAll (int maxFiles = 150)
{
    std::vector<Entry> out;
    auto files = learnedDir().findChildFiles (juce::File::findFiles, false, "lp_*.xml");

    // Newest first, ALWAYS — not only when trimming. The directory order the OS
    // returns is effectively random (the filenames are UUIDs), so without this
    // two equally-scoring archive entries would win by chance, and the offline
    // designer's flavour would depend on NTFS internals rather than on which
    // patch the user liked most recently.
    std::stable_sort (files.begin(), files.end(),
                      [] (const juce::File& a, const juce::File& b)
                      { return a.getLastModificationTime() > b.getLastModificationTime(); });

    if ((int) files.size() > maxFiles)
        files.removeRange (maxFiles, files.size() - maxFiles);

    for (const auto& f : files)
    {
        auto xml = juce::parseXML (f);
        if (xml == nullptr || ! xml->hasTagName ("LearnedPatch"))
            continue;

        Entry e;
        e.brief  = xml->getStringAttribute ("brief");
        e.title  = xml->getStringAttribute ("title");
        e.engine = xml->getStringAttribute ("engine");
        e.model  = xml->getStringAttribute ("model");
        e.saved  = xml->getStringAttribute ("saved");
        if (auto* params = xml->getChildByName ("Params"))
            for (int i = 0; i < params->getNumAttributes(); ++i)
                e.patch[params->getAttributeName (i)] = params->getAttributeValue (i).getFloatValue();

        if (! e.patch.empty() && e.brief.trim().isNotEmpty())
            out.push_back (std::move (e));
    }
    return out;
}

// Score a stored brief/title against a new brief: distinct-word overlap over
// 4+ letter words, normalised by the shorter side. Returns 0 when nothing
// overlaps, ~100 for near-identical wording.
inline int matchScore (const juce::String& storedText, const juce::String& newBrief)
{
    const auto a = juce::StringArray::fromTokens (storedText.toLowerCase(), " ,.;:!?", "\"'");
    const auto b = juce::StringArray::fromTokens (newBrief.toLowerCase(), " ,.;:!?", "\"'");

    juce::StringArray wa, wb;
    for (const auto& w : a)
        if (w.length() >= 4) wa.add (w);
    for (const auto& w : b)
        if (w.length() >= 4) wb.add (w);

    const int minSize = juce::jmin (wa.size(), wb.size());
    if (minSize == 0)
        return 0;

    int hits = 0;
    for (const auto& w : wa)
        if (wb.contains (w))
            ++hits;

    return hits * 100 / minSize;
}

// Up to maxCount best-matching entries with a score of at least minScore.
// loadAll() orders newest-first and the sort below is stable, so on a tie the
// more recently archived patch wins — deterministic, not directory order.
inline std::vector<Entry> bestMatch (const juce::String& brief, int maxCount = 2, int minScore = 25)
{
    std::vector<Entry> out;
    auto all = loadAll();
    if (all.empty())
        return out;

    std::vector<std::pair<int, const Entry*>> scored;
    scored.reserve (all.size());
    for (const auto& e : all)
    {
        // The model title often carries the strongest signal ("DARK ACID
        // SCREECH"), so match against both brief and title, best wins.
        const int s = juce::jmax (matchScore (e.brief, brief), matchScore (e.title, brief));
        scored.emplace_back (s, &e);
    }

    std::stable_sort (scored.begin(), scored.end(),
                      [] (const auto& a, const auto& b) { return a.first > b.first; });

    for (const auto& [s, e] : scored)
    {
        if ((int) out.size() >= maxCount)
            break;
        if (s >= minScore)
            out.push_back (*e);
    }
    return out;
}

} // namespace learned
