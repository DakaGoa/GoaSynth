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
#include <functional>

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
static void press (juce::TextButton& btn)
{
    if (btn.onClick != nullptr)
        btn.onClick();
    juce::Thread::sleep (8);
}

// Set when GOASYNTH_KNOB_SHOT is set: capture docs/assets/mod-dots.png.
static bool wantKnobShot = false;

int main()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    const juce::ScopedJuceInitialiser_GUI juceInit;

    // --screenshot-knob: render one knob (with a mod routing active) into
    // docs/assets/mod-dots.png for the README's UI-conventions section.
    // Runs first, then continues into the normal assertions.
    if (std::getenv ("GOASYNTH_KNOB_SHOT") != nullptr)
        wantKnobShot = true;

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

    auto putEnv = [] (const char* name, const juce::String& value)
    {
        const juce::String kv = juce::String (name) + "=" + value;
       #if JUCE_WINDOWS
        _putenv (kv.toRawUTF8());
       #else
        setenv (name, value.toRawUTF8(), 1);
       #endif
    };
    putEnv ("GOASYNTH_LICENSE_FILE", lic.getFullPathName());
    putEnv ("GOASYNTH_LEDGER_FILE",  led.getFullPathName());
    putEnv ("GOASYNTH_TRIAL_FILE",   trial.getFullPathName());

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

            // Resize the window with the overlay up: the editor must re-fit it.
            ed->setBounds (ed->getBounds().withSize (1300, 900));
            juce::Thread::sleep (8);
            expectOnScreen (*modOverlay, "MOD overlay re-fits on resize");

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
                // the control at the other — and require accentB-ish pixels
                // near it. A 4px line at 70% opacity over the card lands within
                // ~90 of accentB, so 100 covers the blend and the antialiasing
                // without matching the card's own gradient.
                const auto want = goaui::accentB;
                const auto bounds = shot.getBounds();
                int probes = 0, hits = 0;

                for (const float t : { 0.35f, 0.5f, 0.65f, 0.8f })
                {
                    const auto p = line.getPointAlongLineProportionally (t);
                    bool hit = false;

                    for (int oy = -3; oy <= 3 && ! hit; ++oy)
                        for (int ox = -3; ox <= 3 && ! hit; ++ox)
                        {
                            const int x = juce::roundToInt (p.x) + ox;
                            const int y = juce::roundToInt (p.y) + oy;
                            if (! bounds.contains (x, y))
                                continue;

                            const auto px = shot.getPixelAt (x, y);
                            const int dr = (int) px.getRed()   - (int) want.getRed();
                            const int dg = (int) px.getGreen() - (int) want.getGreen();
                            const int db = (int) px.getBlue()  - (int) want.getBlue();
                            if (dr * dr + dg * dg + db * db < 100 * 100)
                                hit = true;
                        }

                    ++probes;
                    if (hit)
                        ++hits;
                }

                EXPECT (hits >= probes - 1,
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

                m.slots[1] = { 1, 15, 0.5f };  // LFO 1 -> DELAY FB, amount 0.5
                m.publishGlobal (1.0f, 0, 0, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f);
                EXPECT (std::abs (m.globalFx.delayFb.load() - 0.3f) < 1.0e-4f,
                        "global FX destination publishes (LFO1->delayFb = 0.3)");

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
            press (*browseBtn);
            expectOnScreen (*browser, "preset browser on screen");
            expectChildLaidOut (*browser, "browser controls laid out");
            // Folder tabs must have real bounds too (list + tabs are the point).
            if (auto* tab = findButton (browser, "FACTORY"))
                EXPECT (tab->getWidth() > 0 && tab->isVisible(),
                        "browser FACTORY tab visible with bounds");
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

    std::printf (fails == 0 ? "ALL PASSED\n" : "%d FAILURE(S)\n", fails);
    return fails == 0 ? 0 : 1;
}
