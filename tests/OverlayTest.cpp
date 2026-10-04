// Overlay smoke test: build the real editor on the desktop and drive the
// header buttons through the same onClick handlers the user's clicks run.
//
// This exists because two wiring bugs shipped side by side and nothing
// caught them:
//   1. modOverlay was created but never addChildComponent()ed — setVisible()
//      toggled a component that wasn't on screen, so the MOD button did
//      nothing at all.
//   2. ModOverlay::resized() was an empty override, so its title label and
//      close button stayed at zero size (invisible, unclickable) inside the
//      painted card.
//
// The test pins both bug classes down for good: after running each overlay's
// open path it asserts the overlay is on screen (visible parent chain,
// non-degenerate bounds) and that its children were actually laid out
// (non-zero rects). It also resizes the window with an overlay open — the
// editor must re-fit it rather than leave stale bounds.
//
// Handlers are invoked via btn.onClick() rather than triggerClick(): the
// latter posts an async command message, and this harness has no dispatch
// loop (JUCE_MODAL_LOOPS_PERMITTED=0, as in the plugin itself). The wiring
// under test — overlay attachment, layout, toggle state — is synchronous.
//
// Everything runs through the public component tree (find-by-text/type),
// not editor internals, so the test doubles as an encapsulation check.
//
//   cmake --build build --config Release --target OverlayTest
//   ctest -R OverlayTest
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_graphics/juce_graphics.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <functional>
#include <utility>
#include <vector>

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "License.h"
#include "Parameters.h"
#include "TestMasterKey.h"   // throwaway master key for the suite (see the header)

// ---- tiny assertion helpers -------------------------------------------------

static int fails = 0;

#define EXPECT(cond, msg)                                                     \
    do                                                                        \
    {                                                                         \
        if (! (cond))                                                         \
        {                                                                     \
            std::printf ("FAIL: %s\n", juce::String (msg).toRawUTF8());      \
            ++fails;                                                          \
        }                                                                     \
        else                                                                  \
        {                                                                     \
            std::printf ("ok: %s\n", juce::String (msg).toRawUTF8());        \
        }                                                                     \
    } while (false)

// First descendant of `parent` whose type matches T (public tree only).
template <typename T>
static T* findDescendant (juce::Component* parent)
{
    if (parent == nullptr)
        return nullptr;
    for (auto* c : parent->getChildren())
    {
        if (auto* t = dynamic_cast<T*> (c))
            return t;
        if (auto* deep = findDescendant<T> (c))
            return deep;
    }
    return nullptr;
}

static juce::TextButton* findButton (juce::Component* parent, const juce::String& text)
{
    if (parent == nullptr)
        return nullptr;
    for (auto* c : parent->getChildren())
    {
        if (auto* b = dynamic_cast<juce::TextButton*> (c))
            if (b->getButtonText() == text)
                return b;
        if (auto* deep = findButton (c, text))
            return deep;
    }
    return nullptr;
}

// First button whose tooltip starts with `prefix`. Needed for buttons whose
// label is not usable as an identity: the trial badge reads "TRIAL 4 h 12 m"
// while a trial runs, and is left with an EMPTY label when the synth is
// licensed - only its tooltip is fixed, so that is what identifies it.
static juce::Button* findButtonByTooltip (juce::Component* parent,
                                          const juce::String& prefix)
{
    if (parent == nullptr)
        return nullptr;
    for (auto* c : parent->getChildren())
    {
        if (auto* b = dynamic_cast<juce::Button*> (c))
            if (auto* tip = dynamic_cast<juce::SettableTooltipClient*> (b))
                if (tip->getTooltip().startsWithIgnoreCase (prefix))
                    return b;
        if (auto* deep = findButtonByTooltip (c, prefix))
            return deep;
    }
    return nullptr;
}

// First Label whose text starts with `prefix` (for prose labels that have no
// parameter id to identify them by).
static juce::Label* findLabelStarting (juce::Component* parent,
                                       const juce::String& prefix)
{
    if (parent == nullptr)
        return nullptr;
    for (auto* c : parent->getChildren())
    {
        if (auto* l = dynamic_cast<juce::Label*> (c))
            if (l->getText().startsWithIgnoreCase (prefix))
                return l;
        if (auto* deep = findLabelStarting (c, prefix))
            return deep;
    }
    return nullptr;
}

// A synthetic MouseEvent, for driving a ListBox row gesture through the real
// click routing (star column / tag column / plain click / right-click) instead
// of calling the handlers behind its back. MouseEvent has no default
// constructor, so every field has to be supplied.
static juce::MouseEvent makeRowClick (juce::Component& target, int x, int y,
                                      bool rightButton, int numClicks = 1)
{
    const auto pos = juce::Point<float> ((float) x, (float) y);
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(),
                             pos,
                             rightButton ? juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier)
                                         : juce::ModifierKeys(),
                             1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                             &target, &target, now, pos, now, numClicks, false);
}

// Sum of every parameter's normalised value: a cheap fingerprint of "which patch
// is loaded". applyPreset() rewrites parameters, so any change proves the click
// actually loaded something.
static double patchFingerprint (GoaSynthAudioProcessor& proc)
{
    double sum = 0.0;
    for (auto* p : proc.getParameters())
        sum += p->getValue() * (double) (1 + (p->getName (32).length() % 7));
    return sum;
}

// Does any Label in the tree currently read exactly `text`? Used to confirm the
// header's preset name followed the selection, through the public tree only.
static bool anyLabelReads (juce::Component* c, const juce::String& text)
{
    if (c == nullptr)
        return false;
    if (auto* l = dynamic_cast<juce::Label*> (c))
        if (l->getText() == text)
            return true;
    for (auto* ch : c->getChildren())
        if (anyLabelReads (ch, text))
            return true;
    return false;
}

// Same idea for Buttons: the zoom readout is a TextButton, not a Label, so
// anyLabelReads() cannot see it. Used by the size/zoom sandbox self-check to
// prove the saved zoom preference was actually read back.
static bool anyButtonReads (juce::Component* c, const juce::String& text)
{
    if (c == nullptr)
        return false;
    if (auto* b = dynamic_cast<juce::Button*> (c))
        if (b->getButtonText() == text)
            return true;
    for (auto* ch : c->getChildren())
        if (anyButtonReads (ch, text))
            return true;
    return false;
}

// First goaui::Ctl / ToggleCtl wired to a given parameter id.
static juce::Component* findCtlByParam (juce::Component* parent, const juce::String& id)
{
    if (parent == nullptr)
        return nullptr;
    for (auto* c : parent->getChildren())
    {
        if (auto* ctl = dynamic_cast<goaui::Ctl*> (c))
            if (ctl->paramId == id)
                return ctl;
        if (auto* tg = dynamic_cast<goaui::ToggleCtl*> (c))
            if (tg->paramId == id)
                return tg;
        if (auto* deep = findCtlByParam (c, id))
            return deep;
    }
    return nullptr;
}

// First LfoGraph whose rate parameter id matches (LFO 1 vs LFO 2).
static goaui::LfoGraph* findLfoGraph (juce::Component* parent, const juce::String& rateId)
{
    if (parent == nullptr)
        return nullptr;
    for (auto* c : parent->getChildren())
    {
        if (auto* g = dynamic_cast<goaui::LfoGraph*> (c))
            if (g->rateParamId == rateId)
                return g;
        if (auto* deep = findLfoGraph (c, rateId))
            return deep;
    }
    return nullptr;
}

// An overlay "is on screen" when it is visible, has real bounds, and sits on
// a visible parent chain. Catches exactly the addChildComponent omission:
// a detached component reports isVisible() true while being nowhere.
static void expectOnScreen (juce::Component& c, const char* what)
{
    bool ok = c.isVisible();
    const auto b = c.getBounds();
    ok = ok && b.getWidth() > 100 && b.getHeight() > 100;
    for (auto* p = c.getParentComponent(); ok && p != nullptr; p = p->getParentComponent())
        ok = p->isVisible();
    EXPECT (ok, what);
}

// Children a resized() pass must have laid out. Zero-sized children are the
// second bug class (the invisible MOD title / close button).
static void expectChildLaidOut (juce::Component& parent, const char* what)
{
    bool any = false;
    for (auto* c : parent.getChildren())
    {
        const auto b = c->getBounds();
        if (b.getWidth() == 0 || b.getHeight() == 0)
        {
            std::printf ("FAIL: %s (zero-sized child)\n", what);
            ++fails;
            return;
        }
        any = true;
    }
    if (! any)
    {
        std::printf ("FAIL: %s (no children at all)\n", what);
        ++fails;
        return;
    }
    std::printf ("ok: %s\n", what);
}

// Run a button's handler synchronously and let any bookkeeping settle.
static void press (juce::Button& btn)
{
    if (btn.onClick != nullptr)
        btn.onClick();
    juce::Thread::sleep (8);
}

// ---- layout audit -----------------------------------------------------------
// "Nothing should overlap" made checkable. Two visible siblings sharing pixels
// means one is painted over the other, so the covered control's name/readout is
// unreadable. This walks a component tree and counts such pairs.
//
// Exemptions, all by design:
//   * A child whose bounds equal its parent's is a full-surface cover - the
//     dimmed backdrop of an overlay, the editor's own background. It is skipped
//     as a *participant* but still recursed into, so an open overlay's internals
//     get audited while it deliberately covers the panel underneath.
//   * Panel frames are decorative backgrounds; the knobs are added to the editor
//     and positioned inside them on purpose, so panel-vs-control never counts -
//     but panel-vs-panel and control-vs-control do.
//   * Hidden subtrees are not on screen and are not walked at all, so a closed
//     overlay's stale bounds can never produce a phantom hit.
//
// A readable name, so a failure names the control rather than a vtable symbol.
static juce::String describeComponent (juce::Component* c)
{
    if (auto* ctl = dynamic_cast<goaui::Ctl*> (c))
        return "Ctl[" + ctl->paramId + "]";
    if (auto* tg = dynamic_cast<goaui::ToggleCtl*> (c))
        return "Toggle[" + tg->paramId + "]";
    if (auto* btn = dynamic_cast<juce::TextButton*> (c))
        return "Button[" + btn->getButtonText() + "]";
    if (auto* lab = dynamic_cast<juce::Label*> (c))
        return "Label[" + lab->getText().substring (0, 14) + "]";
    if (auto* cb = dynamic_cast<juce::ComboBox*> (c))
        return "Combo[" + cb->getText() + "]";
    return juce::String (typeid (*c).name());
}

static bool isPanelFrame (juce::Component* c)
{
    return dynamic_cast<goaui::Panel*> (c) != nullptr;
}

static void walkOverlaps (juce::Component& parent, const juce::String& where,
                          int& pairs, juce::String& firstHit)
{
    auto& kids = parent.getChildren();
    const auto parentBounds = parent.getLocalBounds();

    for (int i = 0; i < kids.size(); ++i)
    {
        auto* a = kids[i];
        if (! a->isVisible() || a->getBounds() == parentBounds)
            continue;

        for (int j = i + 1; j < kids.size(); ++j)
        {
            auto* b = kids[j];
            if (! b->isVisible() || b->getBounds() == parentBounds)
                continue;
            if (isPanelFrame (a) != isPanelFrame (b))
                continue;

            const auto inter = a->getBounds().getIntersection (b->getBounds());
            if (inter.getWidth() > 1 && inter.getHeight() > 1)
            {
                ++pairs;
                const juce::String msg =
                    where + "  " + describeComponent (&parent) + "  ->  "
                    + describeComponent (a) + " " + a->getBounds().toString()
                    + "  vs  " + describeComponent (b) + " " + b->getBounds().toString();
                if (firstHit.isEmpty())
                    firstHit = msg;
                std::printf ("  overlap: %s\n", (const char*) msg.toRawUTF8());
            }
        }
    }

    for (auto* c : parent.getChildren())
        if (c->isVisible())
            walkOverlaps (*c, where, pairs, firstHit);
}

// Count overlapping sibling pairs anywhere under `root`, appending the first
// hit to `firstHit` (which is also why it is passed in, not returned).
static int countOverlaps (juce::Component& root, const juce::String& where,
                          juce::String& firstHit)
{
    int pairs = 0;
    walkOverlaps (root, where, pairs, firstHit);
    return pairs;
}

// Visible components in a subtree. Printed alongside each audit so a "0
// overlaps" line can be told apart from a walk that had nothing to inspect.
static int countVisibleNodes (juce::Component& c)
{
    int n = 1;
    for (auto* ch : c.getChildren())
        if (ch->isVisible())
            n += countVisibleNodes (*ch);
    return n;
}

// Set when GOASYNTH_KNOB_SHOT is set: capture docs/assets/mod-dots.png.
static bool wantKnobShot = false;

// Set when GOASYNTH_BROWSER_SHOT is set: capture docs/assets/preset-browser.png.
static bool wantBrowserShot = false;

int main()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    const juce::ScopedJuceInitialiser_GUI juceInit;

    // --screenshot-knob: render one knob (with a mod routing active) into
    // docs/assets/mod-dots.png for the README's UI-conventions section.
    // Runs first, then continues into the normal assertions.
    if (std::getenv ("GOASYNTH_KNOB_SHOT") != nullptr)
        wantKnobShot = true;

    // GOASYNTH_BROWSER_SHOT=1 captures docs/assets/preset-browser.png with the
    // browser overlay open, which docs/index.html embeds. Nothing else keeps
    // that file honest: the shipped one predated the browser overhaul, so it
    // still advertised a UI with no favourites tab, sort combo, count label or
    // star column.
    if (std::getenv ("GOASYNTH_BROWSER_SHOT") != nullptr)
        wantBrowserShot = true;

    // License sandbox (same trick as RoundTripTest): point the license store
    // at throwaway temp files and activate with the dev master key, so the
    // editor builds the real synth UI instead of the activation screen.
    const juce::File lic = juce::File::getSpecialLocation (juce::File::tempDirectory)
                               .getChildFile ("goasynth_overlay_test_lic.txt");
    const juce::File led = juce::File::getSpecialLocation (juce::File::tempDirectory)
                               .getChildFile ("goasynth_overlay_test_led.txt");
    const juce::File trial = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                 .getChildFile ("goasynth_overlay_test_trial.txt");
    lic.deleteFile(); led.deleteFile(); trial.deleteFile();

    // juce_core has no portable setter for the process environment. On Windows
    // _putenv stores the pointer it is given rather than copying the string, so
    // the text has to outlive the call: a std::deque keeps every element's
    // address stable as more are added. (Passing a temporary String's buffer
    // "works" only until the allocator reuses it - which would silently undo
    // the sandbox these overrides exist to create.)
    auto putEnv = [] (const char* name, const juce::String& value)
    {
        static std::deque<juce::String> storage;
        storage.push_back (juce::String (name) + "=" + value);
       #if JUCE_WINDOWS
        _putenv (const_cast<char*> (storage.back().toRawUTF8()));
       #else
        setenv (name, value.toRawUTF8(), 1);
       #endif
    };
    putEnv ("GOASYNTH_LICENSE_FILE", lic.getFullPathName());
    putEnv ("GOASYNTH_LEDGER_FILE",  led.getFullPathName());
    putEnv ("GOASYNTH_TRIAL_FILE",   trial.getFullPathName());

    // Preset-bank sandbox. The browser section below saves, renames and
    // favourites presets; without this override those writes would land in the
    // real %APPDATA%/GoaSynth bank and could overwrite a user's own patch.
    // Nested one level down so browserStateFile() (a sibling of Presets/) also
    // lands inside the sandbox rather than loose in the temp directory.
    const juce::File bankRoot = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                    .getChildFile ("goasynth_overlay_test_root");
    bankRoot.deleteRecursively();
    const juce::File bank = bankRoot.getChildFile ("Presets");
    bank.createDirectory();
    const juce::File shared = bankRoot.getChildFile ("Shared");
    shared.createDirectory();
    putEnv ("GOASYNTH_PRESET_DIR", bank.getFullPathName());
    putEnv ("GOASYNTH_SHARED_PRESET_DIR", shared.getFullPathName());

    // Self-check BEFORE any preset write. If the override were ignored the whole
    // browser section would silently mutate the developer's real bank, so this
    // fails loudly and stops rather than proceeding on a false assumption.
    if (userpresets::presetsDir() != bank
        || userpresets::sharedPresetsDir() != shared
        || ! userpresets::browserStateFile().isAChildOf (bankRoot))
    {
        std::printf ("FAIL: GOASYNTH_PRESET_DIR not honoured (presetsDir=%s) - "
                     "refusing to run the browser tests against the real bank\n",
                     (const char*) userpresets::presetsDir().getFullPathName().toRawUTF8());
        return 1;
    }

    // Window-size / zoom sandbox. The editor remembers the last window size and
    // the UI zoom in the same per-user folder as the preset bank, and
    // saveSizePref() fires from the DESTRUCTOR - so the size sweeps further down
    // (the editor is pushed through its whole resize range several times) would
    // silently overwrite the real user's saved window size. Point both
    // preferences at throwaway files, then prove the override is honoured
    // BEFORE anything is written: seed sentinels, build a disposable editor and
    // check it came up at exactly those values. If the override were ignored the
    // editor would open at the 1120 x 780 default and this fails loudly instead
    // of clobbering real state.
    const juce::File sizeTmp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                   .getChildFile ("goasynth_overlay_test_size.txt");
    const juce::File zoomTmp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                   .getChildFile ("goasynth_overlay_test_zoom.txt");
    const juce::File themeTmp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                    .getChildFile ("goasynth_overlay_test_theme.txt");
    sizeTmp.deleteFile(); zoomTmp.deleteFile(); themeTmp.deleteFile();
    putEnv ("GOASYNTH_SIZE_FILE", sizeTmp.getFullPathName());
    putEnv ("GOASYNTH_ZOOM_FILE",  zoomTmp.getFullPathName());
    putEnv ("GOASYNTH_THEME_FILE", themeTmp.getFullPathName());
    sizeTmp.replaceWithText ("1024 820");
    zoomTmp.replaceWithText ("1.50");
    themeTmp.replaceWithText ("2");   // WARM ANALOG: prove the skin is restored
    {
        GoaSynthAudioProcessor probe;
        std::unique_ptr<juce::AudioProcessorEditor> pe (probe.createEditor());
        const bool sizeOk  = pe != nullptr && pe->getWidth() == 1024 && pe->getHeight() == 820;
        const bool zoomOk  = anyButtonReads (pe.get(), "150%");
        const bool themeOk = anyButtonReads (pe.get(), "THEME: WARM");
        if (! sizeOk || ! zoomOk || ! themeOk)
        {
            std::printf ("FAIL: GOASYNTH_SIZE_FILE / GOASYNTH_ZOOM_FILE / GOASYNTH_THEME_FILE "
                         "not honoured (probe opened %dx%d, zoom %s, theme %s) - "
                         "refusing to run the layout sweeps against the real preferences\n",
                         pe != nullptr ? pe->getWidth() : -1,
                         pe != nullptr ? pe->getHeight() : -1,
                         zoomOk ? "150%" : "not 150%",
                         themeOk ? "WARM" : "not WARM");
            return 1;
        }
        std::printf ("[sandbox] size/zoom/theme overrides honoured (probe 1024x820, zoom 150%%, theme WARM)\n");

        // Back to the house look for the rest of the suite, and drop the seeded
        // file so any later editor construction loads UV rather than WARM.
        goaui::setTheme (goaui::themeUv, nullptr);
        themeTmp.deleteFile();
    }
    // That probe's destructor just wrote its size into the sandbox. Clear both so
    // the editor under test comes up at the documented 1120 x 780 default and
    // every size assertion below starts from a known state.
    sizeTmp.deleteFile(); zoomTmp.deleteFile();

    // Seed a known user bank BEFORE the editor is built: allPresets is populated
    // in the editor's constructor, so files written later would not show up
    // without a refresh path the test cannot reach. Real state XML (from a
    // throwaway processor) so clicking a row really loads a patch and the
    // fingerprint can prove it.
    {
        GoaSynthAudioProcessor seed;
        const auto mk = [&seed] (const char* name, const juce::StringArray& tags,
                                 float cutoff)
        {
            seed.apvts.getParameter ("cutoff")->setValueNotifyingHost (cutoff);
            auto xml = seed.stateToXml();
            if (xml == nullptr)
            {
                std::printf ("FAIL: could not seed preset '%s'\n", name);
                ++fails;
                return;
            }
            if (! userpresets::savePreset (name, *xml, tags))
            {
                std::printf ("FAIL: could not write seeded preset '%s'\n", name);
                ++fails;
            }
        };
        // Names chosen to make A-Z / Z-A unambiguous among the user bank, and
        // written WITH underscores because safeFileName() maps every space to an
        // underscore - saving "AA TEST PATCH" would list it as "AA_TEST_PATCH",
        // so the test would be asserting against a name that never exists.
        // The tags are chosen so "testtag" appears in NO preset name, which is
        // what proves the search reaches into tags rather than just the title.
        mk ("AA_TEST_PATCH", { "testtag", "alphatag" }, 0.20f);
        mk ("MM_TEST_PATCH", { "testtag" },              0.55f);
        mk ("ZZ_TEST_PATCH", { "testtag", "omegatag" },  0.90f);

        EXPECT (userpresets::scanDir (bank).size() == 3,
                "preset sandbox seeded with 3 user patches");
    }

    goa::License::setTestMachineId ("2D6EFDDAA1AC30F510C8");
    {
        juce::String err;
        if (! goa::License::activate (GOA_TEST_MASTER_KEY, err))
        {
            std::printf ("FAIL: license sandbox activation failed: %s\n",
                         (const char*) err.toRawUTF8());
            return 1;
        }
    }

    GoaSynthAudioProcessor proc;
    goa::ModMatrix::Values mvTest;
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    EXPECT (ed != nullptr, "editor created");
    if (ed == nullptr)
        return 1;
    // Real-host conditions: visible and on the desktop, so the effective
    // visibility chain is meaningful and close-on-miss clicks would land.
    ed->setVisible (true);
    ed->addToDesktop (juce::ComponentPeer::windowIsTemporary);
    juce::Thread::sleep (16);

    // ---- auditor self-test --------------------------------------------------
    // countOverlaps is what certifies "nothing overlaps", so it must be shown
    // to actually fire. A walker that silently visited nothing would report 0
    // overlaps forever and look identical to a clean layout - which is exactly
    // how a green audit lies. Pin the detector down before trusting its verdict.
    {
        juce::Component holder;
        holder.setSize (200, 200);
        juce::Component a, b;
        holder.addAndMakeVisible (a);
        holder.addAndMakeVisible (b);
        a.setBounds (10, 10, 60, 60);
        b.setBounds (40, 40, 60, 60);   // shares 30x30 with a
        juce::String hit;
        const int bad = countOverlaps (holder, "self-test", hit);

        b.setBounds (100, 100, 60, 60);   // now clear of a
        juce::String clearHit;
        const int good = countOverlaps (holder, "self-test", clearHit);
        EXPECT (bad == 1 && good == 0 && hit.isNotEmpty(),
                "the overlap auditor detects an overlap and clears when moved");

        // The exemptions must hold too, or the audit is just false alarms. A
        // full-surface cover (an overlay's dimmed backdrop) overlaps everything
        // underneath by definition and must not be counted.
        juce::Component cover;
        holder.addAndMakeVisible (cover);
        cover.setBounds (holder.getLocalBounds());
        juce::String coverHit;
        EXPECT (countOverlaps (holder, "self-test-cover", coverHit) == 0,
                "a full-surface cover is exempt from the overlap audit");

        // A hidden component is not on screen, so a stale overlap involving it
        // must not be reported (this is what keeps closed overlays quiet).
        a.setBounds (10, 10, 60, 60);
        b.setBounds (40, 40, 60, 60);   // overlapping again...
        b.setVisible (false);           // ...but hidden
        juce::String hiddenHit;
        EXPECT (countOverlaps (holder, "self-test-hidden", hiddenHit) == 0,
                "a hidden component is skipped by the overlap audit");
        b.setVisible (true);
    }

    // ---- layout audit: no two sibling controls may overlap -----------------
    // Swept across the window sizes the plugin actually allows
    // (setResizeLimits(960, 720, 2048, 1440)): the fixed-size day meant only
    // 1120x780 was ever laid out, and the rows are laid out by arithmetic that
    // can run out of width before it runs out of controls.
    //
    // The rules (full-surface covers, Panel frames, hidden subtrees) live in
    // countOverlaps at the top of this file; the same helper audits the
    // overlays below, so main panel and overlays cannot drift apart.
    {
        // Corners and mid-points of the allowed resize range. The 640-tall
        // entries are below the enforced minimum (720) on purpose: the layout
        // must degrade without overlapping even if a host ignores the limits,
        // which is exactly how the FX row / bottom bar collision shipped.
        const int sizes[][2] = {
            { 1120, 780 },   // the default
            {  960, 720 },   // minimum
            {  960, 780 },
            { 1120, 720 },
            { 1300, 900 },
            { 1600, 1100 },
            { 2048, 720 },   // widest, shortest
            { 2048, 1440 },  // maximum
            {  960, 640 },   // below the limit - defensive
            { 1120, 640 },
            { 2048, 640 }
        };

        int pairs = 0;
        juce::String firstHit;
        for (const auto& s : sizes)
        {
            ed->setSize (s[0], s[1]);
            juce::Thread::sleep (4);
            const int before = pairs;
            pairs += countOverlaps (*ed, juce::String (s[0]) + "x" + juce::String (s[1]),
                                    firstHit);
            std::printf ("[layout] %dx%d -> %d overlapping sibling pair(s) across %d components\n",
                         s[0], s[1], pairs - before, countVisibleNodes (*ed));
        }

        // Leave the editor at its default size: the destructor persists
        // whatever size it is destroyed at.
        ed->setSize (1120, 780);
        juce::Thread::sleep (4);

        EXPECT (pairs == 0, "no two sibling controls overlap at any window size"
                            + (firstHit.isNotEmpty() ? juce::String (": ") + firstHit
                                                     : juce::String()));
    }

    // Optional full-UI capture for the docs site, at the default window size
    // with no overlay up (this point in the run is the only one where the plain
    // synth UI is on screen):
    //     GOASYNTH_UI_SHOT=1 ./build/Release/OverlayTest
    // It regenerates docs/assets/ui-overview.png, which docs/index.html embeds.
    // Nothing else keeps that file honest, and it drifts silently: the shipped
    // one still showed a MACRO frame with no controls, the pre-shortening knob
    // labels ("MODWHEEL", "WIDTH") and the AI overlay covering the header.
    if (std::getenv ("GOASYNTH_UI_SHOT") != nullptr)
    {
        ed->setSize (1120, 780);
        juce::Thread::sleep (60);
        // Software-backed: the default native type is D2D-backed on Windows and
        // an encode pulled from it can return stale pixels (see the shots in the
        // MOD section for the full story).
        const juce::Image shot = ed->createComponentSnapshot (
            ed->getLocalBounds(), false, 1.0f, juce::SoftwareImageType());
        const auto f = juce::File (GOASYNTH_DOCS_ASSETS "/ui-overview.png");
        juce::PNGImageFormat png;
        juce::MemoryOutputStream mo;
        const bool encoded = png.writeImageToStream (shot, mo);
        // replaceWithData, NOT createOutputStream: a stream opened over an
        // existing PNG can leave stale bytes past the new data.
        const bool written = encoded && f.replaceWithData (mo.getData(), mo.getDataSize());
        std::printf ("[shot] %s (%s, %dx%d)\n", written ? "written" : "FAILED",
                     (const char*) f.getFullPathName().toRawUTF8(),
                     shot.getWidth(), shot.getHeight());
        EXPECT (f.existsAsFile() && f.getSize() > 20000, "UI overview screenshot written");
    }

    // ---- on-knob text audit ------------------------------------------------
    // A Ctl draws its NAME on the first line and its live READOUT on the
    // second, so the two can no longer collide however narrow the cell is.
    // What still matters is that each line fits its own width. The readout is
    // asserted (it is live data); an over-long name is only reported, because
    // JUCE squeezes a Label rather than overflowing it - cosmetic, not an
    // overlap.
    {
        struct TextAudit
        {
            static void walk (GoaSynthAudioProcessor& proc, juce::Component* c,
                              int& total, int& readoutOver, int& labelOver,
                              juce::String& firstReadout, juce::String& firstLabel)
            {
                if (c == nullptr)
                    return;
                if (auto* ctl = dynamic_cast<goaui::Ctl*> (c);
                    ctl != nullptr && ! ctl->useCombo)
                {
                    ++total;
                    const int reserve = 1 * 8 + 3;   // one mod dot + its gap
                    const auto readout =
                        goaui::liveValueText (proc.apvts, ctl->paramId, true);
                    const int readoutW = juce::roundToInt (
                        juce::Font (juce::FontOptions (7.0f, juce::Font::bold))
                            .getStringWidth (readout));
                    if (readoutW > ctl->getWidth() - reserve)
                    {
                        ++readoutOver;
                        if (firstReadout.isEmpty())
                            firstReadout = ctl->paramId + " \"" + readout + "\" needs "
                                           + juce::String (readoutW) + "px, has "
                                           + juce::String (ctl->getWidth() - reserve);
                    }

                    const int labelW = juce::roundToInt (
                        juce::Font (juce::FontOptions (9.0f))
                            .getStringWidth (ctl->label.getText()));
                    // Flag a label that cannot render in full. JUCE's Label
                    // insets its text area by ~2px, so a name that merely
                    // measures the cell width still comes out ellipsized
                    // ("DEPTH" -> "DEP..."). 3px of slack is what actually
                    // renders cleanly.
                    if (labelW > ctl->getWidth() - 3)
                    {
                        ++labelOver;
                        if (firstLabel.isEmpty())
                            firstLabel = ctl->paramId + " \"" + ctl->label.getText()
                                         + "\" needs " + juce::String (labelW) + "px, cell "
                                         + juce::String (ctl->getWidth());
                        std::printf ("  name: %-14s \"%s\" needs %dpx, cell %d\n",
                                     (const char*) ctl->paramId.toRawUTF8(),
                                     (const char*) ctl->label.getText().toRawUTF8(),
                                     labelW, ctl->getWidth());
                    }
                }
                for (auto* ch : c->getChildren())
                    walk (proc, ch, total, readoutOver, labelOver, firstReadout, firstLabel);
            }
        };

        int total = 0, readoutOver = 0, labelOver = 0;
        juce::String firstReadout, firstLabel;
        TextAudit::walk (proc, ed.get(), total, readoutOver, labelOver,
                         firstReadout, firstLabel);
        std::printf ("[text] %d knobs; readout over: %d; name squeezed: %d\n",
                     total, readoutOver, labelOver);
        if (firstReadout.isNotEmpty())
            std::printf ("[text] readout: %s\n", (const char*) firstReadout.toRawUTF8());
        if (firstLabel.isNotEmpty())
            std::printf ("[text] name:    %s\n", (const char*) firstLabel.toRawUTF8());

        EXPECT (readoutOver == 0, "every knob's readout fits its own line"
                                  + (firstReadout.isNotEmpty()
                                         ? juce::String (": ") + firstReadout
                                         : juce::String()));
    }

    // Optional visual dump of the layout at several heights, for eyeballing
    // that a short window degrades gracefully rather than merely avoiding
    // overlap:  GOASYNTH_LAYOUT_SHOT=1 ./build/Release/OverlayTest
    if (std::getenv ("GOASYNTH_LAYOUT_SHOT") != nullptr)
    {
        auto shoot = [] (juce::Component& c, juce::Rectangle<int> area, float scale,
                         const juce::String& name)
        {
            auto img = c.createComponentSnapshot (area, false, scale);
            const auto out = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                 .getChildFile ("goasynth_" + name + ".png");
            out.deleteFile();
            juce::FileOutputStream os (out);
            juce::PNGImageFormat png;
            if (png.writeImageToStream (img, os))
                std::printf ("[shot] %s\n", (const char*) out.getFullPathName().toRawUTF8());
        };

        for (const int h : { 780, 720, 640 })
        {
            ed->setSize (1120, h);
            juce::Thread::sleep (40);
            shoot (*ed, ed->getLocalBounds(), 1.0f, "layout_" + juce::String (h));
        }

        // Zoomed halves of the FX row (the row with the tightest cells).
        ed->setSize (1120, 780);
        juce::Thread::sleep (40);
        shoot (*ed, { 0, 535, 560, 140 }, 3.0f, "row3_left");
        shoot (*ed, { 560, 535, 560, 140 }, 3.0f, "row3_right");

        ed->setSize (1120, 780);
        juce::Thread::sleep (20);
    }

    // ---- MOD matrix (the two shipped bugs) --------------------------------
    {
        auto* modBtn = findButton (ed.get(), "MOD");
        EXPECT (modBtn != nullptr, "MOD button found in the header");
        auto* modOverlay = findDescendant<goaui::ModOverlay> (ed.get());
        EXPECT (modOverlay != nullptr, "ModOverlay exists in the tree");
        if (modBtn != nullptr && modOverlay != nullptr)
        {
            press (*modBtn);
            expectOnScreen (*modOverlay, "MOD overlay on screen after press");
            expectChildLaidOut (*modOverlay, "MOD title + close laid out");

            // The overlay is a full-surface cover, so countOverlaps skips it as
            // a participant but still recurses into it: this audits the cells,
            // rows and buttons *inside* the card against each other. Without it
            // the invariant only held for the panel the overlay covers.
            {
                juce::String hit;
                const int pairs = countOverlaps (*ed, "MOD open", hit);
                std::printf ("[layout] MOD overlay open -> %d overlapping sibling pair(s)"
                             " across %d components\n", pairs, countVisibleNodes (*modOverlay));
                EXPECT (pairs == 0, "no overlap among the MOD overlay's controls"
                                    + (hit.isNotEmpty() ? juce::String (": ") + hit
                                                        : juce::String()));

                // That walk only reaches 4 nodes, because the matrix itself is
                // custom-painted rather than built from child components. So the
                // cells - the part that actually carries the information - have
                // to be audited as geometry, or "nothing overlaps" would be
                // asserting nothing about the overlay the user actually reads.
                const auto card = modOverlay->cardBounds();
                const auto grid = modOverlay->rowsRect().toNearestInt();

                EXPECT (modOverlay->getLocalBounds().contains (card),
                        "MOD card fits inside the overlay");
                EXPECT (grid.getWidth() > 0 && grid.getHeight() > 0
                            && card.contains (grid),
                        "MOD matrix grid sits inside the card");

                // Collect all 8 rows x 5 cells and test every pair. The rows are
                // laid out by arithmetic, so a change to the row height or to a
                // cell width can silently make one row bleed into the next.
                std::vector<std::pair<juce::Rectangle<float>, juce::String>> cells;
                for (int r = 0; r < param::modSlots; ++r)
                {
                    const auto g = modOverlay->rowGeo (r);
                    const juce::String tag = "row" + juce::String (r + 1);
                    cells.push_back ({ g.src,   tag + ".SRC" });
                    cells.push_back ({ g.dst,   tag + ".DST" });
                    cells.push_back ({ g.curve, tag + ".CURVE" });
                    cells.push_back ({ g.lag,   tag + ".LAG" });
                    cells.push_back ({ g.amt,   tag + ".AMT" });
                }

                int cellOverlaps = 0;
                juce::String firstCellHit;
                for (size_t i = 0; i < cells.size(); ++i)
                    for (size_t j = i + 1; j < cells.size(); ++j)
                    {
                        const auto inter =
                            cells[i].first.getIntersection (cells[j].first);
                        if (inter.getWidth() > 0.5f && inter.getHeight() > 0.5f)
                        {
                            ++cellOverlaps;
                            if (firstCellHit.isEmpty())
                                firstCellHit = cells[i].second + " " + cells[i].first.toString()
                                             + "  vs  " + cells[j].second + " "
                                             + cells[j].first.toString();
                        }
                    }
                std::printf ("[layout] MOD matrix -> %d painted cells, %d overlap(s)\n",
                             (int) cells.size(), cellOverlaps);
                EXPECT (cellOverlaps == 0, "no two MOD matrix cells overlap"
                                           + (firstCellHit.isNotEmpty()
                                                  ? juce::String (": ") + firstCellHit
                                                  : juce::String()));

                // The three real children must clear the painted grid as well.
                for (auto* ch : modOverlay->getChildren())
                {
                    const auto inter = ch->getBounds().getIntersection (grid);
                    EXPECT (inter.getWidth() <= 1 || inter.getHeight() <= 1,
                            "MOD child \"" + describeComponent (ch)
                                + "\" clears the painted matrix grid");
                }

                // The footer cheat-sheet is the only documentation of a painted
                // matrix's affordances, so every line must fit: a truncated tail
                // silently drops the last item (ESC), which is exactly what one
                // 9pt line in a 528px band did before it was split in two.
                for (auto* prefix : { "click SRC", "click the amount bar" })
                {
                    auto* l = findLabelStarting (modOverlay, prefix);
                    EXPECT (l != nullptr, juce::String ("MOD cheat-sheet line found: ")
                                              + prefix);
                    if (l == nullptr)
                        continue;
                    const int textW = juce::roundToInt (
                        l->getFont().getStringWidth (l->getText()));
                    std::printf ("[text] MOD cheat-sheet %dpx in %dpx\n",
                                 textW, l->getWidth());
                    EXPECT (textW <= l->getWidth(),
                            "MOD cheat-sheet line fits (" + juce::String (textW)
                                + "px in " + juce::String (l->getWidth()) + "px)");
                }

                // Same optional dump as the main layout:  GOASYNTH_LAYOUT_SHOT=1
                if (std::getenv ("GOASYNTH_LAYOUT_SHOT") != nullptr)
                {
                    const juce::Image shot = ed->createComponentSnapshot (
                        card, false, 2.0f, juce::SoftwareImageType());
                    const auto out = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                         .getChildFile ("goasynth_mod_matrix.png");
                    out.deleteFile();
                    juce::FileOutputStream os (out);
                    juce::PNGImageFormat png;
                    if (png.writeImageToStream (shot, os))
                        std::printf ("[shot] %s\n",
                                     (const char*) out.getFullPathName().toRawUTF8());
                }
            }

            // Resize the window with the overlay up: the editor must re-fit it.
            ed->setBounds (ed->getBounds().withSize (1300, 900));
            juce::Thread::sleep (8);
            expectOnScreen (*modOverlay, "MOD overlay re-fits on resize");
            {
                juce::String hit;
                const int pairs = countOverlaps (*ed, "MOD @1300x900", hit);
                std::printf ("[layout] MOD overlay @1300x900 -> %d overlapping sibling pair(s)"
                             " across %d components\n", pairs, countVisibleNodes (*modOverlay));
                EXPECT (pairs == 0, "MOD matrix stays clean when the window grows"
                                    + (hit.isNotEmpty() ? juce::String (": ") + hit
                                                        : juce::String()));
            }

            press (*modBtn);
            EXPECT (! modOverlay->isVisible(), "MOD overlay closes on 2nd press");

            // Close button path: open again, click the overlay's own X.
            press (*modBtn);
            if (modOverlay->isVisible())
            {
                if (auto* x = findButton (modOverlay, "X"))
                {
                    press (*x);
                    EXPECT (! modOverlay->isVisible(), "MOD close button works");
                }
                else
                {
                    EXPECT (false, "MOD close button found");
                }
                press (*modBtn);   // ensure clean state if X failed
            }

            // ---- destination pick mode ------------------------------------
            // Arm row 0, cancel via ESC, arm again, then click the filter
            // CUTOFF knob's centre in overlay coordinates to assign it.
            modOverlay->armPick (0);
            EXPECT (modOverlay->isPicking(), "MOD pick mode arms");
            modOverlay->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
            EXPECT (! modOverlay->isPicking(), "ESC cancels pick mode");

            modOverlay->armPick (0);
            juce::Component* cutoffKnob = nullptr;
            std::function<juce::Component* (juce::Component*)> findCtl =
                [&] (juce::Component* p) -> juce::Component*
            {
                if (auto* c = dynamic_cast<goaui::Ctl*> (p))
                    if (c->paramId == param::cutoff)
                        return c;
                for (auto* ch : p->getChildren())
                    if (auto* r = findCtl (ch))
                        return r;
                return nullptr;
            };
            cutoffKnob = findCtl (ed.get());
            EXPECT (cutoffKnob != nullptr, "CUTOFF knob found for pick test");
            if (cutoffKnob != nullptr)
            {
                // Overlay-local coordinates of the knob's centre, then a
                // synthetic "outside the card" press at that point.
                const auto c = cutoffKnob->getBoundsInParent().getCentre();
                modOverlay->tryAssignDestination (c.toFloat(), false);
                EXPECT (modOverlay->isPicking() == false, "assign consumes pick");
                auto* dst = proc.apvts.getParameter (param::modDst (0));
                const int dstIdx = (int) dst->getNormalisableRange().convertFrom0to1 (dst->getValue());
                EXPECT (juce::String (param::modDestList()[(size_t) dstIdx].param)
                            == juce::String (param::cutoff),
                        "click-to-assign routes CUTOFF into row 1");

                // The landed assignment must flash the target knob.
                auto* flashed = dynamic_cast<goaui::Flashable*> (
                    findCtlByParam (ed.get(), param::cutoff));
                EXPECT (flashed != nullptr && flashed->flashFade() > 0.0f,
                        "assignment flashes the target knob");
            }

            // A click on nothing routable is refused and ends the pick.
            modOverlay->armPick (1);
            modOverlay->tryAssignDestination ({ 5.0f, 5.0f }, false);   // top-left corner: the logo
            EXPECT (! modOverlay->isPicking(), "non-routable click cancels pick");

            // README illustration #2: with the matrix open, land a pick
            // assignment and snapshot mid-flash (fading DST cell).
            if (wantKnobShot)
            {
                auto* resoCtl = findCtlByParam (ed.get(), param::reso);
                EXPECT (resoCtl != nullptr, "RESO knob found for flash shot");
                if (resoCtl != nullptr)
                {
                    modOverlay->armPick (2);
                    const auto c = resoCtl->getBoundsInParent().getCentre();
                    modOverlay->tryAssignDestination (c.toFloat(), false);   // flashes
                    // Force a software-backed image: the default native image
                    // type is D2D-backed on Windows and an encode pulled from it
                    // once returned data that no longer matched a second encode
                    // of the same object (fading flash kept moving).
                    const juce::Image shot = ed->createComponentSnapshot (
                        ed->getLocalBounds(), false, 2.0f, juce::SoftwareImageType());
                    // Encode once, write the same bytes everywhere.
                    juce::PNGImageFormat png;
                    juce::MemoryOutputStream mo;
                    const bool encoded = png.writeImageToStream (shot, mo);
                    const auto f = juce::File (GOASYNTH_DOCS_ASSETS "/mod-pick-flash.png");
                    // replaceWithData, NOT createOutputStream: a FileOutputStream
                    // opened on an existing capture left stale bytes past the new
                    // data, and PNG readers then decoded stale (pre-arrow) IDAT
                    // chunks from earlier runs. replaceWithData is atomic.
                    const bool written = encoded
                                         && f.replaceWithData (mo.getData(), mo.getDataSize());
                    std::printf ("[shot] %s (%s, %dx%d)\n", written ? "written" : "FAILED",
                                 (const char*) f.getFullPathName().toRawUTF8(),
                                 shot.getWidth(), shot.getHeight());
                    EXPECT (f.existsAsFile() && f.getSize() > 20000,
                            "pick-flash screenshot written");
                }
            }

            // ---- source pick mode -----------------------------------------
            // Arm SRC of row 0, click the LFO 2 graph -> source becomes LFO 2.
            {
                auto* lfo2Graph = findLfoGraph (ed.get(), param::lfo2Rate);
                EXPECT (lfo2Graph != nullptr, "LFO 2 graph found for source pick");
                if (lfo2Graph != nullptr)
                {
                    modOverlay->armPickSource (0);
                    EXPECT (modOverlay->isPicking(), "source pick arms");
                    const auto c = lfo2Graph->getBoundsInParent().getCentre();
                    modOverlay->tryAssignSource (c.toFloat(), false);
                    auto* srcP = proc.apvts.getParameter (param::modSrc (0));
                    const int srcIdx = (int) srcP->getNormalisableRange().convertFrom0to1 (srcP->getValue());
                    EXPECT (srcIdx == 2, "click-to-assign routes LFO 2 as source");

                    // A knob is NOT a valid source: pick must refuse and end.
                    modOverlay->armPickSource (0);
                    modOverlay->tryAssignSource ({ 5.0f, 5.0f }, false);
                    EXPECT (! modOverlay->isPicking(), "non-source click ends source pick");
                }
            }

            // Pick the new FX destinations too (delay feedback = DELAY FB).
            auto* dlyFb = proc.apvts.getParameter (param::delayFb);
            EXPECT (dlyFb != nullptr, "DELAY FB knob exists for pick test");
            auto* dlyFbBtn = findCtlByParam (ed.get(), param::delayFb);
            EXPECT (dlyFbBtn != nullptr, "DELAY FB control found for pick test");
            if (dlyFbBtn != nullptr)
            {
                modOverlay->armPick (2);
                const auto c = dlyFbBtn->getBoundsInParent().getCentre();
                modOverlay->tryAssignDestination (c.toFloat(), false);
                auto* dst = proc.apvts.getParameter (param::modDst (2));
                const int dstIdx = (int) dst->getNormalisableRange().convertFrom0to1 (dst->getValue());
                EXPECT (juce::String (param::modDestList()[(size_t) dstIdx].param)
                            == juce::String (param::delayFb),
                        "click-to-assign routes DELAY FB into row 3");
            }

            // The confirm flash also draws a brief pointer line from the
            // assigned cell toward the clicked control. It cannot be found by
            // scanning a region: whether the segment leaves the card depends
            // on where the target control happens to sit, and most of the
            // synth UI sits behind the card. So the flash reports the segment
            // it drew and the test samples THAT — a region guess would pass on
            // a stray pixel and fail on a knob just inside the card edge.
            // (The flash is still fresh after the DELAY FB assignment above.)
            if (dlyFbBtn != nullptr)
            {
                // Software-backed, like the screenshot captures above: the
                // default native type is D2D-backed on Windows and its pixels
                // come back stale (a fading flash kept moving between reads).
                const juce::Image shot = ed->createComponentSnapshot (
                    ed->getLocalBounds(), false, 1.0f, juce::SoftwareImageType());
                // Read the pointer AFTER the snapshot: the paint that rendered
                // it is what records the segment.
                const auto line = modOverlay->flashPointer();

                EXPECT (line.getLength() > 1.0f,
                        "confirm flash reports a pointer line");

                // It must aim at the control that was clicked: the line stops
                // 10px short of the target's centre, so "near" is generous.
                const auto aimedAt = dlyFbBtn->getBoundsInParent().getCentre().toFloat();
                EXPECT (line.getEnd().getDistanceFrom (aimedAt) < 20.0f,
                        "pointer line aims at the clicked control");

                // Sample the shaft — away from the flashed cell at one end and
                // the control at the other — and require accentB ink there.
                //
                // NOT an absolute distance to accentB. A stroked line is
                // antialiased and alpha-blended, so its pixels are never exactly
                // accentB, and the blend also depends on how much of the 700 ms
                // fade has elapsed by the time the snapshot is painted. Pinning
                // that number made the test flip on unrelated changes: it read
                // 105 against a limit of 100. What "the line was drawn" really
                // means is that the pixels ALONG the reported segment sit far
                // closer to accentB than the surface immediately beside them, so
                // measure that contrast instead. Measured on the current card:
                // the stroke's core is ~105 from accentB, the surface ~7px off
                // it is ~280 - so the ratio is huge and alpha-independent.
                const auto want = goaui::accentB;
                const auto bounds = shot.getBounds();
                const auto dir = (line.getEnd() - line.getStart()) / line.getLength();
                const juce::Point<float> nrm (-dir.y, dir.x);

                // Squared distance from accentB of the nearest pixel in a small
                // disc around `q` (a disc, because the stroke is antialiased and
                // the sample point can sit between pixel centres).
                auto inkDistance = [&] (juce::Point<float> q)
                {
                    int best = 1 << 30;
                    for (int oy = -2; oy <= 2; ++oy)
                        for (int ox = -2; ox <= 2; ++ox)
                        {
                            const int x = juce::roundToInt (q.x) + ox;
                            const int y = juce::roundToInt (q.y) + oy;
                            if (! bounds.contains (x, y))
                                continue;
                            const auto px = shot.getPixelAt (x, y);
                            const int dr = (int) px.getRed()   - (int) want.getRed();
                            const int dg = (int) px.getGreen() - (int) want.getGreen();
                            const int db = (int) px.getBlue()  - (int) want.getBlue();
                            best = juce::jmin (best, dr * dr + dg * dg + db * db);
                        }
                    return best;
                };

                int probes = 0, drawn = 0;

                for (const float t : { 0.35f, 0.5f, 0.65f, 0.8f })
                {
                    const auto p = line.getPointAlongLineProportionally (t);
                    const int onLine = inkDistance (p);

                    // Furthest of several off-stroke samples: the surface beside
                    // the line is not uniform (cells, borders, glyphs), and one
                    // unlucky offset must not decide the verdict.
                    int offLine = 0;
                    for (const float o : { -10.0f, -7.0f, 7.0f, 10.0f })
                        offLine = juce::jmax (offLine,
                                              inkDistance (p + nrm * o));

                    ++probes;
                    if (onLine * 4 < offLine)   // core at least 2x closer than its surroundings
                        ++drawn;
                }

                std::printf ("[flash] pointer line carries ink at %d of %d probes\n",
                             drawn, probes);

                EXPECT (drawn >= probes - 1,
                        "confirm flash draws a pointer line toward the picked knob");
            }

            // The matrix must actually reach the engine: drive the engine-side
            // slot snapshot directly (processBlock copies APVTS -> slots) and
            // check both a per-voice and a global destination.
            {
                auto& m = proc.synth.mod;
                m.slots[0] = { 5, 1, 1.0f };   // VELOCITY -> CUTOFF, amount 1
                goa::ModMatrix::LagState lagTest;
                m.computeAll (mvTest, 0, 0, 0, 0, 1.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f,
                              lagTest, 48000.0f, 1.0 / 128.0);
                EXPECT (std::abs (mvTest.oct - 4.0f) < 1.0e-4f,
                        "matrix reaches the engine (velocity->cutoff = 4 oct)");

                // Named enum constants, never bare indices: the destination list
                // grows (FILTER B, PULSE W, VOWEL, OTT, PUMP were added), and a
                // literal 15 silently became "OTT DEPTH" when rows were inserted.
                m.slots[1] = { 1, (int) param::mdDelayFb, 0.5f };  // LFO 1 -> DELAY FB, amount 0.5
                m.publishGlobal (1.0f, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f);
                EXPECT (std::abs (m.globalFx.delayFb.load() - 0.3f) < 1.0e-4f,
                        "global FX destination publishes (LFO1->delayFb = 0.3)");

                // The destinations added in this round must reach the engine too:
                // a per-voice one (PULSE W) and a global one (OTT DEPTH).
                m.slots[0] = { 1, (int) param::mdPulseW, 1.0f };   // LFO 1 -> PULSE W
                m.computeAll (mvTest, 1.0f, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f,
                              lagTest, 48000.0f, 1.0 / 128.0);
                EXPECT (std::abs (mvTest.pulseW - 0.45f) < 1.0e-4f,
                        "PULSE W reaches the engine (LFO1 -> +0.45 duty)");

                m.slots[0] = { 1, (int) param::mdCutoff2, 1.0f };  // LFO 1 -> CUTOFF B
                m.computeAll (mvTest, 1.0f, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f,
                              lagTest, 48000.0f, 1.0 / 128.0);
                EXPECT (std::abs (mvTest.oct2 - 4.0f) < 1.0e-4f,
                        "FILTER B cutoff reaches the engine (LFO1 -> 4 oct)");

                m.slots[1] = { 9, (int) param::mdOttDepth, 1.0f }; // MACRO A -> OTT DEPTH
                m.publishGlobal (0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 1.0f, 0.0f);
                EXPECT (std::abs (m.globalFx.ottDepth.load() - 1.0f) < 1.0e-4f,
                        "OTT DEPTH publishes (MACRO A -> full squeeze)");

                m.slots[0] = { 0, 0, 0.0f };   // leave the slots muted
                m.slots[1] = { 0, 0, 0.0f };
                m.publishGlobal (0, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f);
            }

            // Mod dots: with a slot targeting cutoff, the cutoff knob's dot
            // scan must report its source.
            {
                auto setP = [&] (const juce::String& id, float v01)
                {
                    if (auto* p = proc.apvts.getParameter (id))
                    {
                        p->beginChangeGesture();
                        p->setValueNotifyingHost (p->getNormalisableRange().convertTo0to1 (v01));
                        p->endChangeGesture();
                    }
                };
                // setP takes PLAIN range values (index / amount), not
                // normalised fractions - it converts via getNormalisableRange.
                setP (param::modDst (0), 1.0f);    // destination index 1 = CUTOFF
                setP (param::modSrc (0), 5.0f);    // source index 5 = VELOCITY
                setP (param::modAmt (0), 0.5f);
                int src[3] = { 0, 0, 0 };
                float amt[3] = { 0, 0, 0 };
                const int n = goaui::modDotSources (proc.apvts, param::cutoff, src, amt);
                EXPECT (n == 1 && src[0] == 5 && std::abs (amt[0] - 0.5f) < 1.0e-4f,
                        "cutoff knob reports its mod dot (velocity, +0.5)");
                int none[3] = { 0, 0, 0 };
                float noneAmt[3] = { 0, 0, 0 };
                EXPECT (goaui::modDotSources (proc.apvts, param::masterGain, none, noneAmt) == 0,
                        "unrelated knobs report no dots");

                // The dot tooltip must name source, destination and amount.
                juce::String tip;
                EXPECT (goaui::modDotTip (proc.apvts, param::cutoff, tip)
                            && tip.contains ("VELOCITY") && tip.contains ("CUTOFF")
                            && tip.contains ("+50"),
                        "dot tooltip names source, dest and amount");
                juce::String noTip;
                EXPECT (! goaui::modDotTip (proc.apvts, param::masterGain, noTip),
                        "unrouted knobs have no MOD tooltip");
                setP (param::modAmt (0), 0.0f);   // amount 0 (mute)

                // Every knob carries a base tooltip (its parameter name), and
                // the big displays have hand-written ones.
                auto* cutoffCtl = dynamic_cast<goaui::Ctl*> (findCtlByParam (ed.get(), param::cutoff));
                EXPECT (cutoffCtl != nullptr && cutoffCtl->slider.getTooltip().isNotEmpty(),
                        "knobs have base tooltips (parameter name)");
                auto* waveTip = dynamic_cast<juce::SettableTooltipClient*> (
                    findDescendant<goaui::WaveDisplay> (ed.get()));
                EXPECT (waveTip != nullptr && waveTip->getTooltip().isNotEmpty(),
                        "wavetable display has a tooltip");

                // Live-value tooltip: hovering refreshes the text each paint,
                // so after the knob's paint ran the tooltip must carry the
                // value in engine units (cutoff default 1500 Hz).
                juce::Image scratch (juce::Image::ARGB, 64, 64, true);
                juce::Graphics g (scratch);
                cutoffCtl->paint (g);   // force one tooltip refresh
                EXPECT (cutoffCtl->slider.getTooltip().contains ("Hz"),
                        "knob tooltip shows the live value in Hz");

                // README illustration: snapshot the real editor region around
                // the knob (dots + readout) at 4x into docs/assets/mod-dots.png.
                if (wantKnobShot)
                {
                    const auto region = cutoffCtl->getBounds().expanded (12);
                    const juce::Image shot =
                        ed->createComponentSnapshot (region, false, 4.0f, juce::SoftwareImageType());
                    const auto f = juce::File (GOASYNTH_DOCS_ASSETS "/mod-dots.png");
                    juce::PNGImageFormat png;
                    juce::MemoryOutputStream mo;
                    const bool encoded = png.writeImageToStream (shot, mo);
                    // replaceWithData, NOT createOutputStream: a FileOutputStream
                    // opened on an existing capture can leave stale bytes past the
                    // new data (PNG readers then decode stale IDAT chunks).
                    const bool written = encoded
                                         && f.replaceWithData (mo.getData(), mo.getDataSize());
                    std::printf ("[shot] %s (%s, %dx%d)\n", written ? "written" : "FAILED",
                                 (const char*) f.getFullPathName().toRawUTF8(),
                                 shot.getWidth(), shot.getHeight());
                    EXPECT (f.existsAsFile() && f.getSize() > 4096,
                            "knob screenshot written");
                }
            }
        }
    }

    // The grid toggles (P.RAND etc.) render as mini-switch strips, not stock
    // tickboxes. Pixel-verified: the strip's thumb is accent-coloured only when
    // engaged, so flipping the control must swing the accent ink inside the
    // control's bounds. Two flips leave the original state behind. Runs after
    // the MOD section: the overlay's dimmed backdrop would otherwise darken
    // every pixel under the snapshot.
    if (auto* prand = dynamic_cast<goaui::ToggleCtl*> (
            findCtlByParam (ed.get(), param::osc1PRand)); prand != nullptr)
    {
        // The pick tests leave the MOD overlay up; close it so the strip
        // isn't shot through its dimmed backdrop.
        if (auto* mo = findDescendant<goaui::ModOverlay> (ed.get());
            mo != nullptr && mo->isVisible())
            if (auto* mb = findButton (ed.get(), "MOD"))
                press (*mb);

        auto countAccent = [&ed] (juce::Component& c)
        {
            // Snapshot regions are in EDITOR coordinates; convert.
            const auto area = ed->getLocalArea (&c, c.getLocalBounds());
            const juce::Image shot = ed->createComponentSnapshot (
                area, false, 1.0f, juce::SoftwareImageType());
            const auto want = goaui::accent;
            int hits = 0;
            for (int y = 0; y < shot.getHeight(); ++y)
                for (int x = 0; x < shot.getWidth(); ++x)
                {
                    const auto px = shot.getPixelAt (x, y);
                    const int dr = (int) px.getRed()   - (int) want.getRed();
                    const int dg = (int) px.getGreen() - (int) want.getGreen();
                    const int db = (int) px.getBlue()  - (int) want.getBlue();
                    if (dr * dr + dg * dg + db * db < 100 * 100)
                        ++hits;
                }
            return hits;
        };

        const int first = countAccent (*prand);
        // Direct state flip: triggerClick() needs a dispatch loop, which this
        // harness deliberately never runs.
        prand->btn.setToggleState (! prand->btn.getToggleState(),
                                   juce::sendNotification);
        const int second = countAccent (*prand);
        prand->btn.setToggleState (! prand->btn.getToggleState(),
                                   juce::sendNotification);   // restore

        EXPECT (juce::jmax (first, second) > 20
                    && juce::jmax (first, second) > juce::jmin (first, second) * 2,
                "toggle strips render as mini-switches (thumb ink flips with state)");
    }

    // ---- preset browser ----------------------------------------------------
    {
        auto* browseBtn = findButton (ed.get(), "BROWSE");
        auto* browser = findDescendant<goaui::PresetBrowserOverlay> (ed.get());
        EXPECT (browseBtn != nullptr && browser != nullptr, "browser + button found");
        if (browseBtn != nullptr && browser != nullptr)
        {
            // ---- read the visible list through the public tooltip hook --------
            // `rows` is private, but getTooltipForRow() is part of ListBoxModel
            // and its first line is always the row label. Headers return an
            // EMPTY tooltip, which is exactly how they are told apart from
            // presets - so the test never has to reach inside the overlay.
            auto rowLabel = [browser] (int row) -> juce::String
            {
                return browser->getTooltipForRow (row)
                    .upToFirstOccurrenceOf ("\n", false, false);
            };
            auto isHeader = [browser] (int row)
            {
                return browser->getTooltipForRow (row).isEmpty();
            };
            auto bodyCount = [&]
            {
                int n = 0;
                for (int i = 0; i < browser->getNumRows(); ++i)
                    if (! isHeader (i))
                        ++n;
                return n;
            };
            auto firstBodyRow = [&]
            {
                for (int i = 0; i < browser->getNumRows(); ++i)
                    if (! isHeader (i))
                        return i;
                return -1;
            };
            auto bodyLabels = [&]
            {
                juce::StringArray out;
                for (int i = 0; i < browser->getNumRows(); ++i)
                    if (! isHeader (i))
                        out.add (rowLabel (i));
                return out;
            };
            auto findRow = [&] (const juce::String& name)
            {
                for (int i = 0; i < browser->getNumRows(); ++i)
                    if (! isHeader (i) && rowLabel (i) == name)
                        return i;
                return -1;
            };
            // Type into the search box the way a user does: setText with
            // dontSendNotification is what the restore path uses, so the test
            // fires the change callback itself.
            auto typeSearch = [browser] (const juce::String& s)
            {
                browser->setSearchText (s);
                browser->search.onTextChange();
            };

            press (*browseBtn);
            expectOnScreen (*browser, "preset browser on screen");
            expectChildLaidOut (*browser, "browser controls laid out");
            // Folder tabs must have real bounds too (list + tabs are the point).
            if (auto* tab = findButton (browser, "FACTORY"))
                EXPECT (tab->getWidth() > 0 && tab->isVisible(),
                        "browser FACTORY tab visible with bounds");

            const int totalBody = bodyCount();
            EXPECT (totalBody > 0, "browser lists presets");
            EXPECT (browser->folder() == 0 && browser->sortMode() == 0,
                    "browser opens on ALL / A-Z by default");

            // The count label must agree with what is actually listed, or it is
            // worse than no label at all.
            EXPECT (browser->count.getText()
                        == juce::String (totalBody) + (totalBody == 1 ? " preset" : " presets"),
                    "count label agrees with the listed rows: "
                        + browser->count.getText());

            // Bank headers (FACTORY / USER PATCHES) must be present: the two
            // banks have to stay visually separated under every sort mode.
            bool sawHeader = false;
            for (int i = 0; i < browser->getNumRows(); ++i)
                sawHeader = sawHeader || isHeader (i);
            EXPECT (sawHeader, "browser groups the banks under headers");

            // README/site illustration: docs/assets/preset-browser.png. Capture
            // the whole editor (dimmed synth behind, overlay on top) to match the
            // composition of the committed asset. FACTORY is selected first so the
            // shot shows the factory bank rather than this run's sandbox test
            // patches, and the folder is restored afterwards so the assertions
            // below still see the ALL view they expect.
            if (wantBrowserShot)
            {
                browser->setFolder (1);            // FACTORY
                browser->list.selectRow (0, false, true);
                browser->list.scrollToEnsureRowIsOnscreen (0);
                juce::Thread::sleep (60);
                const juce::Image shot = ed->createComponentSnapshot (
                    ed->getLocalBounds(), false, 1.0f, juce::SoftwareImageType());
                juce::PNGImageFormat png;
                juce::MemoryOutputStream mo;
                const bool encoded = png.writeImageToStream (shot, mo);
                const auto f = juce::File (GOASYNTH_DOCS_ASSETS "/preset-browser.png");
                const bool written = encoded
                                     && f.replaceWithData (mo.getData(), mo.getDataSize());
                std::printf ("[shot] %s (%s, %dx%d)\n", written ? "written" : "FAILED",
                             (const char*) f.getFullPathName().toRawUTF8(),
                             shot.getWidth(), shot.getHeight());
                EXPECT (f.existsAsFile() && f.getSize() > 20000,
                        "preset-browser screenshot written");
                browser->setFolder (0);            // back to ALL
            }

            {
                const int r0 = firstBodyRow();
                const juce::String tip = browser->getTooltipForRow (r0);
                EXPECT (tip.contains ("click to audition")
                            && tip.contains ("double-click to load and close"),
                        "row tooltip explains click vs double-click");
                EXPECT (tip.contains ("right-click"),
                        "row tooltip advertises the right-click menu");
                // Headers are the only rows with no tooltip, and the A-Z view
                // opens with the FACTORY header at the top.
                EXPECT (isHeader (0) && browser->getTooltipForRow (0).isEmpty(),
                        "the list opens with the FACTORY header (no tooltip)");
            }

            {
                juce::String hit;
                const int pairs = countOverlaps (*ed, "browser open", hit);
                std::printf ("[layout] preset browser open -> %d overlapping sibling pair(s)"
                             " across %d components\n", pairs, countVisibleNodes (*browser));
                EXPECT (pairs == 0, "no overlap inside the preset browser"
                                    + (hit.isNotEmpty() ? juce::String (": ") + hit
                                                        : juce::String()));
            }

            // ---- audition: click loads but LEAVES THE BROWSER OPEN ------------
            // This is the headline behaviour change. The old browser closed on a
            // single click, so comparing two patches meant reopening every time.
            {
                const int rz = findRow ("ZZ_TEST_PATCH");
                EXPECT (rz >= 0, "seeded user patch is listed in the browser");
                if (rz >= 0)
                {
                    const double before = patchFingerprint (proc);
                    // x = 100: past the star column (28) and clear of the tag
                    // column, i.e. the name area - the plain-click path.
                    browser->listBoxItemClicked (
                        rz, makeRowClick (browser->list, 100, 0, false));
                    EXPECT (browser->isVisible(),
                            "plain click auditions and leaves the browser open");
                    EXPECT (std::fabs (patchFingerprint (proc) - before) > 1.0e-9,
                            "plain click actually loaded the patch");
                    EXPECT (anyLabelReads (ed.get(), "ZZ_TEST_PATCH"),
                            "the header name follows the auditioned patch");
                }
            }

            // ---- accept: double-click loads AND closes -----------------------
            {
                const int rz = findRow ("ZZ_TEST_PATCH");
                if (rz >= 0)
                {
                    browser->listBoxItemDoubleClicked (
                        rz, makeRowClick (browser->list, 100, 0, false, 2));
                    EXPECT (! browser->isVisible(),
                            "double-click loads the patch and closes the browser");
                    EXPECT (anyLabelReads (ed.get(), "ZZ_TEST_PATCH"),
                            "the accepted patch is the loaded one");
                }
            }

            // ---- ESC closes without rolling back ----------------------------
            // Closing must never undo the audition: losing the patch you just
            // chose is worse than any leftover. Two paths, because the search
            // box swallows ESC (TextEditor::consumeEscAndReturnKeys) and the
            // overlay's keyPressed() only sees it when the list has focus.
            {
                press (*browseBtn);
                EXPECT (browser->isVisible(), "browser reopens");

                const int ra = findRow ("AA_TEST_PATCH");
                if (ra >= 0)
                    browser->listBoxItemClicked (
                        ra, makeRowClick (browser->list, 100, 0, false));
                const double auditioned = patchFingerprint (proc);

                browser->search.onEscapeKey();   // the real search-box path
                EXPECT (! browser->isVisible(),
                        "ESC from the search box closes the browser");
                EXPECT (std::fabs (patchFingerprint (proc) - auditioned) < 1.0e-12,
                        "closing never rolls back - the auditioned patch stays loaded");

                press (*browseBtn);
                EXPECT (browser->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)),
                        "overlay handles ESC when the list has focus");
                EXPECT (! browser->isVisible(), "ESC via keyPressed closes the browser");
            }

            // ---- Return accepts the highlighted row -------------------------
            {
                press (*browseBtn);
                const int rm = findRow ("MM_TEST_PATCH");
                EXPECT (rm >= 0, "MM_TEST_PATCH listed");
                if (rm >= 0)
                {
                    browser->list.selectRow (rm, false, true);
                    browser->search.onReturnKey();
                    EXPECT (! browser->isVisible(),
                            "Return from the search box takes the preset and closes");
                    EXPECT (anyLabelReads (ed.get(), "MM_TEST_PATCH"),
                            "Return loaded the highlighted preset");
                }
            }

            // ---- arrow keys walk the list and skip headers ------------------
            {
                press (*browseBtn);
                EXPECT (browser->keyPressed (juce::KeyPress (juce::KeyPress::downKey)),
                        "overlay handles the down arrow while search has focus");
                const int sel = browser->list.getSelectedRow();
                EXPECT (sel >= 0, "down arrow moves the list cursor onto a row");
                EXPECT (sel < 0 || ! isHeader (sel),
                        "down arrow never lands on a group header");
                browser->keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
                EXPECT (! browser->isVisible(), "browser closed again");
            }

            // ---- search reaches into the TAGS, not just the name ------------
            // "testtag" is on all three seeded patches and in NO preset name, so
            // a hit can only come from the tag half of the haystack. This is the
            // exact case the old name-only search failed.
            {
                press (*browseBtn);
                const int all = bodyCount();

                typeSearch ("testtag");
                EXPECT (bodyCount() == 3,
                        "search matches a tag that appears in no preset name");
                EXPECT (browser->count.getText().startsWith ("3 of "),
                        "count label switches to 'N of M' when filtered: "
                            + browser->count.getText());

                typeSearch ("test_patch");
                const int nName = bodyCount();
                EXPECT (nName == 3, "search on a name fragment finds the seeded patches");
                typeSearch ("test_patch alphatag");
                EXPECT (bodyCount() == 1 && rowLabel (firstBodyRow()) == "AA_TEST_PATCH",
                        "a second term ANDs rather than widens (1 of 3)");

                typeSearch ("test_patch zzznope");
                EXPECT (bodyCount() == 0, "an unmatched term yields an empty list");
                EXPECT (browser->count.getText().startsWith ("0 of "),
                        "empty result is reported in the count label");
                // Exercise the empty-state paint branch (rows.empty()).
                const juce::Image shot =
                    browser->createComponentSnapshot (browser->getLocalBounds());
                EXPECT (shot.isValid(),
                        "empty-state paint path runs without crashing");

                typeSearch ("alphatag");
                EXPECT (bodyCount() == 1 && rowLabel (firstBodyRow()) == "AA_TEST_PATCH",
                        "tag-only search narrows to exactly the tagged patch");

                typeSearch ("");
                EXPECT (bodyCount() == all, "clearing the search restores the full list");
            }

            // ---- sort modes, checked inside the USER bank -------------------
            // The user bank had NO defined order before this - it came straight
            // out of findChildFiles(), so it differed per machine and per run.
            {
                const juce::StringArray asc { "AA_TEST_PATCH", "MM_TEST_PATCH",
                                              "ZZ_TEST_PATCH" };
                const juce::StringArray desc { "ZZ_TEST_PATCH", "MM_TEST_PATCH",
                                               "AA_TEST_PATCH" };

                browser->setSort (0);
                browser->setFolder (2);   // setFolder fires the rebuild
                EXPECT (browser->folder() == 2 && browser->sortMode() == 0,
                        "USER tab + A-Z selected");
                EXPECT (bodyLabels() == asc,
                        "A-Z orders the user bank ascending: "
                            + bodyLabels().joinIntoString (","));

                browser->setSort (1);
                browser->setFolder (2);
                EXPECT (bodyLabels() == desc,
                        "Z-A reverses the user bank: "
                            + bodyLabels().joinIntoString (","));

                browser->setSort (3);     // BANK ORDER
                browser->setFolder (2);
                auto bankOrder = bodyLabels();
                bankOrder.sort (true);
                EXPECT (bankOrder == asc,
                        "BANK ORDER lists the same three patches (order preserved)");

                browser->setSort (2);     // NEWEST
                browser->setFolder (2);
                EXPECT (bodyCount() == 3, "NEWEST lists the whole user bank");

                browser->setSort (0);
                browser->setFolder (0);
                EXPECT (browser->folder() == 0, "back on the ALL tab");
            }

            // ---- tag column click sets the tag filter -----------------------
            {
                const int ra = findRow ("AA_TEST_PATCH");
                EXPECT (ra >= 0, "AA_TEST_PATCH listed for the tag-click test");
                if (ra >= 0)
                {
                    // The hit-test band is shared with the painter (tagColumnWidth),
                    // so clicking where the tags are drawn must filter by them.
                    // The tag taken is the one drawn LEFTMOST on the row, which for
                    // the seeded patches is "testtag" - carried by all three, so
                    // the filter narrows the 139-patch bank to exactly those.
                    const int x = browser->list.getWidth() - 5;
                    browser->listBoxItemClicked (
                        ra, makeRowClick (browser->list, x, 0, false));
                    EXPECT (browser->tagCombo.getSelectedItemIndex() > 0,
                            "clicking the tag column sets the tag filter");
                    EXPECT (browser->tagCombo.getText().equalsIgnoreCase ("testtag"),
                            "the filter took the tag drawn leftmost on the row: "
                                + browser->tagCombo.getText());

                    auto tagged = bodyLabels();
                    tagged.sort (true);
                    EXPECT (tagged == juce::StringArray ({ "AA_TEST_PATCH",
                                                           "MM_TEST_PATCH",
                                                           "ZZ_TEST_PATCH" }),
                            "the tag filter narrows to exactly the patches with that tag: "
                                + tagged.joinIntoString (","));

                    browser->setTagText ("");
                    browser->setFolder (0);
                    EXPECT (bodyCount() == totalBody,
                            "clearing the tag filter restores the full list");
                }
            }

            // ---- right-click routes to the row menu, not to loading ---------
            // The editor owns the menu (and opens a native popup, which this
            // harness must not spin up), so the callback is swapped for a probe:
            // what is under test is the click routing, not the popup itself.
            {
                const int r0 = firstBodyRow();
                bool menuCalled = false;
                int menuIdx = -1;
                const double before = patchFingerprint (proc);
                auto saved = browser->onRowMenu;
                browser->onRowMenu = [&] (int i, juce::Point<int>)
                {
                    menuCalled = true;
                    menuIdx = i;
                };
                browser->listBoxItemClicked (
                    r0, makeRowClick (browser->list, 100, 0, true));
                browser->onRowMenu = saved;

                EXPECT (menuCalled, "right-click reports a row-menu gesture");
                EXPECT (menuIdx >= 0, "the row menu knows which preset was clicked");
                EXPECT (browser->isVisible()
                            && std::fabs (patchFingerprint (proc) - before) < 1.0e-12,
                        "right-click neither loads the patch nor closes the browser");
            }

            // ---- favourites: star column, FAVOURITES tab, persistence -------
            {
                const int rz = findRow ("ZZ_TEST_PATCH");
                EXPECT (rz >= 0, "ZZ_TEST_PATCH listed for the favourite test");
                if (rz >= 0)
                {
                    // x = 10 is inside the star column (x < 28).
                    browser->listBoxItemClicked (
                        rz, makeRowClick (browser->list, 10, 0, false));
                    browser->setFolder (3);
                    EXPECT (bodyCount() == 1
                                && rowLabel (firstBodyRow()) == "ZZ_TEST_PATCH",
                            "starring a row puts it in FAVOURITES");

                    EXPECT (userpresets::browserStateFile().existsAsFile(),
                            "browser.json written inside the sandbox");
                    const auto st = userpresets::loadBrowserState();
                    EXPECT (st.favourites.size() == 1,
                            "the favourite was persisted, not just held in memory");
                    EXPECT (st.favourites[0].startsWith ("u:"),
                            "a user patch is keyed by file name: "
                                + (st.favourites.isEmpty() ? juce::String ("<none>")
                                                           : st.favourites[0]));

                    browser->setFolder (0);
                    const int rz2 = findRow ("ZZ_TEST_PATCH");
                    if (rz2 >= 0)
                        browser->listBoxItemClicked (
                            rz2, makeRowClick (browser->list, 10, 0, false));
                    browser->setFolder (3);
                    EXPECT (bodyCount() == 0,
                            "clicking the star again removes it from FAVOURITES");

                    browser->setFolder (0);
                    EXPECT (bodyCount() == totalBody,
                            "the full bank is back after the favourite round-trip");
                }
            }

            // Layout must still hold with a filter applied and rows on screen.
            {
                typeSearch ("acid");
                juce::String hit;
                const int pairs = countOverlaps (*ed, "browser filtered", hit);
                std::printf ("[layout] preset browser filtered -> %d overlapping pair(s)\n",
                             pairs);
                EXPECT (pairs == 0, "no overlap inside the filtered preset browser"
                                    + (hit.isNotEmpty() ? juce::String (": ") + hit
                                                        : juce::String()));
                EXPECT (bodyCount() > 0, "the 'acid' filter matched factory patches");
                typeSearch ("");
            }

            press (*browseBtn);
            EXPECT (! browser->isVisible(), "preset browser closes");
        }
    }

    // ---- AI designer -------------------------------------------------------
    {
        auto* aiBtn = findButton (ed.get(), "AI");
        auto* ai = findDescendant<goaui::AiOverlay> (ed.get());
        EXPECT (aiBtn != nullptr && ai != nullptr, "AI overlay + button found");
        if (aiBtn != nullptr && ai != nullptr)
        {
            press (*aiBtn);
            expectOnScreen (*ai, "AI overlay on screen");
            expectChildLaidOut (*ai, "AI controls laid out");
            {
                juce::String hit;
                const int pairs = countOverlaps (*ed, "AI open", hit);
                std::printf ("[layout] AI designer open -> %d overlapping sibling pair(s)"
                             " across %d components\n", pairs, countVisibleNodes (*ai));
                EXPECT (pairs == 0, "no overlap inside the AI designer"
                                    + (hit.isNotEmpty() ? juce::String (": ") + hit
                                                        : juce::String()));
            }
            press (*aiBtn);
            EXPECT (! ai->isVisible(), "AI overlay closes");
        }
    }

    // ---- save dialog (opens via SAVE; CANCEL must close) -------------------
    {
        auto* saveBtn = findButton (ed.get(), "SAVE");
        auto* save = findDescendant<goaui::SavePresetOverlay> (ed.get());
        EXPECT (saveBtn != nullptr && save != nullptr, "save overlay + button found");
        if (saveBtn != nullptr && save != nullptr)
        {
            press (*saveBtn);
            expectOnScreen (*save, "save overlay on screen");
            expectChildLaidOut (*save, "save controls laid out");
            {
                juce::String hit;
                const int pairs = countOverlaps (*ed, "save open", hit);
                std::printf ("[layout] save dialog open -> %d overlapping sibling pair(s)"
                             " across %d components\n", pairs, countVisibleNodes (*save));
                EXPECT (pairs == 0, "no overlap inside the save dialog"
                                    + (hit.isNotEmpty() ? juce::String (": ") + hit
                                                        : juce::String()));
            }
            if (save->isVisible())
            {
                if (auto* cancel = findButton (save, "CANCEL"))
                {
                    press (*cancel);
                    EXPECT (! save->isVisible(), "save overlay CANCEL closes");
                }
                else
                {
                    EXPECT (false, "save CANCEL button found");
                }
            }
        }
    }

    // ---- licence / activation screen ---------------------------------------
    // The one overlay with no ordinary header button: it is forced full-screen
    // when unlicensed and "peeked" from the trial badge otherwise. This session
    // is activated, so the badge is hidden - but its onClick is the real open
    // path, so drive that handler exactly as a user click would.
    {
        auto* licence = findDescendant<goaui::LicenseOverlay> (ed.get());
        auto* badge = findButtonByTooltip (ed.get(), "Time left in your trial");
        EXPECT (licence != nullptr && badge != nullptr,
                "licence overlay + trial badge found");
        if (licence != nullptr && badge != nullptr)
        {
            press (*badge);
            expectOnScreen (*licence, "licence overlay on screen");
            expectChildLaidOut (*licence, "licence controls laid out");
            {
                juce::String hit;
                const int pairs = countOverlaps (*ed, "licence open", hit);
                std::printf ("[layout] licence screen open -> %d overlapping sibling pair(s)"
                             " across %d components\n", pairs, countVisibleNodes (*licence));
                EXPECT (pairs == 0, "no overlap inside the licence screen"
                                    + (hit.isNotEmpty() ? juce::String (": ") + hit
                                                        : juce::String()));
            }

            // A peek (allowClose = true) must show its × and that × must close
            // it - otherwise the user is stuck on the activation screen.
            if (auto* x = findButton (licence, "\u00d7"))
            {
                EXPECT (x->isVisible(), "licence peek shows its close button");
                press (*x);
                EXPECT (! licence->isVisible(), "licence peek closes via \u00d7");
            }
            else
            {
                EXPECT (false, "licence peek close button found");
            }
        }
    }

    // ---- unit-map sweep: every knob's parameter must format with a known
    // unit (or be a combo, which shows its selection text). Driven by the
    // controls the editor actually builds, so new knobs join automatically.
    {
        struct Sweep
        {
            static void walk (GoaSynthAudioProcessor& proc, juce::Component* c,
                              int& checked, int& bad, juce::String& firstBad)
            {
                if (c == nullptr)
                    return;
                if (auto* ctl = dynamic_cast<goaui::Ctl*> (c))
                {
                    ++checked;
                    if (ctl->useCombo)
                    {
                        if (ctl->combo.getNumItems() == 0)
                        { ++bad; if (firstBad.isEmpty()) firstBad = ctl->paramId + " (empty combo)"; }
                    }
                    else
                    {
                        // Full form must be <number> <known unit> OR a pure
                        // integer (counts: voices, poly, bend range). A decimal
                        // with no unit is exactly the bug this sweep exists for.
                        // Compact form must be non-empty (e.g. "1.5k", "250m").
                        const juce::String full =
                            goaui::liveValueText (proc.apvts, ctl->paramId);
                        const juce::String compact =
                            goaui::liveValueText (proc.apvts, ctl->paramId, true);
                        static const char* units[] = { "Hz", "ms", "ct", "dB",
                                                       "s", "%", "oct" };
                        bool ok = compact.isNotEmpty();
                        if (ok)
                        {
                            ok = false;
                            for (auto* u : units)
                                if (full.endsWith (u)) { ok = true; break; }
                            if (! ok)   // pure integer (optionally signed)?
                            {
                                const juce::String t = full.trim();
                                const juce::String digits =
                                    t.startsWith ("-") || t.startsWith ("+")
                                        ? t.substring (1) : t;
                                ok = digits.isNotEmpty()
                                     && digits.containsOnly ("0123456789");
                            }
                        }
                        if (! ok)
                        {
                            ++bad;
                            if (firstBad.isEmpty())
                                firstBad = ctl->paramId + " -> \"" + full + "\"";
                            std::printf ("  unknown unit: %s -> \"%s\"\n",
                                         (const char*) ctl->paramId.toRawUTF8(),
                                         (const char*) full.toRawUTF8());
                        }
                    }
                }
                for (auto* ch : c->getChildren())
                    walk (proc, ch, checked, bad, firstBad);
            }
        };
        int checked = 0, bad = 0;
        juce::String firstBad;
        Sweep::walk (proc, ed.get(), checked, bad, firstBad);
        std::printf ("[sweep] %d knobs checked\n", checked);
        EXPECT (checked >= 60, "unit sweep covers all knobs");
        EXPECT (bad == 0, "every knob formats with a known unit"
                          + (firstBad.isNotEmpty() ? juce::String (": ") + firstBad
                                                   : juce::String()));
    }

    // ---- unit correctness for the master EQ ---------------------------------
    // The sweep above only proves a readout ends in a KNOWN unit, so a gain that
    // fell through to the 0..1 "%" fallback would read "-1500 %" and still pass.
    // These are the parameters where "known" and "right" differ.
    {
        auto readFull = [&] (const char* id)
        { return goaui::liveValueText (proc.apvts, id, false); };

        auto set01 = [&] (const char* id, float normalised)
        {
            if (auto* p = proc.apvts.getParameter (id))
                p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, normalised));
        };

        set01 (param::eqLow, 0.0f);            // range is -15..+15 dB
        const juce::String lo = readFull (param::eqLow);
        EXPECT (lo.endsWith ("dB"), "EQ low gain reads in dB (got \"" + lo + "\")");

        // A neutral band must not read "-0.0": the normalised->dB conversion
        // lands a hair below zero, so the readout snaps near-zero to zero.
        // (Also pins the range's shape: 0 dB has to sit at the centre of the
        // knob, which a skewed range would move to ~70% of the travel.)
        set01 (param::eqLow, 0.5f);
        const juce::String mid = readFull (param::eqLow);
        EXPECT (mid == "0.0 dB", "a flat EQ band reads as 0.0 dB at knob centre (got \"" + mid + "\")");

        set01 (param::eqHigh, 1.0f);
        const juce::String hi = readFull (param::eqHigh);
        EXPECT (hi.endsWith ("dB") && ! hi.startsWith ("-"),
                "EQ high gain reads as a positive dB value (got \"" + hi + "\")");

        set01 (param::eqMidFreq, 0.5f);
        const juce::String mf = readFull (param::eqMidFreq);
        EXPECT (mf.endsWith ("Hz"), "EQ mid frequency reads in Hz (got \"" + mf + "\")");

        // Pulse width is a 0..1 amount, so a percentage is the honest unit.
        set01 (param::pulseWidth, 0.5f);
        const juce::String pw = readFull (param::pulseWidth);
        EXPECT (pw.endsWith ("%"), "pulse width reads as a percentage (got \"" + pw + "\")");

        // Put the EQ back to a bypass state so later phases see a neutral patch.
        set01 (param::eqLow, 0.5f);            // 0 dB is the middle of -15..+15
        set01 (param::eqHigh, 0.5f);
        set01 (param::eqMidFreq, proc.apvts.getParameter (param::eqMidFreq)
                                     ->getDefaultValue());
        set01 (param::pulseWidth, proc.apvts.getParameter (param::pulseWidth)
                                      ->getDefaultValue());
    }

    // ---- extremes sweep: drive every knob to its min and max and check the
    // compact readout still fits the on-knob band (text width vs the band
    // minus the space any mod dots would claim). Catches "20000 Hz"-style
    // overflows that only appear at the ends of a range.
    {
        juce::Image scratch (juce::Image::ARGB, 8, 8, true);
        juce::Graphics meas (scratch);   // used only for string measuring
        int driven = 0, over = 0;
        juce::String firstOver;

        struct Extremes
        {
            static void walk (GoaSynthAudioProcessor& proc, juce::Component* c,
                              juce::Graphics& meas, int& driven, int& over,
                              juce::String& firstOver)
            {
                if (c == nullptr)
                    return;
                if (auto* ctl = dynamic_cast<goaui::Ctl*> (c);
                    ctl != nullptr && ! ctl->useCombo)
                {
                    if (auto* p = proc.apvts.getParameter (ctl->paramId))
                    {
                        const auto& range = p->getNormalisableRange();
                        // The draw code trims for the dots actually present;
                        // drive the slot to one active dot (the common case)
                        // so the band matches what the UI really shows.
                        proc.apvts.getParameter (param::modAmt (0))
                            ->setValueNotifyingHost (0.75f);   // +0.5 = one dot
                        const int dotsReserve = 1 * 8 + 3;
                        const int bandWidth = juce::jmax (10, ctl->getWidth() - dotsReserve);

                        for (int end = 0; end < 2; ++end)
                        {
                            const float plain = end == 0
                                ? range.convertFrom0to1 (0.0f)   // min
                                : range.convertFrom0to1 (1.0f);  // max
                            p->beginChangeGesture();
                            p->setValueNotifyingHost (end == 0 ? 0.0f : 1.0f);
                            p->endChangeGesture();
                            ++driven;

                            const juce::String txt =
                                goaui::liveValueText (proc.apvts, ctl->paramId, true);
                            const int textW = meas.getCurrentFont()
                                                  .withHeight (7.0f)
                                                  .getStringWidth (txt);
                            if (textW > bandWidth)
                            {
                                ++over;
                                if (firstOver.isEmpty())
                                    firstOver = ctl->paramId + " \"" + txt + "\" ("
                                                + juce::String (textW) + "px > "
                                                + juce::String (bandWidth) + "px)";
                                std::printf ("  overflow: %s at %s -> \"%s\" needs %dpx, band %dpx (ctl %dpx)\n",
                                             (const char*) ctl->paramId.toRawUTF8(),
                                             end == 0 ? "min" : "max",
                                             (const char*) txt.toRawUTF8(),
                                             textW, bandWidth, ctl->getWidth());
                            }
                        }
                        p->setValueNotifyingHost (p->getDefaultValue());   // restore
                    }
                }
                for (auto* ch : c->getChildren())
                    walk (proc, ch, meas, driven, over, firstOver);
            }
        };
        // Leave the slot as we found it (muted).
        struct Cleanup
        {
            static void mute (GoaSynthAudioProcessor& proc)
            {
                if (auto* p = proc.apvts.getParameter (param::modAmt (0)))
                    p->setValueNotifyingHost (0.5f);   // amount 0
            }
        };
        Extremes::walk (proc, ed.get(), meas, driven, over, firstOver);
        Cleanup::mute (proc);
        std::printf ("[sweep] %d knob extremes driven\n", driven);
        EXPECT (driven >= 120, "extremes sweep covers both ends of every knob");
        EXPECT (over == 0, "compact readouts fit the band at min and max"
                           + (firstOver.isNotEmpty() ? juce::String (": ") + firstOver
                                                     : juce::String()));
    }

    // ---- bounds sweep: after resized(), every visible, mouse-reachable
    // component must have non-degenerate bounds. This is the net that catches
    // "created but never laid out" bugs (F-DRIVE/F-FB shipped at width 0 for
    // weeks; the NOISE knob was once squeezed to a 17px sliver).
    // Legitimately zero-sized things are skipped explicitly: hidden overlays,
    // panels whose knobs are laid out on top of them, and sub-widget children
    // that only exist inside their Ctl/ToggleCtl (already covered there).
    {
        int checked = 0, zero = 0;
        juce::String firstZero;
        std::function<void (juce::Component*)> walkBounds =
            [&] (juce::Component* c)
        {
            if (c == nullptr)
                return;
            for (auto* ch : c->getChildren())
            {
                // Overlays start hidden; they get bounds at open time and the
                // overlay tests above already assert they lay out on show.
                if (dynamic_cast<goaui::ModOverlay*> (ch) != nullptr
                    || dynamic_cast<goaui::AiOverlay*> (ch) != nullptr
                    || dynamic_cast<goaui::SavePresetOverlay*> (ch) != nullptr
                    || dynamic_cast<goaui::PresetBrowserOverlay*> (ch) != nullptr
                    || dynamic_cast<goaui::AboutOverlay*> (ch) != nullptr
                    || dynamic_cast<goaui::UpdateResultOverlay*> (ch) != nullptr
                    || dynamic_cast<goaui::LicenseOverlay*> (ch) != nullptr)
                    continue;

                if (ch->isVisible() && ch->getWidth() > 0 && ch->getHeight() > 0)
                {
                    ++checked;
                    walkBounds (ch);
                }
                else if (ch->isVisible())
                {
                    // Visible with a degenerate rect: the bug class itself.
                    // Exception: a Ctl/ToggleCtl/Panel is a *container* whose
                    // knobs are positioned independently over it; the frame
                    // may legitimately be zero-sized while its knobs are not.
                    const bool container = dynamic_cast<goaui::Ctl*> (ch) != nullptr
                                        || dynamic_cast<goaui::ToggleCtl*> (ch) != nullptr
                                        || dynamic_cast<goaui::Panel*> (ch) != nullptr
                                        || dynamic_cast<goaui::WaveDisplay*> (ch) != nullptr
                                        || dynamic_cast<goaui::FilterGraph*> (ch) != nullptr
                                        || dynamic_cast<goaui::EnvGraph*> (ch) != nullptr
                                        || dynamic_cast<goaui::LfoGraph*> (ch) != nullptr
                                        || dynamic_cast<goaui::StepStrip*> (ch) != nullptr
                                        || dynamic_cast<goaui::Keyboard*> (ch) != nullptr;
                    ++zero;
                    if (auto* zc = dynamic_cast<goaui::Ctl*> (ch);
                        zc != nullptr && firstZero.isEmpty())
                        firstZero = zc->paramId;
                    if (firstZero.isEmpty())
                        firstZero = juce::String (typeid (*ch).name()) + " ("
                                    + juce::String (ch->getWidth()) + "x"
                                    + juce::String (ch->getHeight()) + ")";
                    std::printf ("  zero-sized: %s %s %dx%d%s\n",
                                 (const char*) typeid (*ch).name(),
                                 (const char*) (dynamic_cast<goaui::Ctl*> (ch) != nullptr
                                                    ? dynamic_cast<goaui::Ctl*> (ch)->paramId.toRawUTF8()
                                                    : ""),
                                 ch->getWidth(), ch->getHeight(),
                                 container ? " [container]" : "");
                    if (container)
                        --zero;   // reported, but not counted as a failure
                    walkBounds (ch);
                }
                else
                {
                    walkBounds (ch);   // hidden children may still matter later
                }
            }
        };
        walkBounds (ed.get());
        std::printf ("[sweep] %d visible components bounds-checked\n", checked);
        EXPECT (checked >= 150, "bounds sweep covers the editor surface");
        EXPECT (zero == 0, "no visible control ships with zero bounds"
                           + (firstZero.isNotEmpty() ? juce::String (": ") + firstZero
                                                     : juce::String()));
    }

    // ---- version policy: 1.x line until the owner says otherwise ----------
    // Bump rules from the owner: every functional change goes to the next
    // minor (1.2, 1.3, ...); the major stays 1. Both halves are enforced: the
    // compile-time static_asserts in PluginEditor.h catch the version macro
    // itself, and this catches the plugin binary, the About card and the web
    // feed drifting apart.
    {
        const juce::String ver = GOASYNTH_VERSION;
        const auto parts = juce::StringArray::fromTokens (ver, ".", {});
        EXPECT (parts.size() == 3 && parts[0].getIntValue() == 1,
                "GOASYNTH_VERSION must be major.minor.patch on the 1.x line, got " + ver);
        EXPECT (parts.size() == 3 && parts[1].getIntValue() >= 2,
                "GOASYNTH_VERSION minor must never regress below 1.2, got " + ver);

        // The About card must show the same version string the plugin was
        // built with (it re-reads the define at construction).
        std::function<bool (juce::Component*, const juce::String&)> anyLabelReads =
            [&] (juce::Component* c, const juce::String& text) -> bool
        {
            if (c == nullptr)
                return false;
            if (auto* l = dynamic_cast<juce::Label*> (c))
                if (l->getText() == text)
                    return true;
            for (auto* ch : c->getChildren())
                if (anyLabelReads (ch, text))
                    return true;
            return false;
        };
        const bool aboutOk = anyLabelReads (ed.get(), "VERSION " + ver)
                             && anyButtonReads (ed.get(), "COPY");
        EXPECT (aboutOk, "About overlay shows the build version (VERSION " + ver + ")");

        // And the card must lay out on open, like every other overlay.
        auto* edr = static_cast<GoaSynthAudioProcessorEditor*> (ed.get());
        goaui::AboutOverlay* about = nullptr;
        std::function<void (juce::Component*)> findAbout = [&] (juce::Component* c)
        {
            if (about != nullptr)
                return;
            if (auto* a = dynamic_cast<goaui::AboutOverlay*> (c))
            {
                about = a;
                return;
            }
            for (auto* ch : c->getChildren())
                findAbout (ch);
        };
        findAbout (edr);
        EXPECT (about != nullptr, "editor owns an About overlay");
        if (about != nullptr)
        {
            about->setBounds (edr->getLocalBounds());
            about->setVisible (true);
            about->resized();
            const bool aboutLaid = about->copyBtn.getWidth() > 20
                                   && about->verLine.getWidth() > 100
                                   && about->idValue.getHeight() > 10;
            EXPECT (aboutLaid, "About card lays out on open");
            // The X must sit ON the card where the eye looks for it: the old
            // window-corner placement read as "no close button at all".
            const bool xOnCard = about->cardBounds()
                                     .contains (about->closeBtn.getBounds());
            EXPECT (xOnCard, "About close button sits inside the card");
            about->setVisible (false);
        }

        // The update result dialog is an overlay, not a native box: the two
        // outcomes that name the download carry a clickable link (an OS box
        // cannot host one), and as this editor's child the host cannot bury
        // it. Drive all three branches exactly the way updateCheckDone does.
        goaui::UpdateResultOverlay* res = nullptr;
        std::function<void (juce::Component*)> findResult = [&] (juce::Component* c)
        {
            if (res != nullptr)
                return;
            if (auto* r = dynamic_cast<goaui::UpdateResultOverlay*> (c))
            {
                res = r;
                return;
            }
            for (auto* ch : c->getChildren())
                findResult (ch);
        };
        findResult (edr);
        EXPECT (res != nullptr, "editor owns an update-result overlay");

        if (res != nullptr)
        {
            // Branch 1 — feed unreachable: the one that used to return
            // silently, so the dialog itself is the assertion that matters.
            edr->updateReachable.store (false);
            edr->updateCheckDone();
            EXPECT (res->isVisible(),
                    "an unreachable feed shows the result dialog (it used to return silently)");
            EXPECT (res->titleLabel.getText() == "GOASYNTH",
                    "the unreachable dialog is titled GOASYNTH, got "
                        + res->titleLabel.getText());
            EXPECT (res->messageLabel.getText().contains ("Could not reach"),
                    "the unreachable dialog says the feed could not be reached");
            EXPECT (res->link.isVisible(),
                    "the unreachable dialog carries the clickable download link");
            EXPECT (res->link.getButtonText() == "https://y4m4.github.io/GoaSynth/",
                    "the link points at the product site, got " + res->link.getButtonText());
            EXPECT (res->link.getMouseCursor() == juce::MouseCursor::PointingHandCursor,
                    "the link shows a hand cursor over its whole row");
            EXPECT (! res->changelogLink.isVisible(),
                    "the unreachable branch offers no change-log link into the site it just failed to reach");
            // Every row on the card — an off-card row is a row nobody sees.
            EXPECT (res->cardBounds().contains (res->titleLabel.getBounds()),
                    "the title sits on the card");
            EXPECT (res->cardBounds().contains (res->messageLabel.getBounds()),
                    "the message sits on the card");
            EXPECT (res->cardBounds().contains (res->link.getBounds()),
                    "the link sits on the card");
            EXPECT (res->cardBounds().contains (res->okBtn.getBounds()),
                    "the OK button sits on the card");

            // Branch 2 — a newer release: the dialog proved live with the
            // temporary 1.5.0 feed, and it must name both versions — and now
            // also show what changed (the feed's release notes).
            edr->updateReachable.store (true);
            edr->updateOlder.store (true);
            edr->updateThreadVersion = "1.5.0";
            edr->updateNotes = juce::StringArray { "Faster preset browser",
                                                   "New trancegate shapes" };
            edr->updateThreadDate = "2 October 2026";
            edr->updateThreadSize = "21.9 MB";
            edr->updateCheckDone();
            EXPECT (res->titleLabel.getText() == "UPDATE AVAILABLE",
                    "a newer feed shows UPDATE AVAILABLE, got "
                        + res->titleLabel.getText());
            EXPECT (res->messageLabel.getText().contains ("1.5.0")
                        && res->messageLabel.getText().contains ("running " + ver + ")"),
                    "the dialog names the available and the running version, got "
                        + res->messageLabel.getText());
            EXPECT (res->messageLabel.getText().contains ("What's new")
                        && res->messageLabel.getText().contains ("Faster preset browser")
                        && res->messageLabel.getText().contains ("New trancegate shapes"),
                    "the update dialog shows the feed's release notes, got "
                        + res->messageLabel.getText());
            EXPECT (res->messageLabel.getText().contains ("Released 2 October 2026."),
                    "the update dialog shows when the new version shipped, got "
                        + res->messageLabel.getText());
            EXPECT (res->messageLabel.getText().contains ("Download the new installer (21.9 MB):"),
                    "the download lead-in names the installer's size, got "
                        + res->messageLabel.getText());
            EXPECT (res->link.isVisible(),
                    "the update-available dialog carries the download link");
            // The download row is the new version's installer asset, direct
            // from its GitHub release: named by its file (the full address
            // would be drawn ellipsized over the part that matters), with
            // the address itself as the link's target.
            EXPECT (res->link.getButtonText() == "GoaSynth-Setup-1.5.0.exe",
                    "the update dialog names the installer file on the row, got "
                        + res->link.getButtonText());
            EXPECT (res->link.getURL().toString (true)
                        == "https://github.com/Y4m4/GoaSynth/releases/download/v1.5.0/GoaSynth-Setup-1.5.0.exe",
                    "the update dialog links the new version's installer asset directly, got "
                        + res->link.getURL().toString (true));
            EXPECT (res->changelogLink.isVisible(),
                    "the update-available dialog carries the change-log link");
            EXPECT (res->changelogLink.getURL().toString (true)
                        == "https://y4m4.github.io/GoaSynth/#v1.5.0",
                    "the change-log link deep-links to the new version's notes, got "
                        + res->changelogLink.getURL().toString (true));
            EXPECT (res->cardBounds().contains (res->changelogLink.getBounds()),
                    "the change-log link sits on the card");

            // Degrade path: a feed without the new fields (an older site, or
            // a release whose assets the Release workflow has not attached
            // yet) must lose only the optional lines, never the dialog.
            edr->updateThreadDate.clear();
            edr->updateThreadSize.clear();
            edr->updateCheckDone();
            EXPECT (! res->messageLabel.getText().contains ("Released "),
                    "no date in the feed, no date line, got "
                        + res->messageLabel.getText());
            EXPECT (res->messageLabel.getText().contains ("Download the new installer:"),
                    "no size in the feed leaves the download lead-in bare, got "
                        + res->messageLabel.getText());

            // Branch 3 — up to date: nothing to download, so the download row
            // is hidden rather than dead furniture — but the release notes are
            // one click away.
            edr->updateOlder.store (false);
            edr->updateCheckDone();
            EXPECT (res->titleLabel.getText() == "GOASYNTH",
                    "an up-to-date build shows GOASYNTH, got "
                        + res->titleLabel.getText());
            EXPECT (res->messageLabel.getText().contains ("latest version (" + ver + ")"),
                    "the up-to-date dialog names the running version, got "
                        + res->messageLabel.getText());
            EXPECT (! res->link.isVisible(),
                    "the up-to-date dialog carries no download link (nothing to download)");
            EXPECT (res->changelogLink.isVisible(),
                    "the up-to-date dialog still offers the change log");
            EXPECT (res->changelogLink.getURL().toString (true)
                        == "https://y4m4.github.io/GoaSynth/#whats-new",
                    "up to date, the change-log link opens the whats-new section, got "
                        + res->changelogLink.getURL().toString (true));
            EXPECT (res->cardBounds().contains (res->changelogLink.getBounds()),
                    "the change-log link sits on the card");

            // The house dismissal: a click on the backdrop, outside the card,
            // closes it — same rule as every other overlay.
            res->mouseDown (makeRowClick (*res, 2, 2, false));
            EXPECT (! res->isVisible(), "clicking the backdrop dismisses the dialog");
        }

        // docs/version.json is the update-check feed. It follows the newest
        // RELEASED version (tools/make-changelog.py writes it from
        // CHANGELOG.md), which can legitimately lag the running build while
        // the next release is in development — but it may never advertise a
        // release newer than this build, and it must stay a sane version.
        const juce::File docsDir = juce::File (GOASYNTH_DOCS_ASSETS)
                                       .getParentDirectory();
        const juce::var feed = juce::JSON::parse (
            docsDir.getChildFile ("version.json").loadFileAsString());
        const juce::String latest = feed.getProperty ("latest", {}).toString();
        EXPECT (latest.isNotEmpty() && latest.containsOnly ("0123456789."),
                "docs/version.json latest (" + latest + ") must be a version");
        EXPECT (goaui::compareVersions (latest, ver) <= 0,
                "docs/version.json latest (" + latest + ") may not outrun the "
                "plugin version (" + ver + ")");
        EXPECT (feed.getProperty ("url", {}).toString().startsWith ("https://"),
                "docs/version.json must carry the product download url");

        // The same feed carries the release notes the update dialog shows:
        EXPECT (goaui::latestReleaseNotes (feed).size() > 0,
                "docs/version.json carries the released version's notes");
        // ...and the facts the update dialog names alongside them: when it
        // shipped (from the CHANGELOG heading, always known), and how big
        // its installer is. The size is probed from the GitHub release, so
        // it is legitimately absent in the window between tagging and the
        // Release workflow's landing commit — a release that does not exist
        // yet has no asset to measure, and stripping the previous version's
        // size (rather than mislabelling it) is what make-changelog.py must
        // do. Absent is fine; present, it must be a size the dialog quotes.
        EXPECT (feed.getProperty ("released", {}).toString().isNotEmpty(),
                "docs/version.json carries the released version's date");
        const juce::String installerSize = feed.getProperty ("installer_size", {}).toString();
        if (installerSize.isNotEmpty())
            EXPECT (installerSize.contains ("MB") || installerSize.contains ("kB")
                        || installerSize.contains ("GB"),
                    "docs/version.json's installer size is a size, got " + installerSize);
        else
            std::printf ("[feed] no installer_size (pre-release window): the dialog degrades\n");
        // ...and the helper is strict about what it lets through:
        const auto junkFeed = juce::JSON::parse (
            R"({"notes":["  kept ", "", "second"]})");
        const auto notes = goaui::latestReleaseNotes (junkFeed, 2);
        EXPECT (notes.size() == 2 && notes[0] == "kept" && notes[1] == "second",
                "latestReleaseNotes trims, drops empties and caps");
        EXPECT (goaui::latestReleaseNotes (juce::JSON::parse ("{}")).isEmpty(),
                "a feed without notes yields no notes");

        // Semantics of the update comparison (goaui::compareVersions, shared
        // with the plugin code): major.minor only, patch ignored, malformed
        // tokens rank as 0 — a broken feed can never outrank the running build.
        EXPECT (goaui::compareVersions ("1.4.0", "1.3.9") > 0, "1.4.0 outranks 1.3.9");
        EXPECT (goaui::compareVersions ("1.4", "1.3.9") > 0, "two-token version form works");
        EXPECT (goaui::compareVersions ("1.3.7", "1.3.10") == 0,
                "the patch field must be ignored (1.3.7 == 1.3.10)");
        EXPECT (goaui::compareVersions ("2.0", "1.9.9") > 0,
                "an older major must never outrank a newer one");
        EXPECT (goaui::compareVersions ("1.3.0", "1.3.0") == 0,
                "equal versions compare equal");
        EXPECT (goaui::compareVersions ("nonsense", "1.2.0") < 0,
                "a malformed token must rank as 0, never above the build");
    }

    std::printf (fails == 0 ? "ALL PASSED\n" : "%d FAILURE(S)\n", fails);
    return fails == 0 ? 0 : 1;
}
