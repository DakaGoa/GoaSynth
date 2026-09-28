#pragma once

#include <cstdlib>
#include <juce_core/juce_core.h>

// User preset bank: patches live as .goapreset files (XML written by the
// processor's state serializer) in a per-user directory, so AI-generated
// patches and hand-tweaked sounds survive across sessions.
namespace userpresets
{

inline juce::File presetsDir()
{
    // Test override, same convention as the licence store (GOASYNTH_LICENSE_FILE
    // and friends). Without it a test that saves or renames a preset would write
    // straight into the real user's bank. Tests assert this is honoured before
    // touching anything.
    if (const char* over = std::getenv ("GOASYNTH_PRESET_DIR"))
        if (*over != 0)
        {
            const juce::File dir (juce::String::fromUTF8 (over));
            if (! dir.exists())
                dir.createDirectory();
            return dir;
        }

    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("GoaSynth").getChildFile ("Presets");
    if (! dir.exists())
        dir.createDirectory();
    return dir;
}

// Shared machine-wide bank (C:\Users\Public\Documents\GoaSynth): patches saved
// here are visible to every Windows account on this machine, so studios and
// shared machines can trade sounds without copying files by hand.
inline juce::File sharedPresetsDir()
{
    // Test override. The shared bank is machine-wide, so a test that writes here
    // and then crashes leaves junk patches for EVERY account on the machine -
    // including a studio's. Same convention as GOASYNTH_PRESET_DIR.
    if (const char* over = std::getenv ("GOASYNTH_SHARED_PRESET_DIR"))
        if (*over != 0)
        {
            const juce::File dir (juce::String::fromUTF8 (over));
            if (! dir.exists())
                dir.createDirectory();
            return dir;
        }

    auto dir = juce::File::getSpecialLocation (juce::File::commonDocumentsDirectory)
                   .getChildFile ("GoaSynth");
    if (! dir.exists())
        dir.createDirectory();
    return dir;
}

inline juce::Array<juce::File> scanDir (const juce::File& dir)
{
    return dir.findChildFiles (juce::File::findFiles, false, "*.goapreset");
}

// Full bank = the per-user presets plus any shared-bank presets whose file
// name is not already shadowed by one of the user's own.
inline juce::Array<juce::File> scanPresets()
{
    auto bank = scanDir (presetsDir());
    juce::StringArray have;
    for (const auto& f : bank)
        have.add (f.getFileName());

    for (const auto& f : scanDir (sharedPresetsDir()))
        if (! have.contains (f.getFileName(), true))
            bank.add (f);
    return bank;
}

// Turn an arbitrary display name into a safe file stem (no slashes, no
// traversal, non-empty, sane length).
// True if this file lives in the machine-wide shared bank rather than the
// per-user one. The browser badges shared presets so users can see which
// patches come from C:\Users\Public\Documents\GoaSynth.
inline bool isSharedPreset (const juce::File& f)
{
    return f.getParentDirectory() == sharedPresetsDir();
}

inline juce::String safeFileName (juce::String name)
{
    name = name.trim().replaceCharacters ("< > : \" / \\ | ? *",
                                          "_________________");
    while (name.contains ("__"))
        name = name.replace ("__", "_");
    name = name.trimCharactersAtEnd (". _");
    if (name.isEmpty())
        name = "Untitled";
    return name.substring (0, 64);
}

inline juce::File fileForName (const juce::String& name)
{
    return presetsDir().getChildFile (safeFileName (name) + ".goapreset");
}

inline bool savePresetTo (const juce::File& f, const juce::XmlElement& stateXml,
                          const juce::StringArray& tags = {})
{
    auto xml = juce::parseXML (stateXml.toString());
    if (xml == nullptr)
        return const_cast<juce::File&> (f).replaceWithText (stateXml.toString());

    // Sidecar metadata block: tags for the browser's tag filter. Stored in the
    // same file so bank sharing / backups carry the tags with the patches.
    //
    // Strip any existing block FIRST. This function is also used to rewrite a
    // preset that already has one (rename, retag), and blindly appending would
    // leave two PRESETINFO children - readTagsFromXml() takes the first, so the
    // new tags would be silently ignored and clearing tags impossible.
    for (int i = xml->getNumChildElements(); --i >= 0;)
        if (auto* child = xml->getChildElement (i))
            if (child->hasTagName ("PRESETINFO"))
                xml->removeChildElement (child, true);

    juce::StringArray clean;
    for (const auto& tag : tags)
        if (tag.trim().isNotEmpty())
            clean.add (tag.trim().toLowerCase());
    clean.removeDuplicates (true);
    if (! clean.isEmpty())
    {
        auto info = std::make_unique<juce::XmlElement> ("PRESETINFO");
        info->setAttribute ("tags", clean.joinIntoString (";"));
        xml->addChildElement (info.release());
    }

    return const_cast<juce::File&> (f).replaceWithText (xml->toString());
}

inline bool savePreset (const juce::String& name, const juce::XmlElement& stateXml,
                        const juce::StringArray& tags = {})
{
    return savePresetTo (fileForName (name), stateXml, tags);
}

inline bool saveSharedPreset (const juce::String& name, const juce::XmlElement& stateXml,
                              const juce::StringArray& tags = {})
{
    return savePresetTo (sharedPresetsDir().getChildFile (safeFileName (name) + ".goapreset"),
                         stateXml, tags);
}

// Tags of a preset, from parsed XML (PRESETINFO tags="a;b;c").
inline juce::StringArray readTagsFromXml (const juce::XmlElement& xml)
{
    juce::StringArray tags;
    if (auto* info = xml.getChildByName ("PRESETINFO"))
    {
        tags.addTokens (info->getStringAttribute ("tags"), ";", "");
        tags.removeEmptyStrings();
    }
    return tags;
}

inline juce::StringArray readTags (const juce::File& f)
{
    if (auto xml = juce::parseXML (f))
        return readTagsFromXml (*xml);
    return {};
}

inline juce::String displayName (const juce::File& f)
{
    return f.getFileNameWithoutExtension();
}

inline bool deletePreset (const juce::File& f)
{
    return f.deleteFile();
}

// ---- preset packs (.goapack = a plain zip of .goapreset files) -------------

constexpr const char* packExtension = "goapack";
constexpr const char* presetExtension = "goapreset";

inline juce::File withPackExtension (juce::File f)
{
    return f.hasFileExtension (packExtension) ? f : f.withFileExtension (packExtension);
}

// Write the given preset files into a .goapack. Returns false if nothing was
// written (empty selection, unreadable source, or unwritable destination).
inline bool exportPack (const juce::File& packFile, const juce::Array<juce::File>& presets)
{
    if (presets.isEmpty())
        return false;

    const auto target = withPackExtension (packFile);

    juce::ZipFile::Builder builder;
    for (const auto& f : presets)
        builder.addFile (f, 9, f.getFileName()); // flatten: store by name only

    juce::MemoryBlock mb;
    {
        juce::MemoryOutputStream out (mb, false);
        if (! builder.writeToStream (out, nullptr))
            return false;
    }
    // replaceWithData is atomic and truncating - FileOutputStream on an
    // existing pack would corrupt it.
    return target.replaceWithData (mb.getData(), mb.getSize());
}

struct PackImportResult
{
    int imported = 0, skipped = 0;
    juce::StringArray importedNames, skippedNames;
    juce::String error; // empty on success
};

// Import a .goapack into a bank directory. Entries are flattened to their base
// name (no folder traversal); anything that isn't a .goapreset is ignored.
// Existing files are skipped unless overwrite is set.
inline PackImportResult importPack (const juce::File& packFile, const juce::File& destDir,
                                    bool overwrite)
{
    PackImportResult r;
    if (! packFile.existsAsFile())
    {
        r.error = "Pack file not found.";
        return r;
    }

    juce::FileInputStream in (packFile);
    if (! in.openedOk())
    {
        r.error = "Could not open the pack file.";
        return r;
    }

    juce::ZipFile zip (in);
    if (zip.getNumEntries() <= 0)
    {
        r.error = "Empty or invalid pack file.";
        return r;
    }

    destDir.createDirectory();

    for (int i = 0; i < zip.getNumEntries(); ++i)
    {
        const auto* e = zip.getEntry (i);
        if (e == nullptr)
            continue;
        if (e->filename.endsWithChar ('/') || e->filename.endsWithChar ('\\'))
            continue; // directory entry
        if (! e->filename.trim().endsWithIgnoreCase (".goapreset"))
            continue; // ignore READMEs, __MACOSX cruft, ... but don't fail the pack

        // Flatten to base name so hostile paths (..\..\x.goapreset,
        // ../../x.goapreset) can't escape. Split on BOTH separators: JUCE's
        // File::getFileName() only handles the platform one on Windows.
        const auto flat = e->filename.trim().replaceCharacter ('\\', '/');
        const int lastSlash = flat.lastIndexOfChar ('/');
        auto name = lastSlash >= 0 ? flat.substring (lastSlash + 1) : flat;
        const int colon = name.lastIndexOfChar (':');          // "c:name" drives
        if (colon >= 0)
            name = name.substring (colon + 1);
        if (name.isEmpty() || name == "." || name == "..")
            continue;

        auto dest = destDir.getChildFile (name);
        if (dest.existsAsFile() && ! overwrite)
        {
            r.skippedNames.add (name);
            ++r.skipped;
            continue;
        }

        auto entry = zip.createStreamForEntry (i);
        if (entry == nullptr)
        {
            ++r.skipped;
            continue;
        }
        // replaceWithData truncates + atomically moves, so an existing file is
        // cleanly replaced even if the new content is shorter.
        juce::MemoryBlock mb;
        entry->readIntoMemoryBlock (mb);
        if (! dest.replaceWithData (mb.getData(), mb.getSize()))
        {
            ++r.skipped;
            continue;
        }
        r.importedNames.add (name);
        ++r.imported;
    }

    if (r.imported == 0 && r.skipped == 0 && r.error.isEmpty())
        r.error = "No .goapreset patches found in this pack.";
    return r;
}

// ---- browser state: favourites + last filter -------------------------------
// Kept deliberately OUT of the patch files. A "favourite" flag written into a
// .goapreset would mutate the user's own data, would not survive a re-save from
// an older build, and could never work at all for factory patches, which have
// no file behind them. One small JSON next to the bank instead.
struct BrowserState
{
    juce::StringArray favourites;   // stable keys, see favouriteKey()
    int folder = 0;                 // 0 ALL, 1 FACTORY, 2 USER, 3 FAVOURITES
    int sortMode = 0;               // 0 name A-Z, 1 name Z-A, 2 newest, 3 bank order
    juce::String tag, search;
};

inline juce::File browserStateFile()
{
    // Sibling of Presets/, so the preset folder stays purely patches.
    return presetsDir().getParentDirectory().getChildFile ("browser.json");
}

// Identity of a preset across sessions. User patches are identified by their
// file name (they have one); factory patches by name (they do not).
inline juce::String favouriteKey (bool isUser, const juce::String& name,
                                  const juce::File& f)
{
    return isUser ? "u:" + f.getFileName() : "f:" + name;
}

inline BrowserState loadBrowserState()
{
    BrowserState s;
    const auto f = browserStateFile();
    if (! f.existsAsFile())
        return s;

    const auto v = juce::JSON::parse (f.loadFileAsString());
    const auto* o = v.getDynamicObject();
    if (o == nullptr)
        return s;   // unreadable/corrupt: fall back to defaults, never throw

    if (const auto* arr = o->getProperty ("favourites").getArray())
        for (const auto& e : *arr)
            if (e.toString().isNotEmpty())
                s.favourites.addIfNotAlreadyThere (e.toString());

    s.folder   = juce::jlimit (0, 3, (int) o->getProperty ("folder"));
    s.sortMode = juce::jlimit (0, 3, (int) o->getProperty ("sort"));
    s.tag      = o->getProperty ("tag").toString();
    s.search   = o->getProperty ("search").toString();
    return s;
}

inline void saveBrowserState (const BrowserState& s)
{
    auto* o = new juce::DynamicObject();
    juce::Array<juce::var> favs;
    for (const auto& k : s.favourites)
        favs.add (k);
    o->setProperty ("favourites", favs);
    o->setProperty ("folder", s.folder);
    o->setProperty ("sort", s.sortMode);
    o->setProperty ("tag", s.tag);
    o->setProperty ("search", s.search);
    // Best effort: a failed write must never break the UI.
    browserStateFile().replaceWithText (juce::JSON::toString (juce::var (o)));
}

} // namespace userpresets
