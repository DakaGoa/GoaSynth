#pragma once

#include <array>
#include <functional>
#include <set>
#include "AiCloudGen.h"
#include "PluginProcessor.h"
#include "SynthEngine.h"
#include "Tuning.h"
#include "UserPresets.h"   // BrowserState (favourites + last filter)

// GoaSynth release version, as "major.minor.patch". GOASYNTH_VERSION comes
// from CMakeLists.txt (project()); tests that compile PluginEditor.cpp without
// it fall back to the same string CMake currently bakes in — keep the two in
// sync when bumping.
#ifndef GOASYNTH_VERSION
 #define GOASYNTH_VERSION "1.6.0"
#endif

// Version policy: the product line is 1.x until the owner says otherwise, so
// the major must stay 1. OverlayTest enforces both halves at build time.
// (CMake may pass its own GOASYNTH_VERSION_MAJOR; that one wins.)
#ifndef GOASYNTH_VERSION_MAJOR
 #define GOASYNTH_VERSION_MAJOR 1
#endif
static_assert (GOASYNTH_VERSION_MAJOR == 1,
               "v2 needs the owner's explicit go-ahead (see docs/version.json)");
static_assert (GOASYNTH_VERSION[0] == '1',
               "GOASYNTH_VERSION must stay on the 1.x line (see docs/version.json)");

// The running release version as a JUCE string (MENU header, About card,
// update comparison).
inline juce::String goaVersionString() { return juce::String (GOASYNTH_VERSION); }

// Compares GoaSynth versions as "major.minor" (the patch field is ignored):
// <0 when a is older than b, 0 when equal, >0 when newer. Non-numeric tokens
// count as 0, so a malformed feed can never outrank this build. In goaui so
// OverlayTest can drive it the same way the update check does.
namespace goaui
{
    inline int compareVersions (const juce::String& a, const juce::String& b)
    {
        auto majorMinor = [] (const juce::String& s)
        {
            const auto t = juce::StringArray::fromTokens (s, ".", {});
            return std::pair<int, int> { t.size() > 0 ? t[0].getIntValue() : 0,
                                         t.size() > 1 ? t[1].getIntValue() : 0 };
        };
        const auto A = majorMinor (a);
        const auto B = majorMinor (b);
        if (A.first  != B.first)  return A.first  < B.first  ? -1 : 1;
        if (A.second != B.second) return A.second < B.second ? -1 : 1;
        return 0;
    }

    // Pulls the "notes" array out of the parsed version.json feed — the
    // release notes the update-available dialog shows under "What's new".
    // Plain, non-empty strings only, capped: the wire is untrusted, and the
    // card has room for a handful of lines (the site has the full list). In
    // goaui so OverlayTest can drive it exactly as the update check does.
    inline juce::StringArray latestReleaseNotes (const juce::var& feed, int maxNotes = 4)
    {
        juce::StringArray out;
        if (const auto* arr = feed.getProperty ("notes", {}).getArray())
            for (const auto& n : *arr)
            {
                const juce::String line = n.toString().trim();
                if (line.isNotEmpty())
                    out.add (line);
                if (out.size() >= maxNotes)
                    break;
            }
        return out;
    }
}

namespace goaui
{

// ---- Themeable palette ------------------------------------------------------
// The palette lives in a mutable struct; switching a theme rewrites it and
// calls retintAll(). Paint paths read the live values, so panels/graphs recolour
// on the next repaint; components that capture colours at construction (knob
// attachments, labels) also update through their retint() hooks.

enum Theme : int
{
    themeUv = 0,     // UV Goa: violet / teal / magenta on purple-black
    themeSteel,      // studio steel: blue-greys, restrained amber accent
    themeAnalog,     // warm analog: cream face, walnut sides, orange/olive
    themeNeon,       // cyberpunk: cyan / hot magenta / neon yellow on black
    themePaper,      // light mode: warm off-white, cobalt / emerald / amber
    themeOled,       // pure black OLED with single red accent
    themeCount
};

struct Palette
{
    juce::Colour bgDark, bgPanel, bgPanelLo, bgHeader, bgInset, border;
    juce::Colour accent, accentA, accentB, textDim, textBright;
    // Component-specific shades.
    juce::Colour knobTop, knobBottom, knobRim, knobTrack;
    juce::Colour keyWhite, keyBlack, keyDim;
    juce::Colour bgTop, bgBottom;      // editor backdrop gradient
};

// Module header strip colours are a fixed ROLE per module (so a theme can
// re-map which colour each module gets instead of hardcoding accents).
enum ColRole
{
    roleAccent,      // primary accent (violet / slate / orange)
    roleA,           // secondary (teal / steel blue / olive)
    roleB,           // tertiary (magenta / amber / rust)
    roleNeutral,     // subdued text-ish
    roleCount
};

extern Palette pal;
extern Theme  activeTheme;

// The palette colours themselves are mutable namespace globals with the same
// names as before — every existing `goaui::bgPanel` reference keeps compiling
// and reads the CURRENT theme's value at the moment of use.
extern juce::Colour bgDark, bgPanel, bgPanelLo, bgHeader, bgInset, border,
                    accent, accentA, accentB, textDim, textBright,
                    knobTop, knobBottom, knobRim, knobTrack,
                    keyWhite, keyBlack, keyDim, bgTop, bgBottom;

void setTheme (Theme t, juce::Component* root);
Theme themeFromIndex (int i) noexcept;

// Which mod sources currently target `paramId`: returns the count (0..3),
// filling outSrc with modSourceName() indices and outAmt with the slot
// amounts. A slot counts when its source is set and its amount is non-zero;
// knobs paint one animated dot per active source. outSlot maps each dot back
// to its matrix row (for the tooltip text).
int modDotSources (juce::AudioProcessorValueTreeState& apvts,
                   const juce::String& paramId, int outSrc[3], float outAmt[3],
                   int outSlot[3] = nullptr);

// Hover text for one control's mod dots: "MOD: LFO 1 -> CUTOFF (+40)" per
// active slot (and the count when several share the knob). Returns false
// when the control has no active slots.
bool modDotTip (juce::AudioProcessorValueTreeState& apvts,
                const juce::String& paramId, juce::String& tip);

// Live value for a parameter in engine units ("1500 Hz", "375 ms", "85 %",
// "2.4 oct"); dropUnit=true yields the compact on-knob readout ("1.5k",
// "250m", "85"). Exposed for the OverlayTest unit-map sweep.
juce::String liveValueText (juce::AudioProcessorValueTreeState& apvts,
                            const juce::String& id, bool dropUnit = false);
inline juce::Colour roleColour (ColRole r) noexcept
{
    switch (r)
    {
        case roleAccent:    return accent;
        case roleA:         return accentA;
        case roleB:         return accentB;
        case roleNeutral:   return textDim;
        default:            return accent;
    }
}

// The three Goa accents as a horizontal gradient (roles map per theme).
inline juce::ColourGradient uvGradient (juce::Rectangle<float> r)
{
    juce::ColourGradient g (accent, r.getX(), r.getCentreY(),
                            accentA, r.getRight(), r.getCentreY(), false);
    g.addColour (0.5, accentB);
    return g;
}

// Brief fading outline shared by controls a MOD pick assignment can flash:
// knobs/toggles (destination picked) and the LFO/ENV graphs (source picked).
// triggerFlash() arms ~0.7 s; paintOverChildren/paint draws the glow scaled
// by flashFade(); the editor's 30 fps timer drives the fade repaints.
struct Flashable
{
    virtual ~Flashable() = default;

    void triggerFlash()                    { flashUntil = juce::Time::getMillisecondCounter() + 700; }
    float flashFade() const                // 1 -> 0 over the flash window
    {
        const juce::uint32 now = juce::Time::getMillisecondCounter();
        return now < flashUntil ? (float) (flashUntil - now) / 700.0f : 0.0f;
    }

    juce::uint32 flashUntil = 0;
};

// "GOA" logo in the theme gradient with a soft glow.
struct GoaLogo : juce::Label
{
    GoaLogo()
    {
        setText ("GOA", juce::dontSendNotification);
        setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
        setJustificationType (juce::Justification::centredRight);
    }

    // Breathing glow: 0..1 modulation driven by the editor timer.
    float breathe = 0.5f;

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        auto glow = b.expanded (6.0f);
        const float alpha = 0.18f + 0.14f * breathe;   // 0.18 .. 0.32
        juce::ColourGradient halo (accent.withAlpha (alpha), glow.getCentreX(),
                                   glow.getCentreY(), accent.withAlpha (0.0f),
                                   glow.getWidth() * 0.5f, glow.getHeight() * 0.5f, true);
        g.setGradientFill (halo);
        g.fillRoundedRectangle (glow, glow.getHeight());

        g.setFont (getFont());
        g.setGradientFill (uvGradient (b));
        g.drawText (getText(), b, getJustificationType());
    }
};

struct Ctl : juce::Component, public Flashable
{
    Ctl (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId,
         const juce::String& text, bool comboBox = false);

    void resized() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void retint();                       // re-apply theme colours to label/knob/combo
    void setTip (const juce::String&);   // dots' hover text -> child widgets

    // Parameter lock. While the editor's lock mode is armed the knob stops
    // intercepting mouse clicks (so a click cannot move it) and the editor
    // toggles this control's lock through onLockClick instead.
    void setLockMode (bool on);
    bool locked = false;
    bool lockMode = false;
    std::function<void()> onLockClick;

    juce::Label label;
    juce::Slider slider;
    juce::ComboBox combo;
    bool useCombo = false;
    // Vertical split of a knob cell: the name on the first line (a child
    // Label), the live readout on the second (painted by paint()), the rotary
    // below. Two lines rather than one shared band - see Ctl::resized().
    static constexpr int labelLine   = 11;
    static constexpr int readoutLine = 7;
    const juce::String paramId;          // for the MOD matrix pick-a-destination mode
    juce::String baseTip;                // parameter name; dots override while active
    juce::AudioProcessorValueTreeState* modSource;   // slot scan for the mod dots
    goa::GoaSynth* liveEngine = nullptr; // live source values for animated dots
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> cAtt;
};

// Mini hardware-switch skin for ToggleCtl: pill track, sliding thumb with a
// tick when engaged, and an ON/OFF caption beside the strip. Owned per-button
// (not on the shared GoaLAF) so ordinary checkboxes — e.g. the save dialog's
// SHARED option — keep the stock tickbox look.
struct SwitchLAF : juce::LookAndFeel_V4
{
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;
};

struct ToggleCtl : juce::Component, public Flashable
{
    ToggleCtl (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId,
               const juce::String& text);
    ~ToggleCtl() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void retint();
    void setTip (const juce::String&);   // dots' hover text -> child widgets

    juce::ToggleButton btn;
    SwitchLAF laf;                       // the strip skin (P.RAND etc.)
    juce::Label label;
    const juce::String paramId;          // for the MOD matrix pick-a-destination mode
    juce::String baseTip;                // parameter name; dots override while active
    juce::AudioProcessorValueTreeState* modSource;   // slot scan for the mod dots
    goa::GoaSynth* liveEngine = nullptr; // live source values for animated dots
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att;
};

// Framed module with a themed header strip. The header colour is a ROLE
// (accent / A / B / neutral), so each theme decides which colour a module gets.
struct Panel : juce::Component
{
    Panel (const juce::String& title, ColRole r);

    void paint (juce::Graphics&) override;
    void retint();

    juce::String title;
    ColRole role = roleAccent;
    juce::Colour headCol;
};

// Full-surface mod-matrix editor: 8 rows of SRC -> DST -> AMT, click the SRC
// and DST cells to cycle through the source/destination lists, drag the AMT
// cell to set the bipolar amount.
struct ModOverlay : juce::Component
{
    ModOverlay (GoaSynthAudioProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;
    void retint();
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    juce::Rectangle<int> cardBounds() const;

    // The pick-to-assign pointer line the confirm flash is drawing right now,
    // in overlay (== editor) coordinates: from the assigned cell toward the
    // clicked control, stopping just short of it. Empty when no flash is live.
    // Exposed for OverlayTest: the segment's geometry depends on where the
    // target control happens to sit, so a test cannot guess a region to scan
    // and must sample the line the flash actually drew.
    juce::Line<float> flashPointer() const noexcept;

    // Destination pick mode: click a DST cell to arm it, then click any knob
    // in the synth UI to assign that knob's parameter as the row's mod
    // destination. Source pick works the same via the SRC cell: click an LFO
    // scope or envelope graph to route it as the row's source. Re-click the
    // armed cell to cancel (or switch kinds by clicking the other cell);
    // right-click a cell still cycles its list.
    bool isPicking() const noexcept { return pickRow >= 0; }
    void armPick (int row);
    void armPickSource (int row);
    void cancelPick();
    // Resolves the assignable control under `pos` (overlay-local coordinates)
    // and sets it as the armed row's destination / source; false when the
    // click landed on nothing routable (the caller then cancels the pick).
    bool tryAssignDestination (juce::Point<float> pos, bool rightButton);
    bool tryAssignSource (juce::Point<float> pos, bool rightButton);
    bool keyPressed (const juce::KeyPress&) override;   // ESC cancels / closes

    // Painted-cell geometry, exposed for OverlayTest. The matrix is drawn by
    // paint() rather than built from child components, so the component-tree
    // overlap audit cannot see its cells at all (it walks 4 nodes: title, hint,
    // close and the overlay itself). Exposing rowGeo() lets the audit check the
    // real cell rectangles for overlap, which is the only way "nothing
    // overlaps" can be asserted for a custom-painted surface.
    struct RowGeo { juce::Rectangle<float> src, dst, amt, curve, lag; };
    juce::Rectangle<float> rowsRect() const;
    RowGeo rowGeo (int row) const;

private:
    int rowAt (juce::Point<float>) const;
    // Curve and lag are per physical slot (shared by both banks); src/dst/amt
    // resolve against the bank the card is currently editing.
    int slotRow (int i) const { return i; }
    bool editingBankB = false;
    void cycleAtCell (juce::Point<float> pos);
    void setDestIndex (int row, int index);   // write a destination id + repaint
    void setSlotIndex (int row, bool src, int index);   // generic cell write
    float amountFromClickX (juce::Rectangle<float> bar, float x) const;
    void updateCursor (juce::Point<float> pos);
    juce::Component* hitControlAt (juce::Point<float> pos);   // shared click resolve
    juce::Point<float> targetPointFor (juce::Component* target) const;   // flash pointer end

    GoaSynthAudioProcessor& proc;
    juce::Label head;
    // Interaction cheat-sheet, card footer. Two lines: the whole sheet measures
    // ~753px at 9pt and the card's inner width is 528px, so one line silently
    // ellipsized the tail and dropped the ESC hint (caught by OverlayTest).
    juce::Label hint;
    juce::Label hint2;
    juce::TextButton closeBtn { "X", "Close" };
    int dragRow = -1;
    float dragStartAmt = 0.0f;
    float dragStartY = 0.0f;
    int hoverRow = -1;
    int pickRow = -1;                    // cell awaiting a click assignment
    bool pickSrc = false;                // true = picking a SOURCE, else a DEST
    // Just-assigned cell flash (the knob flash is invisible under this
    // overlay's dim backdrop, so the confirmation draws in the card).
    int cellFlashRow = -1;
    bool cellFlashSrc = false;
    juce::uint32 cellFlashUntil = 0;
    // Where the clicked control sits (editor == overlay coordinates), so the
    // confirm flash can also draw a brief pointer line from the cell to it.
    juce::Point<float> cellFlashTarget;
    // The segment the last live flash painted (see flashPointer()).
    juce::Line<float> flashPointerLine;
};

// Full-surface AI patch designer: prompt field, GENERATE button, result line.
struct AiOverlay : juce::Component
{
    // brief, variation strength (0 SUBTLE, 1 NORMAL, 2 WILD), engine index
    // (0 LOCAL offline, 1 GEMINI, 2 OPENAI, 3 CUSTOM endpoint) and the
    // credential typed into the key editor.
    std::function<void (const juce::String&, int, int, int, const juce::String&)> onGenerate;

    AiOverlay();
    void paint (juce::Graphics&) override;
    void resized() override;
    void retint();
    void mouseDown (const juce::MouseEvent&) override;
    void visibilityChanged() override { if (isVisible()) prompt.grabKeyboardFocus(); }
    void showResult (const juce::String& title, const juce::String& desc);
    void showError (const juce::String& msg);
    void setBusy (bool b);
    void updateEngineUi();
    void cycleModel();
    void runTestKey();
    void loadCloudSettings();
    void saveCloudSettings();

    juce::Rectangle<int> cardBounds() const;
    juce::Label head;
    juce::TextEditor prompt;
    juce::TextButton goBtn { "GENERATE", "Describe the sound and generate a patch" };
    juce::Label varLabel;
    juce::TextButton varSubtle { "SUBTLE", "Re-rolls stay close to the family template" };
    juce::TextButton varNormal { "NORMAL", "Every generation is freely re-imagined" };
    juce::TextButton varWild   { "WILD",   "Re-rolls may redesign waves, octaves and FX" };
    int varIdx = 1;                       // selected variation strength
    // Cloud AI row: engine cycle button, credential editor, save button.
    juce::Label cloudLabel;
    juce::TextButton engineBtn { "ENGINE: LOCAL", "Cycle the AI engine: offline / Gemini / OpenAI / custom endpoint" };
    juce::TextEditor keyEditor;
    juce::TextButton keySaveBtn { "SAVE KEY", "Remember the API key on this computer" };
    juce::TextButton modelBtn { "MODEL", "Cycle the cloud model (newest first)" };
    juce::TextButton testBtn { "TEST KEY", "Send a small live request to verify the key and model" };
    juce::Label keyStatus;
    int cloudEngine = 0;                  // 0 LOCAL, 1 GEMINI, 2 OPENAI, 3 CUSTOM
    int modelIdx = 0;                     // index into the engine's model list
    bool busy = false;
    juce::TextButton closeBtn { "X", "Close" };
    juce::Label result;
};

// Full-surface activation screen: shown instead of the synth when the plugin
// is not licensed. Displays this machine's id (for the seller's keygen), a
// serial entry and an IMPORT button for buyer-ready .goalicense files; a
// valid serial (or license file) reveals the whole UI.
struct LicenseOverlay : juce::Component
{
    std::function<void()> onActivated;   // called after a successful activation

    LicenseOverlay();
    void paint (juce::Graphics&) override;
    void resized() override;
    void retint();
    void mouseDown (const juce::MouseEvent&) override {}

    juce::Rectangle<int> cardBounds() const;

    // Parses a .goalicense file and runs the normal activation path. The
    // outcome (or the file's note/issued lines on failure) is shown in the
    // overlay; a successful activation fires onActivated.
    void importLicenseFile (const juce::File& f);

    // "TRIAL EXPIRED \u2014 ACTIVATION REQUIRED" after the 24 h trial ran out,
    // plain "ACTIVATION REQUIRED" for never-activated instances.
    void setHeadline (bool trialExpired);

    juce::Label head;
    juce::Label machineLabel;
    juce::Label machineId;
    juce::TextButton copyBtn { "COPY", "Copy the machine id to the clipboard" };
    juce::TextEditor serial;
    juce::TextButton activateBtn { "ACTIVATE", "Activate with this serial" };
    juce::TextButton chooseFileBtn { "IMPORT", "Import a .goalicense license file" };
    // Peek mode (trial running, user wants to activate early): a small × in
    // the card corner closes the overlay; in forced mode (expired) there is
    // no way past the activation screen.
    bool dismissible = false;
    juce::TextButton closeBtn { "\u00d7", "Back to the synth" };    std::shared_ptr<juce::FileChooser> fileChooser;   // must outlive its async callback
    juce::Label fileNote;                             // note/issued lines from an imported file
    juce::Label error;
    juce::Label hint;
};

// Modal save dialog for user presets: name + tags + SAVE/CANCEL. Doubles as the
// rename and duplicate prompt (see beginRename / beginSaveAs) so those actions
// need no second dialog: it already has the name and tag fields they need, and
// JUCE modal loops are disabled in this plugin, so a new modal is not an option.
// Full-screen tinted card shown from the header's MENU → ABOUT: version,
// license state, machine ID and the product page.
struct AboutOverlay : juce::Component
{
    AboutOverlay();
    void paint (juce::Graphics&) override;
    void resized() override;
    void retint();

    // Clicking the darkened backdrop outside the card also dismisses the
    // overlay (the handler is assigned in the ctor).
    void mouseDown (const juce::MouseEvent&) override;

    // The centred 440x268 card that paint() and resized() both position
    // against; the close button is tested to sit inside it.
    juce::Rectangle<int> cardBounds() const noexcept;

    juce::TextButton closeBtn { "×", "Back to the synth" };

    juce::Label head, verLine, stateLine, idLabel, idValue, siteLine;
    juce::TextButton copyBtn { "COPY", "Copy the machine id to the clipboard" };

private:
    // Assigned (not constructed) in the ctor body so it can capture `this`.
    std::function<void (const juce::MouseEvent&)> mouseDownCallback;
};

// Result dialog for MENU → CHECK FOR UPDATES — an overlay on purpose: an OS
// message box cannot host a clickable link, and the two outcomes that name
// the download URL are exactly the ones where clicking it is the point. Same
// furniture as every other overlay (tinted backdrop, centred card, × on the
// card, backdrop-click dismissal), and as the editor's own child the host has
// no window to bury it behind.
struct UpdateResultOverlay : juce::Component
{
    UpdateResultOverlay();

    // Title + message + up to two link rows: linkUrl is the download/site
    // address (empty hides the row — "up to date" has nothing to download),
    // changelogUrl is the "View full change log" deep link (empty hides it —
    // the unreachable branch offers no second door into a site the check
    // just failed to reach). linkLabel replaces the download row's text when
    // the address itself is too long to show (the update branch's GitHub
    // installer URL): the row then names the file, and the address stays the
    // link's target and tooltip. The editor positions and shows the overlay
    // afterwards, like every other overlay's open path.
    void configure (const juce::String& title, const juce::String& message,
                    const juce::String& linkUrl,
                    const juce::String& changelogUrl = {},
                    const juce::String& linkLabel = {});

    void paint (juce::Graphics&) override;
    void resized() override;
    void retint();

    // Clicking the darkened backdrop outside the card also dismisses it
    // (the handler is assigned in the ctor).
    void mouseDown (const juce::MouseEvent&) override;

    // The centred 440x300 card that paint() and resized() both position
    // against; every row is tested to sit inside it.
    juce::Rectangle<int> cardBounds() const noexcept;

    juce::Label titleLabel, messageLabel;
    // juce's own control: underlined, hand cursor, click → default browser.
    juce::HyperlinkButton link;
    // Second link row: opens the release notes on the site. Update-available
    // deep-links to the new version's own heading (#vX.Y.Z, written by
    // tools/make-changelog.py); up-to-date opens the whats-new section.
    juce::HyperlinkButton changelogLink;
    juce::TextButton okBtn { "OK", "Dismiss the result dialog" };
    juce::TextButton closeBtn { "×", "Back to the synth" };

private:
    // Assigned (not constructed) in the ctor body so it can capture `this`.
    std::function<void (const juce::MouseEvent&)> mouseDownCallback;
};

struct SavePresetOverlay : juce::Component
{
    std::function<void (const juce::String&, const juce::StringArray&, bool)> onSave;
    std::function<void (const juce::String&, const juce::StringArray&)> onRename;

    SavePresetOverlay();
    void paint (juce::Graphics&) override;
    void resized() override;
    void retint();
    void mouseDown (const juce::MouseEvent&) override;
    void visibilityChanged() override { if (isVisible()) name.grabKeyboardFocus(); }
    void prefill (const juce::String& s) { name.setText (s, juce::dontSendNotification); name.selectAll(); }
    juce::StringArray collectTags() const;

    // Normal "save a new preset" flow, pre-filled (used by Duplicate).
    void beginSaveAs (const juce::String& presetName, const juce::StringArray& presetTags);
    // Rewrite the currently selected user patch under a new name.
    void beginRename (const juce::String& presetName, const juce::StringArray& presetTags);
    bool isRenaming() const noexcept { return renaming; }

    juce::Rectangle<int> cardBounds() const;
    juce::Label head;
    juce::TextEditor name;
    juce::TextEditor tags;   // space-separated tags, e.g. "acid bass night"
    juce::ToggleButton sharedBtn;  // on = write to the machine-wide shared bank
    juce::TextButton saveBtn { "SAVE", "Save preset" };
    juce::TextButton cancelBtn { "CANCEL", "Cancel" };

private:
    bool renaming = false;
};

// Filled / hollow star, built from a code point rather than a "\u2605" literal:
// MSVC compiles these sources as code page 1252, which cannot represent U+2605,
// so a universal-character-name warns (C4566) and encodes wrongly. U+00D7 (the
// overlay close buttons) happens to exist in 1252, which is why that one is fine.
inline juce::String starGlyph (bool filled)
{
    return juce::String::charToString ((juce::juce_wchar) (filled ? 0x2605 : 0x2606));
}

// Serum-style preset browser overlay: folder tabs (ALL / FACTORY / USER / star),
// search, tag filter, sort, and a grouped click-to-load list. The editor owns
// the filtering and all preset policy; the overlay displays rows and reports
// gestures.
//
// Audition model: a plain click loads the patch and LEAVES THE BROWSER OPEN, so
// patches can be flipped through without reopening. Double-click or Return takes
// one and closes. Closing never rolls back - whatever is loaded stays loaded,
// which is what every other synth browser does and avoids the surprise of losing
// a patch you just chose.
struct PresetBrowserOverlay : juce::Component, juce::ListBoxModel
{
    struct Row
    {
        juce::String label;
        juce::StringArray tags;   // rendered with +N overflow by paintListBoxItem
        bool header = false;
        bool isUser = false;
        bool shared = false;      // from the machine-wide shared bank
        bool fav = false;         // starred
        bool selected = false;    // the preset currently loaded (not the list cursor)
        int  visibleIndex = -1;   // index into the editor's visiblePresets
    };

    std::function<void (int)> onSelect;     // audition: load, keep the browser open
    std::function<void (int)> onAccept;     // take it: load and close
    std::function<void ()> onChanged;       // search / tag / folder / sort changed
    std::function<void ()> onExport;        // EXPORT PACK clicked
    std::function<void ()> onImport;        // IMPORT PACK clicked
    std::function<void (int)> onFavourite;  // star clicked on a row
    std::function<void (int)> onTagClick;   // tag column clicked on a row
    // Right-click on a row. The editor owns preset policy (rename / delete /
    // reveal), so it builds the menu; the overlay only reports the gesture.
    std::function<void (int, juce::Point<int>)> onRowMenu;

    PresetBrowserOverlay();
    void paint (juce::Graphics&) override;
    void resized() override;
    void retint();
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

    juce::Rectangle<int> cardBounds() const;
    void setRows (const std::vector<Row>& r, int selectedRow);
    void setStatus (const juce::String& text, bool ok);
    void setCount (int shown, int total);
    void setFolder (int f);            // used by the tabs: fires onChanged
    void setSort (int mode);
    void setSearchText (const juce::String& s);
    void setTagText (const juce::String& s);
    // Restore a persisted filter quietly; the caller rebuilds once afterwards.
    void applyState (int folder, int sort, const juce::String& tag,
                     const juce::String& searchText);
    int folder() const noexcept { return folderIdx; }   // 0 ALL 1 FACTORY 2 USER 3 FAVS
    int sortMode() const noexcept { return sortIdx; }
    void focusSearch();

    // juce::ListBoxModel
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool rowSelected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed (int row) override;
    void selectedRowsChanged (int row) override;
    juce::String getTooltipForRow (int row) override;

    juce::Label head;
    juce::TextButton closeBtn { "X", "Close" };
    juce::TextEditor search;
    juce::ComboBox tagCombo;
    juce::ComboBox sortCombo;
    juce::TextButton allTab { "ALL", "All presets" };
    juce::TextButton factoryTab { "FACTORY", "Factory presets" };
    juce::TextButton userTab { "USER", "User presets" };
    juce::TextButton favTab { starGlyph (true), "Favourite presets" };
    juce::TextButton exportBtn { "EXPORT PACK", "Save all user presets as a .goapack file" };
    juce::TextButton importBtn { "IMPORT PACK", "Add presets from a .goapack file" };
    juce::ListBox list;
    juce::Label status;   // pack import/export messages
    juce::Label count;    // "18 of 247 presets", or the empty-state hint

private:
    void syncTabs();
    void acceptRow (int row);    // load + close
    void previewRow (int row);   // load, keep open
    // Width of the right-hand tag column. Shared by the painter and the click
    // hit-test so "click a tag to filter" cannot drift from where tags are drawn.
    static int tagColumnWidth() noexcept { return 118; }
    std::vector<Row> rows;
    int selectedRow = -1;
    int folderIdx = 0;
    int sortIdx = 0;
    // setRows() runs updateContent(), which moves the ListBox selection and would
    // otherwise be read as the user arrowing onto a row - and load a preset.
    bool internalUpdate = false;
};

// Wavetable editor for OSC A / OSC B (User mode): sketch individual frames,
// see the morph result at the live WT position, pick frames from the strip.
// Left-drag draws, right-drag erases, double-click resets the table,
// click the bottom strip to select the frame being edited.
struct WaveDisplay : juce::Component, private juce::Timer, public juce::SettableTooltipClient
{
    WaveDisplay (GoaSynthAudioProcessor& proc, int oscIndex,
                 const juce::String& waveParamId, const juce::String& wtPosParamId,
                 ColRole r);
    ~WaveDisplay() override { stopTimer(); }

    void paint (juce::Graphics&) override;
    void timerCallback() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    bool userMode() const;

    // Invoked by the 8th tool ("WAV"): asks the editor to open a file chooser
    // and slice the chosen file into this oscillator's frames.
    std::function<void()> onLoadWav;

    void retint() { col = roleColour (role); repaint(); }

private:
    enum Zone { zCurve, zStrip, zTools };
    Zone zoneAt (juce::Point<float>) const;
    int frameAt (juce::Point<float>) const;
    juce::Rectangle<float> stripRect() const;
    juce::Rectangle<float> curveRect() const;
    juce::Rectangle<float> toolsRect() const;
    int toolAt (juce::Point<float>) const;
    void applyStroke (juce::Point<float> pos, bool erase);
    void applyTool (int tool);
    void beginSession();
    void endSession();

    GoaSynthAudioProcessor& proc;
    int oscIndex = 0;
    int selectedFrame = 0;
    ColRole role = roleA;
    juce::Colour col;
    std::atomic<float>* waveValue = nullptr;
    std::atomic<float>* posValue = nullptr;
    int lastWave = -1;
    float lastPos = -1.0f;
    bool dragging = false;
    bool editing = false;
    int hoverTool = -1;   // hover highlight for the tool buttons
};

// Draws a stylised filter response curve from cutoff/reso/type.
struct FilterGraph : juce::Component, private juce::Timer, public juce::SettableTooltipClient
{
    FilterGraph (juce::AudioProcessorValueTreeState& apvts, ColRole r);
    ~FilterGraph() override { stopTimer(); }

    void paint (juce::Graphics&) override;
    void timerCallback() override;

    ColRole role = roleAccent;
    juce::Colour col;
    void retint() { col = roleColour (role); repaint(); }
    std::atomic<float>* cut = nullptr;
    std::atomic<float>* reso = nullptr;
    std::atomic<float>* type = nullptr;
    float lastCut = -1.0f, lastReso = -1.0f;
    int lastType = -1;
};

// Draggable ADSR graph.
struct EnvGraph : juce::Component, private juce::Timer, public Flashable,
                   public juce::SettableTooltipClient
{
    EnvGraph (juce::AudioProcessorValueTreeState& apvts,
              const juce::String& idA, const juce::String& idD,
              const juce::String& idS, const juce::String& idR, ColRole r);
    ~EnvGraph() override { stopTimer(); }

    void paint (juce::Graphics&) override;
    void timerCallback() override { repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    juce::Colour colour() const noexcept { return roleColour (role); }

private:
    struct Geo { float wA = 0, wD = 0, wS = 0, wR = 0, susY = 0, hTop = 0, hBot = 0, w = 0; };
    Geo geo() const;
    float norm (juce::RangedAudioParameter* p) const;
    void setNorm (juce::RangedAudioParameter* p, float v) const;

    juce::RangedAudioParameter* pA = nullptr;
    juce::RangedAudioParameter* pD = nullptr;
    juce::RangedAudioParameter* pS = nullptr;
    juce::RangedAudioParameter* pR = nullptr;
    ColRole role = roleA;
    juce::Colour col;
    enum Drag { none, atk, dec, sus, rel } dragging = none;
    float startNorm = 0.0f;

public:
    void retint() { col = roleColour (role); repaint(); }   // theme hook

    // Which envelope this graph edits (its ATTACK parameter id): lets the
    // MOD matrix's source-pick mode tell ENV A from ENV F.
    juce::String attackParamId() const
    {
        return pA != nullptr ? pA->getParameterID() : juce::String();
    }
};

// Animated LFO shape display.
struct LfoGraph : juce::Component, private juce::Timer, public Flashable,
                   public juce::SettableTooltipClient
{
    LfoGraph (juce::AudioProcessorValueTreeState& apvts,
              const juce::String& rateId, const juce::String& waveId, ColRole r);
    ~LfoGraph() override { stopTimer(); }

    void paint (juce::Graphics&) override;
    void timerCallback() override;

    float shapeAt (int wave, float p) const;

    ColRole role = roleAccent;
    juce::Colour col;
    void retint() { col = roleColour (role); repaint(); }
    std::atomic<float>* rate = nullptr;
    juce::String rateParamId;            // which LFO: for MOD matrix source-pick
    std::atomic<float>* wave = nullptr;
};

// 16-step sequencer strip: click/drag toggles steps, shows the playhead,
// right-click cycles step-specific settings (gate: shift the pattern;
// arp: per-step semitone) and the SYNC/OCT cells cycle their params.
// The arp strip's lower band edits per-step velocity: drag vertically, or
// right-click a step to toggle full-velocity accent. The gate strip's COPY
// cell mirrors the arp strip's active/rest pattern onto the gate steps; the
// arp strip's DIR cell cycles the playback direction.
struct StepStrip : juce::Component, private juce::Timer, public juce::SettableTooltipClient
{
    enum Kind { gateStrip, arpStrip };

    StepStrip (GoaSynthAudioProcessor& p, Kind k, ColRole r);
    ~StepStrip() override { stopTimer(); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> cellRect (int i) const;
    juce::Rectangle<float> velRect (int i) const;
    juce::Rectangle<float> gateRect (int i) const;   // arp strip: gate-length band
    juce::Rectangle<float> syncCell() const;
    juce::Rectangle<float> octCell() const;
    juce::Rectangle<float> copyCell() const;   // gate strip: ARP → GATE copy
    juce::Rectangle<float> patCell() const;
    juce::Rectangle<float> shapeCell() const;  // gate strip: edge shape picker
    juce::Rectangle<float> dirCell() const;    // arp strip: playback direction
    juce::Rectangle<float> scaleCell() const;  // arp strip: scale quantizer
    juce::Rectangle<float> fillCell() const;   // arp strip: in-key generative fill
    void fillInKey();                          // write in-scale steps into rests
    bool patternStepsChanged();                // arp strip: step edits need repaint
    int stepAt (juce::Point<float> pos) const;
    void poke (juce::Point<float> pos, bool erase);
    void setVelocity (int step, float v01);
    void setGate (int step, float frac);   // arp strip: per-step gate length
    void applyPattern (int idx);
    void copyFromArp();
    void cycleAt (juce::Point<float> pos, bool fine);

    GoaSynthAudioProcessor& proc;
    Kind kind;
    ColRole role = roleAccent;
    juce::Colour col;
    juce::RangedAudioParameter* syncPar = nullptr;
    juce::RangedAudioParameter* octPar = nullptr;
    juce::RangedAudioParameter* dirPar = nullptr;   // arp strip: playback direction
    juce::RangedAudioParameter* shapePar = nullptr; // gate strip: edge shape
    juce::RangedAudioParameter* scalePar = nullptr; // arp strip: scale quantizer
    juce::RangedAudioParameter* rootPar  = nullptr; // arp strip: scale root
    int lastScaleIdx = -1;                          // repaint when the scale changes
    int lastRootIdx  = -1;                          // repaint when the root changes
    std::array<juce::uint8, 16> lastSteps {};       // step-value cache for repaints
    std::array<juce::RangedAudioParameter*, 16> steps {};
    std::array<juce::RangedAudioParameter*, 16> vels {};
    std::array<juce::RangedAudioParameter*, 16> gates {};
    bool gateDragging = false;  // gate-length drag in progress
    float dragStartGate = 0.75f;
    bool dragToggle = false;
    bool velDragging = false;   // velocity drag in progress
    float dragStartVel = 0.0f;  // velocity at drag start (relative dragging)
    float dragStartY = 0.0f;
    int lastCell = -1;
    int lastLive = -1;
    int hoverStep = -1;
    int patternIdx = 0;         // selected trancegate pattern (0 = MANUAL)
    float lastPh = -1.0f;

    void timerCallback() override;   // poll gate playhead for the step outline

public:
    void retint() { col = roleColour (role); repaint(); }   // theme hook
};

// Stereo output level + limiter gain-reduction readout. Polls the two atomics
// the audio thread publishes (uiPeakL / uiPeakR) on a 30 Hz timer; it
// never touches audio state directly, and it repaints only when a value moved
// far enough to matter.
struct LevelMeter : juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
    explicit LevelMeter (GoaSynthAudioProcessor& p)
        : proc (p)
    {
        setTooltip ("Output level, L over R (white ticks hold recent peaks);\n"
                    "the backdrop pulse follows the same signal");
        startTimerHz (30);
    }
    ~LevelMeter() override { stopTimer(); }

    void paint (juce::Graphics&) override;
    void timerCallback() override;
    void retint() { repaint(); }        // theme hook

    GoaSynthAudioProcessor& proc;
    // Smoothed per-channel display level 0..1, plus DAW-style peak-hold ticks
    // (hold ~1.2 s, then fall at ~12 dB/s).
    float levelL = 0.0f, levelR = 0.0f;
    float peakHoldL = 0.0f, peakHoldR = 0.0f;
    int   peakHoldAgeL = 999, peakHoldAgeR = 999;   // frames since the last new peak
};

// Minimal clickable piano keyboard feeding the synth directly.
struct Keyboard : juce::Component, public juce::SettableTooltipClient
{
    explicit Keyboard (GoaSynthAudioProcessor& p) : proc (p) {}

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override { press (hitTest (e.position)); }
    void mouseDrag (const juce::MouseEvent& e) override { press (hitTest (e.position)); }
    void mouseUp (const juce::MouseEvent&) override { release(); }

    void setOctave (int oct);
    void release();

    GoaSynthAudioProcessor& proc;
    int startNote = 48;
    int held = -1;
    int octave = 3;
    static constexpr int numWhite = 25;

    static int whiteSemi (int i);
    int noteForWhite (int i) const;
    bool blackAfter (int i) const;
    int hitTest (juce::Point<float> pos) const;
    void press (int note);
};

struct GoaLAF : juce::LookAndFeel_V4
{
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox& box) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button& button, const juce::Colour&,
                               bool highlighted, bool down) override;
};

} // namespace goaui

class GoaSynthAudioProcessorEditor : public juce::AudioProcessorEditor,
                                     private juce::Timer,
                                     private juce::FileDragAndDropTarget
{
public:
    explicit GoaSynthAudioProcessorEditor (GoaSynthAudioProcessor& processor);
    ~GoaSynthAudioProcessorEditor() override;

    // MENU → CHECK FOR UPDATES: one shot at a time; the fetch thread hands
    // its result to the message thread via updateCheckDone(). Public because
    // the fetch thread's SafePointer callback calls updateCheckDone().
    void runUpdateCheck();
    void updateCheckDone();
    // The one place every update outcome appears. Public so the tests can
    // drive all three branches without a fetch, a feed, or a DAW.
    void showUpdateResult (const juce::String& title, const juce::String& message,
                           const juce::String& linkUrl,    // empty = no download row
                           const juce::String& changelogUrl = {},   // empty = no change-log row
                           const juce::String& linkLabel = {});     // row text if the URL is too long to show
    std::unique_ptr<juce::Thread> updateThread;   // owned; joined in the destructor
    juce::String updateThreadVersion;             // latest version the thread read
    juce::StringArray updateNotes;                // "What's new" lines from the feed
    juce::String updateThreadDate;                // release date of that version, display-ready
    juce::String updateThreadSize;                // installer size from the feed, display-ready
    std::atomic<bool> updateOlder { false };      // released < current
    std::atomic<bool> updateReachable { false };  // feed fetched and parsed

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    // Psychedelic swirl backdrop: pre-rendered half-res spiral layers composited
    // with rotation + audio-reactive scale pulse. Timer repaints at ~30 fps;
    // paint() draws the cached image under the panels.
    void rebuildBackdrop();
    void applyTheme();
    void timerCallback() override;

    // 24 h trial readout in the header; cheap, safe to call every timer tick.
    // A click peeks at the activation screen (activate before expiry).
    void updateTrialBadge();
    void showLicenseOverlay (bool allowClose);
    juce::TextButton trialBadge;

    // Double-click license import: Windows opens .goalicense files with
    // GoaSynth and hands the path over as a file drop. Suppressed while any
    // overlay is up (except the activation screen itself).
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    bool anyOverlayUp() const;           // a modal overlay covers the synth
    juce::TooltipWindow tooltipWindow { this, 350 };   // makes setTooltip() real
    juce::Image swirl1, swirl2;          // the two spiral layers
    juce::Image backdrop;                // cached composite
    double backdropSize = 0.0;           // size the cache was built for
    double animClock = 0.0;              // seconds, advanced by the Timer

    // One browsable preset, either factory or user.
    struct PresetEntry
    {
        juce::String name;
        juce::StringArray tags;
        bool isUser = false;
        bool shared = false;                 // lives in the shared bank
        int  factoryIndex = -1;              // into getPresets(), user: -1
        juce::File file;                     // user only
        juce::int64 modified = 0;            // file mtime; 0 for factory
        int  bankOrder = 0;                  // stable position as built
    };

    void buildContent (juce::AudioProcessorValueTreeState& apvts);
    void applyPreset (int index);
    void cyclePreset (int direction);
    void applyAiPatch (const juce::String& brief, int variation,
                       int engine, const juce::String& credential, int modelIdx = -1);
    void applyAiPatchResult (const AiCloudGen::Response& res);
    void refreshUserPresets();
    void rebuildPresetList();
    void refreshLicenseUi();   // toggle the activation screen vs the synth UI
    bool asyncFileChooser (const juce::String& title, const juce::String& wildcards,
                           int browserFlags,
                           std::function<void (const juce::File&)> onChosen);
    void exportPresetPack();
    void importPresetPack();
    // Slices a .wav into the 8 wavetable frames of one oscillator (resampled to
    // the table's 256 points per frame) and switches that oscillator to its
    // User wave. Also reachable by dropping a .wav onto an OSC panel.
    void loadWavIntoWavetable (int oscIndex);
    // The actual slice + publish, shared by the WAV tool and a file drop.
    void importWavFile (const juce::File& f, int oscIndex);
    void reportPackStatus (const juce::String& text, bool ok);
    // Source file of the rename currently in the save overlay (rename reuses
    // that dialog rather than adding a second one).
    juce::File renameSource;
    void saveUserPreset (const juce::String& name, const juce::StringArray& tags,
                         bool shared);
    void updatePresetTint();
    void updateArrows();

    // Browser state: favourites + the last folder/tag/search/sort, persisted
    // outside the patch files (see userpresets::BrowserState).
    void loadBrowserState();
    void persistBrowserState();
    bool isFavourite (const PresetEntry& e) const;
    void toggleFavourite (int visIdx);
    void showRowMenu (int visIdx, juce::Point<int> screenPos);
    void renameUserPreset (const juce::File& from, const juce::String& newName,
                           const juce::StringArray& tags);
    void exportSinglePreset (int visIdx);

    // ---- patch history, A/B compare, randomise, parameter lock -------------
    // A snapshot is every parameter's NORMALISED value — the exact currency
    // setValueNotifyingHost() takes — keyed by id, so restoring is lossless and
    // needs no per-parameter special cases.
    using Snapshot = std::vector<std::pair<juce::String, float>>;
    Snapshot capturePatch() const;
    void applySnapshot (const Snapshot& s);
    void pushUndo();                     // call before any patch-level change
    void undo();
    void redo();
    void updateHistoryButtons();
    void randomisePatch();
    void swapAb();
    void setLockMode (bool on);
    bool isLocked (const juce::String& paramId) const
        { return lockedParams.find (paramId) != lockedParams.end(); }
    // True for "gate7" / "arp12" / "arpVel3": a step-sequencer cell, which
    // randomise must leave alone (it would scramble the pattern, not the patch).
    static bool isStepParamId (const juce::String& id, const juce::String& prefix);

    std::vector<Snapshot> undoStack, redoStack;
    static constexpr size_t maxUndo = 32;
    Snapshot abA, abB;
    int  abSlot = 0;                     // 0 = A, 1 = B
    bool abValid = false;
    std::set<juce::String> lockedParams;
    bool lockMode = false;
    std::vector<goaui::Ctl*> allCtls;    // every knob, for lock mode + undo hooks

    std::vector<PresetEntry> allPresets;      // Init + factory + user, always full
    std::vector<PresetEntry> visiblePresets;  // filtered view the browser shows
    int selectedPreset = -1;                  // index into visiblePresets
    userpresets::BrowserState browserState;   // favourites + last filter, persisted
    GoaSynthAudioProcessor& proc;
    goaui::GoaLAF laf;

    // header
    juce::Label logoSynth, tagline;
    goaui::GoaLogo logoGoa;
    juce::Label presetName;
    juce::TextButton browseBtn { "BROWSE", "Open the preset browser" };
    juce::TextButton prevBtn { "<", "Previous preset" }, nextBtn { ">", "Next preset" };
    juce::TextButton panicBtn { "PANIC", "All notes off" };
    juce::TextButton aiBtn { "AI", "AI patch designer" };
    juce::TextButton themeBtn { "THEME", "Pick a theme: UV Goa / steel / warm analog / neon / paper / OLED" };
    juce::TextButton menuBtn { "MENU", "About GoaSynth, check the web for updates" };
    juce::TextButton saveBtn { "SAVE", "Save user preset" };
    // Output level + limiter gain reduction, in the header gap between the
    // preset zone and the button block. Declared after `proc` (above) so the
    // reference it stores is already bound when it is constructed.
    goaui::LevelMeter levelMeter { proc };
    std::unique_ptr<goaui::Ctl> masterCtl;
    std::unique_ptr<goaui::AiOverlay> aiOverlay;
    std::unique_ptr<goaui::SavePresetOverlay> saveOverlay;
    std::unique_ptr<goaui::PresetBrowserOverlay> presetBrowser;
    std::unique_ptr<goaui::ModOverlay> modOverlay;
    std::unique_ptr<goaui::AboutOverlay> aboutOverlay;
    std::unique_ptr<goaui::UpdateResultOverlay> updateResultOverlay;
    juce::TextButton modBtn { "MOD", "Open the modulation matrix (8 routable slots)" };
    std::unique_ptr<goaui::LicenseOverlay> licenseOverlay;
    std::shared_ptr<juce::FileChooser> packChooser;   // one dialog at a time

    // OSC row
    goaui::Panel oscAFrame { "OSC A", goaui::roleA };
    goaui::Panel oscBFrame { "OSC B", goaui::roleB };
    goaui::Panel filtFrame { "FILTER", goaui::roleAccent };
    goaui::Panel filt2Frame { "FILTER B", goaui::roleB };
    goaui::Panel subFrame  { "SUB", goaui::roleNeutral };
    // NOISE was a full panel with one knob; now the knob sits bare in the row.
    goaui::WaveDisplay waveA, waveB;
    goaui::FilterGraph filtCurve;
    std::unique_ptr<goaui::Ctl> wave1Ctl, oct1Ctl, fin1Ctl, uni1Ctl, det1Ctl, wid1Ctl,
        pan1Ctl, wt1Ctl, lvl1Ctl, ph1Ctl, wave2Ctl, oct2Ctl, fin2Ctl, uni2Ctl, det2Ctl, wid2Ctl,
        pan2Ctl, wt2Ctl, lvl2Ctl, ph2Ctl, fmCtl, subWaveCtl, subOctCtl, subCtl, noiseCtl,
        noiseTypeCtl,                           // WHITE / PINK
        pwCtl, ringCtl,                          // PWM duty + ring mod
        ftypeCtl, cutoffCtl, resoCtl, driveCtl, keyCtl,
        ftype2Ctl, cutoff2Ctl, reso2Ctl, routeCtl,
        vMorphCtl, vResCtl, vMixCtl,             // vowel/formant filter
        fdCtl, fbCtl;                            // filter drive + feedback
    std::unique_ptr<goaui::ToggleCtl> prand1Ctl, prand2Ctl, vOnCtl, hqCtl, syncCtl;

    // ENV / LFO row
    goaui::Panel ampEnvFrame { "ENV A (AMP)", goaui::roleA };
    goaui::Panel filEnvFrame { "ENV F (FILTER)", goaui::roleAccent };
    goaui::Panel lfo1Frame { "LFO 1", goaui::roleAccent };
    goaui::Panel lfo2Frame { "LFO 2", goaui::roleB };
    goaui::EnvGraph ampGraph, filGraph;
    goaui::LfoGraph lfo1Graph, lfo2Graph;
    std::unique_ptr<goaui::Ctl> aACtl, aDCtl, aSCtl, aRCtl, fACtl, fDCtl, fSCtl, fRCtl,
        envAmtCtl, modDepthCtl,
        l1RateCtl, l1WaveCtl, l1TgtCtl, l1DepthCtl, l1UnitCtl, l1DivCtl,
        l2RateCtl, l2WaveCtl, l2TgtCtl, l2DepthCtl, l2UnitCtl, l2DivCtl;

    // FX row
    goaui::Panel chorusFrame { "CHORUS", goaui::roleAccent };
    goaui::Panel phaserFrame { "PHASER", goaui::roleA };
    goaui::Panel delayFrame  { "DELAY",  goaui::roleB };
    goaui::Panel reverbFrame { "REVERB", goaui::roleNeutral };
    goaui::Panel moveFrame   { "MOVEMENT", goaui::roleA };
    goaui::Panel macroFrame  { "MACRO", goaui::roleAccent };
    goaui::Panel ottFrame    { "OTT",    goaui::roleAccent };
    std::unique_ptr<goaui::Ctl> chRateCtl, chDepthCtl, chMixCtl,
        phRateCtl, phDepthCtl, phMixCtl,
        dSyncCtl, dTimeCtl, dFbCtl, dMixCtl,
        rSizeCtl, rDampCtl, rMixCtl,
        oDepthCtl, oLowCtl, oMidCtl, oHighCtl, oOutCtl,
        eqLowCtl, eqMidCtl, eqMidFreqCtl, eqHighCtl,
        driftCtl, uniDetCtl, uniSpreadCtl,
        charCtl, chordCtl, shimCtl, duckCtl;

    // Performance macros: plain 0..1 knobs meant as matrix SOURCES (also
    // MIDI CC 14 / CC 15 out of the box).
    std::unique_ptr<goaui::Ctl> macroACtl, macroBCtl;

    // bottom bar
    juce::TextButton octDown { "OCT-", "Octave down" }, octUp { "OCT+", "Octave up" };
    // Patch-workflow buttons, in the bottom bar beside the octave keys: there
    // is no room left in the header, and these are performance-time controls.
    juce::TextButton randBtn { "RAND", "Randomise the patch (locked knobs are kept)" };
    juce::TextButton undoBtn { "UNDO", "Undo the last preset load / randomise / A-B swap" };
    juce::TextButton redoBtn { "REDO", "Redo" };
    juce::TextButton abBtn   { "A", "A/B compare: keep two versions of the patch and flip between them" };
    juce::TextButton lockBtn { "LOCK", "Lock mode: click knobs to protect them from randomise / preset loads" };
    goaui::StepStrip gateStrip { proc, goaui::StepStrip::gateStrip, goaui::roleAccent };
    goaui::StepStrip arpStrip  { proc, goaui::StepStrip::arpStrip,  goaui::roleA };
    goaui::Keyboard keyboard;
    std::unique_ptr<goaui::Ctl> voicingCtl, polyCtl, portaCtl, bendCtl;

    // Scale quantizer + microtuning (bottom-right block, under the voicing row)
    std::unique_ptr<goaui::Ctl> scaleCtl, rootCtl, tuneCtl;
    std::unique_ptr<goaui::ToggleCtl> lockCtl;
    juce::TextButton sclBtn { "SCL", "12-TET \u2014 load a Scala .scl microtuning file" };
    std::shared_ptr<juce::FileChooser> sclChooser;   // one dialog at a time

    // Sidechain pump + analog character (bottom-right, third row)
    std::unique_ptr<goaui::Ctl> pumpSyncCtl, pumpDepthCtl, analogCtl;

    // Interface zoom (100..200%), persisted per machine in %APPDATA%\GoaSynth
    // along with the last window size (standalone hosts forget their bounds).
    float uiZoom = 1.0f;
    juce::TextButton zoomBtn { "100%", "Interface zoom: 100 / 125 / 150 / 175 / 200 %" };
    void applyZoom (float z);
    void loadZoomPref();
    void saveZoomPref();
    static juce::File zoomPrefFile();

    // Theme persistence: the skin picked in the THEME dropdown survives editor
    // reopens (same %APPDATA%\GoaSynth folder as the size/zoom preferences).
    void loadThemePref();
    void saveThemePref();
    static juce::File themePrefFile();

    // Window size persistence: the plugin's default stays 1120 x 780, but the
    // editor remembers the last size for hosts that start every session at
    // the default (same %APPDATA%\GoaSynth folder as the zoom preference).
    static constexpr int defaultWidth  = 1120;
    static constexpr int defaultHeight = 780;
    void loadSizePref();
    void saveSizePref();
    static juce::File sizePrefFile();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GoaSynthAudioProcessorEditor)
};
