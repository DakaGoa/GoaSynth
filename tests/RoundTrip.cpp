// Round-trip test for the user preset system: serialize real plugin state
// (params + drawn user waves) to a .goapreset file, load it into a fresh
// plugin instance, and compare everything back.
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <set>

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "FakePlayHead.h"
#include "AiCloudGen.h"
#include "LearnedPatches.h"
#include "License.h"
#include "UserPresets.h"
#include "AiPatchGen.h"
#include "Tuning.h"
#include "Presets.h"
#include "TestMasterKey.h"   // throwaway master key for the suite (see the header)

// A valid serial for this dev machine, issued by the keygen during --init
// self-tests. Uses a raw literal so MSVC's 4100-analyser rules stay quiet.
static const char* devSerialText =
    "GOA1-2D6EFDDAA1AC30F510C8-8A1D804D0C59579935C063A3FDB5DAC8A95BF7D8266D997FD09BB4206F489D24"
    "871C07026D593274A70FB7E2D442241EC9EAD3B0D7640E44647406C09C712258625646199C35588934ED156FB"
    "562741F85217A2F393B2F57743E4495F071088C63D67DD90E3E54A52A3807EBA00F355D0ACEBF34062146BA89D"
    "488B6F805D751782D8822E172CEAB6FC988462D1EC99C16952A557EE646074C4180FA16A99BC21134EACADBB8"
    "3139053BDEC1ACCAB2713810E27615C9D7AD8C9D782232EF37A36AC94EE387085C10D3595D25CBC87AEF19F78A"
    "6F6C3076406C17857025C7FC8A2EFAA6BC6F7B01C4117067AE67AA45A4A3CCB9E31D325379A6A655DECB73341A";

int main()
{
    // Unbuffered stdout: a crash must leave the log showing how far the run
    // got. Redirected stdout is block-buffered, so a failure used to lose
    // every line printed up to that point.
    std::setvbuf (stdout, nullptr, _IONBF, 0);

    const juce::ScopedJuceInitialiser_GUI juceInit;

    int fails = 0;

    // Progress markers: prints the block about to run, so a hard crash (this
    // test has died with 0xc0000409 intermittently) names its own location.
    auto phase = [] (const char* what) { std::printf ("[phase] %s\n", what); };

    // The linker grants this test a 16 MB main stack (see CMakeLists.txt): the
    // factory-preset table's static initializer and the phase render buffers
    // together overflowed the default 1 MB Windows main-thread stack.

    // ---- license sandbox: activate once up front so every processor below
    // renders; the dedicated licensing block re-tests all paths explicitly.
    // Real license storage is never touched (env overrides point at temp files).
    const juce::File lic = juce::File::getSpecialLocation (juce::File::tempDirectory)
                               .getChildFile ("goasynth_test_lic.txt");
    const juce::File led = juce::File::getSpecialLocation (juce::File::tempDirectory)
                               .getChildFile ("goasynth_test_led.txt");
    const juce::File trial = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                 .getChildFile ("goasynth_test_trial.txt");
    lic.deleteFile();
    led.deleteFile();
    trial.deleteFile();

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

    // Preset-bank sandbox. Several blocks below save presets into the bank to
    // exercise the scan / shadow / shared rules. Without these overrides those
    // writes land in the developer's real %APPDATA%/GoaSynth/Presets and, for
    // the shared-bank block, in C:\Users\Public\Documents\GoaSynth - which is
    // machine-wide, so a crash would leave junk patches for every account.
    const juce::File bankRoot = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                    .getChildFile ("goasynth_roundtrip_test_root");
    bankRoot.deleteRecursively();
    const juce::File bank = bankRoot.getChildFile ("Presets");
    bank.createDirectory();
    const juce::File shared = bankRoot.getChildFile ("Shared");
    shared.createDirectory();
    putEnv ("GOASYNTH_PRESET_DIR", bank.getFullPathName());
    putEnv ("GOASYNTH_SHARED_PRESET_DIR", shared.getFullPathName());

    if (userpresets::presetsDir() != bank || userpresets::sharedPresetsDir() != shared)
    {
        std::printf ("preset sandbox not honoured (presetsDir=%s) - refusing to write "
                     "into the real bank\n",
                     (const char*) userpresets::presetsDir().getFullPathName().toRawUTF8());
        return 1;
    }

    goa::License::setTestMachineId ("2D6EFDDAA1AC30F510C8");
    {
        juce::String err;
        if (! goa::License::activate (GOA_TEST_MASTER_KEY, err))
        {
            std::printf ("license: master activation failed: %s\n", (const char*) err.toRawUTF8());
            ++fails;
        }
        if (goa::License::storedSerial().contains (GOA_TEST_MASTER_KEY))
            std::printf ("license: master key leaked to disk\n"), ++fails;
    }

    phase ("preset round-trip: build state");
    // ---- instance A: make a distinctive state --------------------------------
    GoaSynthAudioProcessor a;
    {
        auto& apvts = a.apvts;
        auto set = [&] (const char* id, float v)
        {
            if (auto* p = apvts.getParameter (id))
                p->setValueNotifyingHost (p->convertTo0to1 (v));
            else
                std::printf ("MISSING PARAM: %s\n", id), ++fails;
        };
        set ("cutoff", 777.0f);
        set ("reso", 0.66f);
        set ("osc1Wave", 1.0f);
        set ("glide", 123.0f);
        set ("lfo1Wave", 2.0f);

        // sequencer params: gate pattern, arp steps, sync divisions
        set ("gateSync", 2.0f);
        set ("gateDepth", 0.9f);
        set ("gate1", 1.0f);
        set ("gate3", 0.0f);
        set ("gate16", 1.0f);
        set ("arpSync", 0.0f);
        set ("arpOct", 2.0f);
        set ("arpDir", 2.0f);
        set ("arp1", 5.0f);
        set ("arp16", 12.0f);
        set ("arpVel1", 1.0f);
        set ("arpVel5", 0.2f);
        set ("arpVel16", 0.75f);

        // filter B + routing
        set ("filter2Type", 2.0f);
        set ("cutoff2", 333.0f);
        set ("reso2", 0.55f);
        set ("filterRoute", 1.0f);

        // mod matrix: slot 1 = velocity->cutoff, slot 2 = modwheel->drive
        set ("mod1Src", 5.0f);
        set ("mod1Dst", 1.0f);
        set ("mod1Amt", 0.6f);
        set ("mod2Src", 6.0f);
        set ("mod2Dst", 9.0f);
        set ("mod2Amt", -0.4f);

        // draw a multi-frame user wave: sine(1x), sine(3x), sine(5x), saw
        if (auto* w = a.wavetable (0))
        {
            float shape[goa::UserWave::size];
            for (int f = 0; f < 4; ++f)
            {
                for (int i = 0; i < goa::UserWave::size; ++i)
                {
                    const float t = (float) i / (float) goa::UserWave::size;
                    shape[i] = f == 3 ? 2.0f * t - 1.0f
                                      : std::sin (juce::MathConstants<float>::twoPi
                                                  * (float) (2 * f + 1) * t);
                }
                w->setFrame (f, shape, goa::UserWave::size);
            }
        }
        a.commitWaves();
        set ("osc1WtPos", 0.5f);
    }

    const auto xml = a.stateToXml();
    if (xml == nullptr)
    {
        std::printf ("stateToXml() returned nullptr\n");
        return 1;
    }

    const auto dir = juce::File::getCurrentWorkingDirectory().getChildFile ("rt_test_out");
    dir.createDirectory();
    const auto file = dir.getChildFile ("roundtrip.goapreset");
    const juce::StringArray tagsIn { "Acid", "bass", "night", "acid", "  " };
    if (! userpresets::savePresetTo (file, *xml, tagsIn))
    {
        std::printf ("failed to write preset file\n");
        return 1;
    }

    phase ("tag persistence");
    // ---- tag persistence -----------------------------------------------------
    {
        const auto tagsOut = userpresets::readTags (file);
        const juce::StringArray want = { "acid", "bass", "night" };
        if (tagsOut != want)
        {
            std::printf ("tag persistence failed: %s\n",
                         tagsOut.joinIntoString (",").toRawUTF8());
            ++fails;
        }
        // And the state must still load: the PRESETINFO block must not confuse it.
        GoaSynthAudioProcessor tagProbe;
        tagProbe.applyStateXml (*xml);
        if (std::fabs (tagProbe.apvts.getParameter ("cutoff")->getValue()
                       - a.apvts.getParameter ("cutoff")->getValue()) > 1.0e-4f)
        {
            std::printf ("state with PRESETINFO block failed to restore\n");
            ++fails;
        }

        // Re-saving the SAME file with different tags is the rename / retag path
        // and it is the one that used to corrupt the file: savePresetTo blindly
        // appended a second PRESETINFO, readTagsFromXml() takes the first, so the
        // new tags were silently ignored and clearing tags was impossible.
        if (! userpresets::savePresetTo (file, *xml, { "retagged" }))
        {
            std::printf ("re-save (retag) failed\n");
            ++fails;
        }
        const auto retagged = userpresets::readTags (file);
        if (retagged != juce::StringArray ({ "retagged" }))
        {
            std::printf ("retag did not replace the old tags (got '%s')\n",
                         retagged.joinIntoString (",").toRawUTF8());
            ++fails;
        }
        if (auto after = juce::parseXML (file))
        {
            int infoBlocks = 0;
            for (int i = 0; i < after->getNumChildElements(); ++i)
                if (after->getChildElement (i)->hasTagName ("PRESETINFO"))
                    ++infoBlocks;
            if (infoBlocks != 1)
            {
                std::printf ("retag left %d PRESETINFO blocks (want exactly 1)\n",
                             infoBlocks);
                ++fails;
            }
        }
        else
        {
            std::printf ("retagged preset file is not valid XML\n");
            ++fails;
        }

        // Clearing the tags entirely must work too - that is only possible if the
        // old block was removed rather than shadowed.
        if (! userpresets::savePresetTo (file, *xml, {}))
        {
            std::printf ("re-save (clear tags) failed\n");
            ++fails;
        }
        if (! userpresets::readTags (file).isEmpty())
        {
            std::printf ("clearing tags did not remove the PRESETINFO block\n");
            ++fails;
        }
    }

    phase ("shared-bank fallback");
    // ---- shared-bank fallback -------------------------------------------------
    // Save into the real shared dir, verify the merged scan exposes it and that
    // a same-named user preset shadows it; clean up afterwards.
    {
        const auto sharedFile = userpresets::sharedPresetsDir()
                                    .getChildFile ("RT_SHARED_TEST.goapreset");
        if (! userpresets::savePresetTo (sharedFile, *xml, { "shared", "test" }))
        {
            std::printf ("failed to write shared-bank preset\n");
            ++fails;
        }

        const auto bank = userpresets::scanPresets();
        bool found = false;
        for (const auto& f : bank)
            if (f.getFileName() == "RT_SHARED_TEST.goapreset")
                found = true;
        if (! found)
        {
            std::printf ("shared-bank preset missing from merged scan\n");
            ++fails;
        }

        // A user preset with the same file name must shadow the shared one.
        const auto userFile = userpresets::fileForName ("RT_SHARED_TEST");
        userpresets::savePresetTo (userFile, *xml);
        const auto bank2 = userpresets::scanPresets();
        int copies = 0;
        for (const auto& f : bank2)
            if (f.getFileName() == "RT_SHARED_TEST.goapreset")
                ++copies;
        if (copies != 1)
        {
            std::printf ("shadow rule broken: %d copies of RT_SHARED_TEST\n", copies);
            ++fails;
        }
        userFile.deleteFile();
        sharedFile.deleteFile();

        // Bank classification: the browser badges shared presets with a
        // SHARED pill, so the classifier must agree with reality.
        auto sharedProbe = userpresets::sharedPresetsDir().getChildFile ("RT_PROBE.goapreset");
        userpresets::savePresetTo (sharedProbe, *xml);
        auto userProbe = userpresets::fileForName ("RT_PROBE");
        userpresets::savePresetTo (userProbe, *xml);
        if (! userpresets::isSharedPreset (sharedProbe))
        {
            std::printf ("isSharedPreset should be true for shared-bank file\n");
            ++fails;
        }
        if (userpresets::isSharedPreset (userProbe))
        {
            std::printf ("isSharedPreset should be false for per-user file\n");
            ++fails;
        }
        sharedProbe.deleteFile();
        userProbe.deleteFile();
    }

    phase ("load and compare");
    // ---- instance B: load and compare ----------------------------------------
    GoaSynthAudioProcessor b;
    auto loaded = juce::parseXML (file);
    if (loaded == nullptr)
    {
        std::printf ("failed to parse preset file\n");
        return 1;
    }
    b.applyStateXml (*loaded);

    {
        auto& apvts = b.apvts;
        auto chk = [&] (const char* id)
        {
            auto* pa = a.apvts.getParameter (id);
            auto* pb = apvts.getParameter (id);
            if (pa == nullptr || pb == nullptr)
                return;
            const float va = pa->getValue(), vb = pb->getValue();
            if (std::fabs (va - vb) > 1.0e-4f)
            {
                std::printf ("MISMATCH %s: %f vs %f\n", id, (double) va, (double) vb);
                ++fails;
            }
        };
        chk ("cutoff"); chk ("reso"); chk ("osc1Wave"); chk ("glide"); chk ("lfo1Wave");
        chk ("osc1WtPos");
        chk ("gateSync"); chk ("gateDepth"); chk ("gateShape"); chk ("gate1"); chk ("gate3"); chk ("gate16");
        chk ("arpSync"); chk ("arpOct"); chk ("arpDir"); chk ("arp1"); chk ("arp16");
        chk ("arpVel1"); chk ("arpVel5"); chk ("arpVel16");
        chk ("arpGate1"); chk ("arpGate8"); chk ("arpGate16");
        chk ("filter2Type"); chk ("cutoff2"); chk ("reso2"); chk ("filterRoute");
        chk ("mod1Src"); chk ("mod1Dst"); chk ("mod1Amt");
        chk ("mod2Src"); chk ("mod2Dst"); chk ("mod2Amt");

        float wa[goa::UserWave::size], wb[goa::UserWave::size];
        for (int f = 0; f < 4; ++f)
        {
            a.wavetable (0)->getFrame (f, wa, goa::UserWave::size);
            b.wavetable (0)->getFrame (f, wb, goa::UserWave::size);
            for (int i = 0; i < goa::UserWave::size; ++i)
                if (std::fabs (wa[i] - wb[i]) > 1.0e-4f)
                {
                    std::printf ("WAVE MISMATCH frame %d at %d: %f vs %f\n",
                                 f, i, (double) wa[i], (double) wb[i]);
                    ++fails;
                    break;
                }
        }
    }

    phase ("wavetable morph");
    // ---- morph behaviour across the drawn frames ------------------------------
    {
        const auto* w = a.wavetable (0);
        // Frames 0..3 hold 1x/3x/5x sine + saw, so positions 0, 1/7, 2/7, 3/7.
        constexpr float pFrame = 1.0f / (float) (goa::UserWave::numFrames - 1);

        // Third-harmonic energy rises along the sweep 0 -> 2/7 (frames 1..3 hold it).
        auto harm3 = [&] (float pos)
        {
            double re = 0.0, im = 0.0;
            constexpr int n = 512;
            for (int i = 0; i < n; ++i)
            {
                const float t = (float) i / (float) n;
                const double ph = juce::MathConstants<double>::twoPi * 3.0 * (double) i / (double) n;
                re += (double) w->sample (t, pos) * std::cos (ph);
                im -= (double) w->sample (t, pos) * std::sin (ph);
            }
            return std::sqrt (re * re + im * im);
        };
        const double eFrame0 = harm3 (0.0f);
        const double eFrame1 = harm3 (1.0f * pFrame);
        const double eMid    = harm3 (0.5f * pFrame);
        if (! (eFrame1 > eFrame0 * 4.0))
        {
            std::printf ("morph sanity failed: h3@frame0=%f h3@frame1=%f\n",
                         eFrame0, eFrame1);
            ++fails;
        }
        if (! (eMid > eFrame0 * 0.5 && eMid < eFrame1 * 1.1))
        {
            std::printf ("morph midpoint not between frames: %f\n", eMid);
            ++fails;
        }
        // Position must actually change the output.
        int diffs = 0;
        for (int i = 0; i < 16; ++i)
            if (std::fabs (w->sample ((float) i / 16.0f, 0.0f)
                         - w->sample ((float) i / 16.0f, 3.0f * pFrame)) > 0.05f)
                ++diffs;
        if (diffs < 8)
        {
            std::printf ("morph produced nearly identical endpoints\n");
            ++fails;
        }
    }

    phase ("wavetable edit tools");
    // ---- wavetable edit-tool transforms ---------------------------------------
    {
        goa::UserWave w;
        w.reset();

        // normalize: DC removal + peak restoration on a lopsided shape
        {
            float shape[goa::UserWave::size];
            for (int i = 0; i < goa::UserWave::size; ++i)
            {
                const float t = (float) i / (float) goa::UserWave::size;
                shape[i] = std::sin (juce::MathConstants<float>::twoPi * t) + 0.7f;
            }
            w.setFrame (2, shape, goa::UserWave::size);
            w.normalizeFrame (2);
            w.publish();                       // mirrors the UI's commitWaves()
            float out[goa::UserWave::size];
            w.getFrame (2, out, goa::UserWave::size);
            double mean = 0.0;
            float peak = 0.0f;
            for (int i = 0; i < goa::UserWave::size; ++i)
            {
                mean += (double) out[i];
                peak = juce::jmax (peak, std::fabs (out[i]));
            }
            mean /= (double) goa::UserWave::size;
            if (std::fabs (mean) > 1.0e-3 || std::fabs (peak - 1.0f) > 1.0e-3)
            {
                std::printf ("normalize failed: mean=%f peak=%f\n", mean, (double) peak);
                ++fails;
            }
        }

        // flip: frame vs its negated copy
        {
            float before[goa::UserWave::size], after[goa::UserWave::size];
            w.getFrame (0, before, goa::UserWave::size);
            w.flipFrame (0);
            w.publish();
            w.getFrame (0, after, goa::UserWave::size);
            for (int i = 0; i < goa::UserWave::size; ++i)
                if (std::fabs (after[i] + before[i]) > 1.0e-4f)
                {
                    std::printf ("flip failed at %d: %f vs -%f\n",
                                 i, (double) after[i], (double) before[i]);
                    ++fails;
                    break;
                }
        }

        // smooth: mean of a zero-mean shape stays ~0, peak drops
        {
            float before[goa::UserWave::size], after[goa::UserWave::size];
            w.getFrame (2, before, goa::UserWave::size);
            w.smoothFrame (2);
            w.publish();
            w.getFrame (2, after, goa::UserWave::size);
            float pb = 0.0f, pa = 0.0f;
            for (int i = 0; i < goa::UserWave::size; ++i)
            {
                pb = juce::jmax (pb, std::fabs (before[i]));
                pa = juce::jmax (pa, std::fabs (after[i]));
            }
            if (! (pa < pb))
            {
                std::printf ("smooth failed to reduce peak: %f -> %f\n",
                             (double) pb, (double) pa);
                ++fails;
            }
        }

        // generators: bounded, pulse duty cycles, nonzero formant/spikes
        {
            float out[goa::UserWave::size];
            w.generateFrame (0, goa::UserWave::ShapeKind::pulse25);
            w.publish();
            w.getFrame (0, out, goa::UserWave::size);
            const float quarter = out[goa::UserWave::size / 8];       // t=0.125: high
            const float threeQ  = out[(3 * goa::UserWave::size) / 4]; // t=0.75: low
            if (! (quarter > 0.9f && threeQ < -0.9f))
            {
                std::printf ("pulse25 duty cycle wrong\n");
                ++fails;
            }

            w.generateFrame (0, goa::UserWave::ShapeKind::pulse50);
            w.publish();
            w.getFrame (0, out, goa::UserWave::size);
            if (! (out[goa::UserWave::size / 4] > 0.9f
                   && out[goa::UserWave::size / 2 + 10] < -0.9f))
            {
                std::printf ("pulse50 duty cycle wrong\n");
                ++fails;
            }

            w.generateFrame (0, goa::UserWave::ShapeKind::formant);
            w.publish();
            w.getFrame (0, out, goa::UserWave::size);
            float peak = 0.0f;
            for (int i = 0; i < goa::UserWave::size; ++i)
                peak = juce::jmax (peak, std::fabs (out[i]));
            if (peak < 0.5f)
            {
                std::printf ("formant shape too quiet: %f\n", (double) peak);
                ++fails;
            }

            w.generateFrame (0, goa::UserWave::ShapeKind::spikes);
            w.publish();
            w.getFrame (0, out, goa::UserWave::size);
            for (int i = 0; i < goa::UserWave::size; ++i)
                if (std::fabs (out[i]) > 1.0f + 1.0e-4f)
                {
                    std::printf ("spikes out of range at %d: %f\n", i, (double) out[i]);
                    ++fails;
                    break;
                }
        }
    }

    phase ("safeFileName");
    // ---- safeFileName sanity --------------------------------------------------
    using userpresets::safeFileName;
    struct { const char* in; const char* want; } sfCases[] = {
        { "MY GOA PATCH", "MY_GOA_PATCH" },
        { "a/b\\c:d*e?", "a_b_c_d_e" },
        { "...", "Untitled" },
        { "  ", "Untitled" },
    };
    for (const auto& c : sfCases)
    {
        const auto got = safeFileName (c.in);
        if (got != c.want)
        {
            std::printf ("safeFileName(%s) = %s, expected %s\n", c.in, got.toRawUTF8(), c.want);
            ++fails;
        }
    }
    if (safeFileName (juce::String().paddedLeft ('x', 100)).length() > 64)
    {
        std::printf ("safeFileName length cap failed\n");
        ++fails;
    }

    phase ("preset packs");
    // ---- preset packs (.goapack zip round trip) ------------------------------
    {
        using namespace userpresets;

        // Seed the real user bank with two known patches so the export has
        // deterministic content; cleaned up at the end of the block.
        const auto pa = fileForName ("RT_PACK_A");
        const auto pb = fileForName ("RT_PACK_B");
        savePresetTo (pa, *xml, { "pack", "alpha" });
        savePresetTo (pb, *xml);
        if (! pa.existsAsFile() || ! pb.existsAsFile())
        {
            std::printf ("pack test: could not seed user bank\n");
            ++fails;
        }

        // EXPORT: the production writer packs the merged bank.
        const auto packFile = dir.getChildFile ("bank.goapack");
        if (! exportPack (packFile, scanPresets()))
        {
            std::printf ("exportPack failed\n");
            ++fails;
        }

        const int bankSize = scanPresets().size();
        const auto packText = packFile.loadFileAsString();
        {
            juce::FileInputStream in (packFile);
            juce::ZipFile zip (in);
            if (zip.getNumEntries() != bankSize)
            {
                std::printf ("pack entries %d != bank %d\n",
                             zip.getNumEntries(), bankSize);
                ++fails;
            }
            bool hasA = false, hasB = false;
            for (int i = 0; i < zip.getNumEntries(); ++i)
                if (auto* e = zip.getEntry (i))
                {
                    hasA |= e->filename == "RT_PACK_A.goapreset";
                    hasB |= e->filename == "RT_PACK_B.goapreset";
                }
            if (! hasA || ! hasB)
            {
                std::printf ("pack missing seeded patches\n");
                ++fails;
            }
            // Tags must survive inside the pack.
            if (auto* e = zip.getEntry ("RT_PACK_A.goapreset"))
            {
                auto entry = zip.createStreamForEntry (zip.getIndexOfFileName (e->filename));
                juce::MemoryBlock mb;
                entry->readIntoMemoryBlock (mb);
                auto presetXml = juce::parseXML (mb.toString());
                if (presetXml == nullptr || readTagsFromXml (*presetXml).indexOf ("alpha") < 0)
                {
                    std::printf ("pack entry lost tags\n");
                    ++fails;
                }
            }
        }

        // IMPORT into a scratch dir: everything lands, bytes intact.
        const auto impDir = dir.getChildFile ("imported");
        auto r1 = importPack (packFile, impDir, false);
        const char* errText = r1.error.isNotEmpty() ? r1.error.toRawUTF8() : "-";
        if (r1.error.isNotEmpty() || r1.imported != bankSize || r1.skipped != 0)
        {
            std::printf ("import 1: err=%s imported=%d skipped=%d (want %d/0)\n",
                         errText, r1.imported, r1.skipped, bankSize);
            ++fails;
        }
        for (auto* src : { &pa, &pb })
        {
            const auto got = impDir.getChildFile (src->getFileName());
            if (! got.existsAsFile() || got.loadFileAsString() != src->loadFileAsString())
            {
                std::printf ("imported %s differs from source\n",
                             src->getFileName().toRawUTF8());
                ++fails;
            }
        }

        // Re-import without overwrite: all skipped, contents untouched.
        auto r2 = importPack (packFile, impDir, false);
        if (r2.imported != 0 || r2.skipped != bankSize)
        {
            std::printf ("re-import (no overwrite): imported=%d skipped=%d\n",
                         r2.imported, r2.skipped);
            ++fails;
        }
        auto r3 = importPack (packFile, impDir, true);
        if (r3.imported != bankSize || r3.skipped != 0)
        {
            std::printf ("re-import (overwrite): imported=%d skipped=%d\n",
                         r3.imported, r3.skipped);
            ++fails;
        }
        const auto gotA = impDir.getChildFile (pa.getFileName());
        if (gotA.loadFileAsString() != pa.loadFileAsString())
        {
            std::printf ("overwrite import changed content\n");
            ++fails;
        }

        // Hostile / junk entries must be ignored or flattened, never escaped.
        {
            const auto readme = dir.getChildFile ("readme_src.txt");
            readme.replaceWithText ("not a preset");
            const auto hostilePack = dir.getChildFile ("hostile.goapack");
            {
                juce::ZipFile::Builder hostile;
                hostile.addFile (readme, 0, "README.txt");
                hostile.addFile (readme, 0, "..\\evil.goapreset");
                hostile.addFile (readme, 0, "sub/dir/deep.goapreset");
                auto out = hostilePack.createOutputStream();
                hostile.writeToStream (*out, nullptr);
                out->flush();
            }

            const auto hostileDir = dir.getChildFile ("hostile");
            auto rh = importPack (hostilePack, hostileDir, false);
            // Both .goapreset entries import, flattened into hostileDir; the
            // README is ignored and nothing escapes the target directory.
            const bool escaped = dir.getChildFile ("evil.goapreset").existsAsFile()
                              || hostileDir.getParentDirectory()
                                    .getChildFile ("sub").isDirectory();
            if (rh.imported != 2 || escaped
                || hostileDir.getNumberOfChildFiles (juce::File::findFiles) != 2
                || ! hostileDir.getChildFile ("evil.goapreset").existsAsFile()
                || ! hostileDir.getChildFile ("deep.goapreset").existsAsFile())
            {
                std::printf ("hostile pack not contained: imported=%d names=[%s] files=[%s]\n",
                             rh.imported, rh.importedNames.joinIntoString (",").toRawUTF8(),
                             [hostileDir]
                             {
                                 juce::StringArray names;
                                 for (const auto& f : hostileDir.findChildFiles (
                                          juce::File::findFiles, false))
                                     names.add (f.getFileName());
                                 return names.joinIntoString (",");
                             }().toRawUTF8());
                ++fails;
            }
        }

        pa.deleteFile();
        pb.deleteFile();
    }

    phase ("offline AI designer");
    // ---- AI patch generator: variation ----------------------------------------
    {
        using P = std::map<juce::String, float>;

        // Same seed => identical patch; different seed => different patch.
        const auto r1 = AiPatchGen::generate ("dark rolling acid bass", 12345);
        const auto r2 = AiPatchGen::generate ("dark rolling acid bass", 12345);
        const auto r3 = AiPatchGen::generate ("dark rolling acid bass", 54321);
        if (r1.patch != r2.patch)
        {
            std::printf ("AI: same seed produced different patches\n");
            ++fails;
        }
        if (r1.patch == r3.patch)
        {
            std::printf ("AI: different seed produced identical patches\n");
            ++fails;
        }

        // Random seed (what the UI uses) must vary across calls.
        int distinct = 0;
        P first;
        for (int i = 0; i < 6; ++i)
        {
            auto r = AiPatchGen::generate ("gated full-on lead");
            if (i == 0)
                first = r.patch;
            else if (r.patch != first)
                ++distinct;
        }
        if (distinct < 3)
        {
            std::printf ("AI: random generations nearly identical (%d/5)\n", distinct);
            ++fails;
        }

        // Every id the generator emits must exist in the real parameter set.
        GoaSynthAudioProcessor probe;
        for (const auto& [id, v] : r1.patch)
            if (probe.apvts.getParameter (id) == nullptr)
            {
                std::printf ("AI: unknown parameter id '%s'\n", id.toRawUTF8());
                ++fails;
            }
        for (const auto& [id, v] : r3.patch)
            if (probe.apvts.getParameter (id) == nullptr)
            {
                std::printf ("AI: unknown parameter id '%s'\n", id.toRawUTF8());
                ++fails;
            }

        // Variation-strength modes: deterministic per (prompt, seed, mode),
        // SUBTLE stays closer to the family template than WILD.
        const auto s1 = AiPatchGen::generate ("gated rolling acid bass", 777,
                                              AiPatchGen::Var::Subtle);
        const auto s2 = AiPatchGen::generate ("gated rolling acid bass", 778,
                                              AiPatchGen::Var::Subtle);
        const auto w1 = AiPatchGen::generate ("gated rolling acid bass", 777,
                                              AiPatchGen::Var::Wild);
        if (s1.patch != AiPatchGen::generate ("gated rolling acid bass", 777,
                                              AiPatchGen::Var::Subtle).patch)
        {
            std::printf ("AI: SUBTLE mode not deterministic\n");
            ++fails;
        }
        if (s1.patch == w1.patch)
        {
            std::printf ("AI: SUBTLE and WILD produced identical patches\n");
            ++fails;
        }
        if (s1.patch == s2.patch)
        {
            std::printf ("AI: SUBTLE re-roll identical to previous SUBTLE roll\n");
            ++fails;
        }
        // SUBTLE keeps the classic 3-on/1-off trancegate grid...
        for (int i = 0; i < 16; ++i)
        {
            const float want = (i % 4 == 3) ? 0.0f : 1.0f;
            auto it = s1.patch.find (juce::String ("gate") + juce::String (i + 1));
            if (it == s1.patch.end() || std::abs (it->second - want) > 1.0e-6f)
            {
                std::printf ("AI: SUBTLE gate groove is not the classic grid\n");
                ++fails;
                break;
            }
        }
        // ...and the 1/16 minor roller with the downbeat accent.
        if (auto it = s1.patch.find ("arpVel1");
            it == s1.patch.end() || it->second < 0.999f)
        {
            std::printf ("AI: SUBTLE arp missing downbeat accent\n");
            ++fails;
        }
    }

    phase ("render smoke");
    // ---- render smoke: DSP chain stays finite and bounded ---------------------
    {
        GoaSynthAudioProcessor r;
        r.prepareToPlay (48000.0, 512);

        auto set = [&] (const char* id, float v)
        {
            if (auto* p = r.apvts.getParameter (id))
                p->setValueNotifyingHost (p->convertTo0to1 (v));
        };
        // Worst cases for the new DSP code: hard drive, deep PWM, sub square,
        // resonant self-oscillation territory, long feedback delay.
        set ("drive", 1.0f);
        set ("reso", 1.0f);
        set ("osc1Wave", 1.0f);
        set ("subWave", 0.0f);
        set ("subLevel", 0.8f);
        set ("delayFb", 0.95f);
        set ("delayMix", 0.6f);

        juce::AudioBuffer<float> buf (2, 512);
        // A host hands the plugin a fresh MIDI buffer for every block, so each
        // processBlock call below is followed by midi.clear(). The processor
        // merges on-screen keyboard events into the buffer it is handed, so a
        // buffer reused across blocks replays every note-on ever sent: the note
        // re-triggers each block, no note is ever held, and any mid-note
        // behaviour (filter types, envelopes, glide) becomes untestable.
        // Clearing after the call (not before) keeps events the test feeds in
        // itself, while stopping the processor's own additions from replaying.
        juce::MidiBuffer midi;
        r.uiNoteOn (48, 1.0f);

        double peak = 0.0;
        bool allFinite = true;
        for (int blk = 0; blk < 30; ++blk)
        {
            buf.clear();
            r.processBlock (buf, midi);
            midi.clear();
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < buf.getNumSamples(); ++i)
                {
                    const float s = buf.getSample (ch, i);
                    if (! std::isfinite (s))
                        allFinite = false;
                    peak = juce::jmax (peak, (double) std::fabs (s));
                }
        }
        r.uiNoteOff (48);
        // Dry the delay for the decay checks below: at fb 0.95 the delay tail
        // pins the limiter for seconds, which would mask the envelope release
        // the check is meant to observe.
        set ("delayMix", 0.0f);
        set ("delayFb", 0.1f);
        for (int blk = 0; blk < 10; ++blk)
        {
            buf.clear();
            r.processBlock (buf, midi);
            midi.clear();
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < buf.getNumSamples(); ++i)
                    if (! std::isfinite (buf.getSample (ch, i)))
                        allFinite = false;
        }

        if (! allFinite)
        {
            std::printf ("render smoke: non-finite samples in output\n");
            ++fails;
        }
        // The master limiter holds everything under its -0.5 dB ceiling
        // (goa::MasterLimiter); anything near full scale means the limiter
        // or its soft-clip safety was bypassed.
        if (peak > 0.95)
        {
            std::printf ("render smoke: output above the master ceiling, peak %f\n", peak);
            ++fails;
        }
        if (peak < 1.0e-4)
        {
            std::printf ("render smoke: synth produced silence\n");
            ++fails;
        }

        // Audio-reactive UI level: the follower must rise while notes sound
        // and relax toward zero afterwards.
        if (r.uiLevel.load() < 1.0e-3f)
        {
            std::printf ("render smoke: uiLevel stayed at zero while playing\n");
            ++fails;
        }
        // Audio-reactive UI level: the follower must rise while notes sound
        // and decay after note-off. It is a block-peak follower with slow
        // release, so it may ripple upward a few thousandths against the
        // periodic delay tail — but the trend must be down, and it must end
        // well below its post-note-off level.
        float lastLevel = r.uiLevel.load();
        const float levelAfterOff = lastLevel;
        for (int blk = 0; blk < 40; ++blk)
        {
            buf.clear();
            r.processBlock (buf, midi);
            midi.clear();
            if (r.uiLevel.load() > lastLevel + 0.01f)
            {
                std::printf ("render smoke: uiLevel rose after note-off\n");
                ++fails;
                break;
            }
            lastLevel = r.uiLevel.load();
        }
        if (lastLevel > levelAfterOff * 0.75f)
        {
            std::printf ("render smoke: uiLevel barely decayed after note-off "
                         "(%f -> %f)\n", (double) levelAfterOff, (double) lastLevel);
            ++fails;
        }

        // ---- FILTER B routing: every route must render finite, audible audio
        for (int route = 0; route < 3; ++route)
        {
            set ("filterRoute", (float) route);
            set ("cutoff", 200.0f);           // A dark
            set ("cutoff2", 8000.0f);         // B bright
            set ("reso", 0.95f);
            set ("reso2", 0.95f);
            set ("filter2Type", 2.0f);        // HP B in parallel/split

            bool finite = true;
            double rPeak = 0.0;
            for (int blk = 0; blk < 8; ++blk)
            {
                buf.clear();
                r.uiNoteOn (48, 1.0f);
                r.processBlock (buf, midi);
                midi.clear();
                r.uiNoteOff (48);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < buf.getNumSamples(); ++i)
                    {
                        const float s = buf.getSample (ch, i);
                        if (! std::isfinite (s)) finite = false;
                        rPeak = juce::jmax (rPeak, (double) std::fabs (s));
                    }
            }
            if (! finite || rPeak < 1.0e-4 || rPeak > 4.0)
            {
                std::printf ("filter route %d: bad render (finite=%d peak=%f)\n",
                             route, (int) finite, rPeak);
                ++fails;
            }
        }
        set ("filterRoute", 0.0f);
        set ("reso", 0.25f);
        set ("reso2", 0.2f);

        // ---- NOTCH stays a notch in every route ----------------------------
        // The notch is (dry - bandpass) and is applied once, for both channels,
        // before routing. SPLIT then subtracted it a second time on the left
        // channel, which turned A's notch back into a bandpass - one of the
        // type/route combinations lied about what it was doing. Total level
        // cannot tell them apart (the resonant band can be louder than the
        // notch), so the discriminator is the FUNDAMENTAL: a 420 Hz bandpass
        // buries a 65 Hz note, a notch at 420 Hz leaves it almost untouched.
        {
            // Energy in ~60-150 Hz of one channel: the note's fundamental and
            // second harmonic, which a bandpass at 420 Hz buries and a notch (or
            // a plain lowpass) leaves alone. The 60 Hz highpass matters - without
            // it, a little DC in the channel outweighs everything the filters do -
            // and the 150 Hz lowpass keeps the bandpass's own resonant peak (with
            // its long skirt down to 300 Hz) out of the measurement.
            // retrigger=false keeps a note already down: the filter cache is
            // reset by startNote(), so a bug that only shows up mid-note is
            // invisible to a fresh note-on.
            auto lowBand = [&] (int ch, bool retrigger = true)
            {
                const float hpA = (float) (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi
                                                        * 60.0 / 48000.0));
                const float lpA = (float) (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi
                                                        * 150.0 / 48000.0));
                float drum = 0.0f, lp = 0.0f;
                double acc = 0.0;

                if (retrigger)
                    r.uiNoteOn (36, 1.0f);

                for (int blk = 0; blk < 20; ++blk)
                {
                    buf.clear();
                    r.processBlock (buf, midi);
                    midi.clear();
                    if (blk >= 4)                    // skip the attack ramp
                        for (int i = 0; i < buf.getNumSamples(); ++i)
                        {
                            const float x = buf.getSample (ch, i);
                            drum += hpA * (x - drum);    // one-pole lowpass...
                            const float hp = x - drum;   // ...so this is the highpass
                            lp += lpA * (hp - lp);
                            acc += (double) lp * (double) lp;
                        }
                }
                if (retrigger)
                {
                    r.uiNoteOff (36);
                    for (int blk = 0; blk < 12; ++blk)   // let the short release clear
                    {
                        buf.clear();
                        r.processBlock (buf, midi);
                        midi.clear();
                    }
                }
                return acc;
            };

            // The note is C2 (~65 Hz) and the filters sit at 1200 Hz: far enough
            // apart that a bandpass is ~40 dB down where the note lives, while a
            // notch there changes nothing. Close cutoffs made the two nearly
            // indistinguishable, which is exactly how the bug hid.
            set ("filterRoute", 2.0f);           // SPLIT: A -> left, B -> right
            set ("cutoff", 1200.0f);
            set ("cutoff2", 1200.0f);
            set ("reso", 0.2f);
            set ("reso2", 0.2f);
            set ("ampS", 1.0f);
            set ("ampR", 0.05f);

            set ("filterType", 4.0f);            // NOTCH
            const double asNotch = lowBand (0);
            set ("filterType", 3.0f);            // BP 12 dB, same cutoff
            const double asBand = lowBand (0);

            // With the double-subtraction bug the left channel measured the same
            // either way; a real notch keeps the fundamental, ~30 dB above what a
            // 420 Hz bandpass lets through.
            if (! std::isfinite (asNotch) || ! (asNotch > asBand * 8.0))
            {
                std::printf ("filter notch: SPLIT left output is not a notch "
                             "(low-band energy notch=%g bandpass=%g)\n", asNotch, asBand);
                ++fails;
            }

            // Filter B must keep its own type when A's type changes *while the
            // note is held*. The reset lived inside A's update, so it only showed
            // up mid-note: a held SPLIT patch whose right channel was a bandpass
            // came back with its fundamental intact while the UI still said
            // BAND PASS, until the next note-on quietly put it right again.
            set ("filterType", 0.0f);            // A: plain LP, so only B shapes the right
            set ("filter2Type", 3.0f);           // B: bandpass
            r.uiNoteOn (36, 1.0f);
            const double bBefore = lowBand (1, false);
            set ("filterType", 1.0f);            // A changes; B must not follow
            const double bAfter = lowBand (1, false);
            r.uiNoteOff (36);
            for (int blk = 0; blk < 12; ++blk)
            {
                buf.clear();
                r.processBlock (buf, midi);
                midi.clear();
            }

            // Sensitivity guard: prove the measurement can tell the two types
            // apart at all, so the assertion below can never pass by measuring
            // nothing. A lowpass at 1200 Hz passes the note's low band; a
            // bandpass there does not.
            set ("filter2Type", 0.0f);
            const double probeLp = lowBand (1);
            set ("filter2Type", 3.0f);
            const double probeBp = lowBand (1);
            if (! std::isfinite (probeLp) || ! std::isfinite (probeBp)
                || ! (probeLp > probeBp * 4.0))
            {
                std::printf ("filter probe: low band does not separate "
                             "lowpass (%g) from bandpass (%g)\n", probeLp, probeBp);
                ++fails;
            }

            if (! std::isfinite (bAfter) || ! (bAfter < bBefore * 4.0))
            {
                std::printf ("filter type: changing A also changed B "
                             "(right-channel low band before=%g after=%g)\n", bBefore, bAfter);
                ++fails;
            }

            // Back to the defaults the next block expects.
            set ("filterType", 1.0f);
            set ("filterRoute", 0.0f);
            set ("cutoff", 1500.0f);
            set ("filter2Type", 0.0f);
            set ("cutoff2", 8000.0f);
            set ("ampS", 0.85f);
            set ("ampR", 0.4f);
        }

        // ---- mod matrix at extreme settings: dests must stay bounded -------
        set ("mod1Src", 1.0f);            // LFO 1
        set ("mod1Dst", 1.0f);            // -> cutoff (4 oct!) — A dark so no blowup
        set ("mod1Amt", 1.0f);
        set ("mod2Src", 5.0f);            // velocity
        set ("mod2Dst", 4.0f);            // -> OSC A level (LFO'd note level)
        set ("mod2Amt", 1.0f);
        set ("mod3Src", 6.0f);            // modwheel
        set ("mod3Dst", 9.0f);            // -> drive
        set ("mod3Amt", 1.0f);
        {
            bool finite = true;
            double rPeak = 0.0;
            for (int blk = 0; blk < 20; ++blk)
            {
                buf.clear();
                r.uiNoteOn (36, 1.0f);
                r.processBlock (buf, midi);
                midi.clear();
                r.uiNoteOff (36);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < buf.getNumSamples(); ++i)
                    {
                        const float s = buf.getSample (ch, i);
                        if (! std::isfinite (s)) finite = false;
                        rPeak = juce::jmax (rPeak, (double) std::fabs (s));
                    }
            }
            if (! finite || rPeak > 4.0)
            {
                std::printf ("mod matrix extreme: bad render (finite=%d peak=%f)\n",
                             (int) finite, rPeak);
                ++fails;
            }
        }

        // ---- OTT at full squeeze: finite, bounded, audible -----------------
        set ("ottDepth", 1.0f);
        set ("ottLow", 1.0f);
        set ("ottMid", 1.0f);
        set ("ottHigh", 1.0f);
        set ("ottOut", 3.0f);
        {
            bool finite = true;
            double rPeak = 0.0;
            for (int blk = 0; blk < 20; ++blk)
            {
                buf.clear();
                r.uiNoteOn (48, 1.0f);
                r.processBlock (buf, midi);
                midi.clear();
                r.uiNoteOff (48);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < buf.getNumSamples(); ++i)
                    {
                        const float s = buf.getSample (ch, i);
                        if (! std::isfinite (s)) finite = false;
                        rPeak = juce::jmax (rPeak, (double) std::fabs (s));
                    }
            }
            if (! finite || rPeak < 1.0e-4 || rPeak > 4.0)
            {
                std::printf ("OTT full squeeze: bad render (finite=%d peak=%f)\n",
                             (int) finite, rPeak);
                ++fails;
            }
        }
        set ("ottDepth", 0.0f);
        set ("ottOut", 0.0f);
    }

    phase ("theme engine");

    // ---- theme engine: switching rewrites the palette, state rides along ----
    {
        if (goaui::activeTheme != goaui::themeUv)
            goaui::setTheme (goaui::themeUv, nullptr);
        const auto uvAccent = goaui::accent;

        goaui::setTheme (goaui::themeSteel, nullptr);
        if (goaui::accent == uvAccent)
        {
            std::printf ("theme: steel accent did not replace UV accent\n");
            ++fails;
        }
        goaui::setTheme (goaui::themeAnalog, nullptr);
        if (goaui::activeTheme != goaui::themeAnalog)
        {
            std::printf ("theme: setTheme did not switch the active theme\n");
            ++fails;
        }

        // The skin choice persists through a state save/load cycle.
        GoaSynthAudioProcessor t1;
        GoaSynthAudioProcessor t2;
        juce::MemoryBlock mb;
        t1.getStateInformation (mb);
        t2.setStateInformation (mb.getData(), (int) mb.getSize());
        if (goaui::themeForNextEditor != goaui::themeAnalog)
        {
            std::printf ("theme: uiTheme attribute lost in state round-trip\n");
            ++fails;
        }
        goaui::setTheme (goaui::themeUv, nullptr);   // leave the house look active
    }

    phase ("licensing paths");
    // ---- licensing: all activation paths -----------------------------------
    {
        // 1) Activated by the master key at startup; deactivating drops it.
        if (! goa::License::isLicensed())
            std::printf ("license: master activation did not persist\n"), ++fails;
        goa::License::deactivate();
        if (goa::License::isLicensed())
            std::printf ("license: deactivate() left a license behind\n"), ++fails;

        // 2) A serial for another machine is refused.
        goa::License::setTestMachineId ("0123456789ABCDEF0123");
        juce::String err;
        if (goa::License::activate (devSerialText, err))
            std::printf ("license: foreign-machine serial was accepted\n"), ++fails;

        // 3) The correct machine's serial activates...
        const juce::String devId = "2D6EFDDAA1AC30F510C8";
        goa::License::setTestMachineId (devId);
        juce::String err2;
        if (! goa::License::activate (devSerialText, err2))
        {
            std::printf ("license: valid serial refused: %s\n", (const char*) err2.toRawUTF8());
            ++fails;
        }
        else
        {
            if (! goa::License::isLicensed())
                std::printf ("license: activation did not persist\n"), ++fails;

            // 4) ...but the same serial on ANOTHER machine is blocked by the
            //    one-machine ledger. The deactivation on THIS machine lifts
            //    THIS machine's binding (transfers), which is why the foreign
            //    machine's ledger check must still fire.
            goa::License::deactivate();
            {
                // Prove the lift really happened: the same serial activates
                // on the same machine again without being "used elsewhere".
                goa::License::setTestMachineId (devId);
                if (! goa::License::activate (devSerialText, err2))
                    std::printf ("license: deactivate did not lift the machine binding\n"), ++fails;
                goa::License::deactivate();
                goa::License::setTestMachineId ("FEDCBA98765432100123");
            }
            juce::String err3;
            if (goa::License::activate (devSerialText, err3))
                std::printf ("license: ledger failed to block a bound serial\n"), ++fails;
        }

        // 4b) Copy/paste tolerance: the same serial with cosmetic dashes inside
        //     the signature half must still activate — users paste serials as
        //     pretty-printed or word-wrapped by emails and consoles. Forgery is
        //     still impossible: the signature check, not the dash layout, is
        //     the security boundary.
        goa::License::setTestMachineId (devId);
        {
            const juce::String body = juce::String (devSerialText).substring (5);
            const int d = body.indexOfChar ('-');
            juce::String wrapped;
            const juce::String sig = body.substring (d + 1);
            for (int i = 0; i < sig.length(); i += 16)
            {
                if (i > 0) wrapped << "-";
                wrapped << sig.substring (i, i + 16);
            }

            juce::String errWrap;
            if (! goa::License::activate ("GOA1-" + body.substring (0, d) + "-" + wrapped, errWrap))
                std::printf ("license: dash-wrapped serial refused: %s\n", (const char*) errWrap.toRawUTF8()), ++fails;
            goa::License::deactivate();
        }

        // 5) The raw master key activates any machine...
        goa::License::deactivate();
        goa::License::setTestMachineId ("ABCD0123456789EFABCD");
        juce::String err4;
        if (! goa::License::activate (GOA_TEST_MASTER_KEY, err4))
        {
            std::printf ("license: master key refused: %s\n", (const char*) err4.toRawUTF8());
            ++fails;
        }

        // 6) Garbage is refused everywhere.
        goa::License::deactivate();
        juce::String err5;
        if (goa::License::activate ("GOA1-0000-DEAD-BEEF", err5))
            std::printf ("license: garbage serial accepted\n"), ++fails;

        goa::License::deactivate();
        goa::License::setTestMachineId ("");   // restore the real machine id

        // 7) .goalicense files: a valid file activates, tampered files and
        //    garbage don't, and a file bound to another machine is refused.
        goa::License::setTestMachineId (devId);
        const juce::File licFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                       .getChildFile ("goasynth_test.goalicense");
        licFile.deleteFile();

        if (! juce::String (goa::License::makeLicenseFile (devSerialText, "roundtrip")).startsWith (goa::License::fileMagic))
            std::printf ("license: file template lost its magic line\n"), ++fails;

        licFile.replaceWithText (goa::License::makeLicenseFile (devSerialText, "test studio PC"));
        juce::String errF;
        if (! goa::License::activateFile (licFile, errF))
            std::printf ("license: valid .goalicense refused: %s\n", (const char*) errF.toRawUTF8()), ++fails;
        else if (! goa::License::isLicensed())
            std::printf ("license: .goalicense activation did not persist\n"), ++fails;
        goa::License::deactivate();

        licFile.replaceWithText (goa::License::makeLicenseFile (devSerialText, "t").replace ("serial:", "serialX:"));
        if (goa::License::activateFile (licFile, errF))
            std::printf ("license: file without a serial line was accepted\n"), ++fails;

        licFile.replaceWithText ("hello world, not a license");
        if (goa::License::activateFile (licFile, errF))
            std::printf ("license: garbage file was accepted\n"), ++fails;

        goa::License::setTestMachineId ("0123456789ABCDEF0123");
        licFile.replaceWithText (goa::License::makeLicenseFile (devSerialText, "someone else"));
        if (goa::License::activateFile (licFile, errF))
            std::printf ("license: foreign .goalicense was accepted\n"), ++fails;

        goa::License::setTestMachineId ("");
        licFile.deleteFile();

        // 8) Trial: fresh stamp = active, 24 h old = expired, and an expired
        //    instance renders silence exactly like an unlicensed one.
        goa::License::setTestMachineId (devId);
        goa::License::deactivate();
        goa::License::setTrialTestStart (0);   // stamp now
        if (! goa::License::trialActive())
            std::printf ("license: fresh trial is not active\n"), ++fails;

        {
            GoaSynthAudioProcessor tr;
            tr.prepareToPlay (48000.0, 256);
            tr.uiNoteOn (48, 1.0f);

            juce::AudioBuffer<float> buf (2, 256);
            juce::MidiBuffer emptyMidi;
            double peak = 0.0;
            for (int blk = 0; blk < 30; ++blk)
            {
                buf.clear();
                tr.processBlock (buf, emptyMidi);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < buf.getNumSamples(); ++i)
                        peak = juce::jmax (peak, (double) std::fabs (buf.getSample (ch, i)));
            }
            if (peak < 1.0e-3)
                std::printf ("license: trial instance produced no audio\n"), ++fails;
        }

        goa::License::setTrialTestStart (juce::Time::currentTimeMillis() / 1000 - 25 * 3600);
        if (goa::License::trialActive())
            std::printf ("license: expired trial is still active\n"), ++fails;

        {
            GoaSynthAudioProcessor exp;
            exp.prepareToPlay (48000.0, 256);

            juce::AudioBuffer<float> buf (2, 256);
            for (int ch = 0; ch < 2; ++ch)
                buf.setSample (ch, 10, 0.5f);
            juce::MidiBuffer emptyMidi;
            exp.processBlock (buf, emptyMidi);

            for (int ch = 0; ch < 2; ++ch)
                if (buf.getSample (ch, 10) != 0.0f)
                    std::printf ("license: expired-trial instance produced audio\n"), ++fails;
        }

        // 8b) Trial expiring mid-session: an instance armed during the trial
        //     must lock itself on the first block after the window ends,
        //     even headless (processBlock re-checks the stamp; the editor
        //     timer only paints the result).
        goa::License::setTrialTestStart (0);   // fresh 24 h window again
        {
            GoaSynthAudioProcessor mid;
            mid.prepareToPlay (48000.0, 256);
            mid.uiNoteOn (48, 1.0f);

            juce::AudioBuffer<float> buf (2, 256);
            juce::MidiBuffer emptyMidi;
            buf.clear();
            mid.processBlock (buf, emptyMidi);
            if (! mid.licensedFlag.load())
                std::printf ("license: mid-life trial instance started locked\n"), ++fails;

            goa::License::setTrialTestStart (juce::Time::currentTimeMillis() / 1000 - 25 * 3600);
            buf.clear();
            mid.processBlock (buf, emptyMidi);
            if (mid.licensedFlag.load())
                std::printf ("license: mid-session expiry did not lock the instance\n"), ++fails;
            if (! mid.pendingPoke.load())
                std::printf ("license: mid-session expiry did not poke the editor\n"), ++fails;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < buf.getNumSamples(); ++i)
                    if (buf.getSample (ch, i) != 0.0f)
                        std::printf ("license: mid-session expiry still produced audio\n"), ++fails;
        }

        // A real license supersedes the trial readout entirely.
        goa::License::activate (GOA_TEST_MASTER_KEY, errF);
        if (goa::License::trialTimeLeft() != "active")
            std::printf ("license: trial readout not 'active' when licensed\n"), ++fails;
        goa::License::deactivate();
        goa::License::setTestMachineId ("");

        // 9) Stuck-note rescue: a UI note-on that never sees its note-off
        //    (editor window closed mid-note, swallowed mouse-up) must still
        //    be silenced by the queued note-off rescue.
        goa::License::activate (GOA_TEST_MASTER_KEY, errF);
        {
            GoaSynthAudioProcessor r;
            r.prepareToPlay (48000.0, 512);
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer emptyMidi;

            auto runPeak = [&] (int blocks)
            {
                double pk = 0.0;
                for (int blk = 0; blk < blocks; ++blk)
                {
                    buf.clear();
                    r.processBlock (buf, emptyMidi);
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < buf.getNumSamples(); ++i)
                            pk = juce::jmax (pk, (double) std::fabs (buf.getSample (ch, i)));
                }
                return pk;
            };

            r.uiNoteOn (60, 0.9f);
            if (r.uiHeldCount() != 1)
                std::printf ("license: uiHeldCount did not track a note-on\n"), ++fails;
            if (! (runPeak (5) > 0.001))
                std::printf ("license: rescue-probe instance produced no audio\n"), ++fails;

            // The editor destructor calls this; simulate a window closed mid-note.
            r.releaseAllUiNotes();
            if (r.uiHeldCount() != 0)
                std::printf ("license: releaseAllUiNotes left held notes behind\n"), ++fails;

            // Flush the rescue delivery and let the natural release tail (default
            // 0.4 s) decay; only then must the output be truly silent. A note
            // that never received its note-off would still be sustaining here.
            runPeak (50);
            double tailPk = 0.0;
            for (int pass = 0; pass < 20; ++pass)
            {
                buf.clear();
                r.processBlock (buf, emptyMidi);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < buf.getNumSamples(); ++i)
                        tailPk = juce::jmax (tailPk, (double) std::fabs (buf.getSample (ch, i)));
            }
            if (tailPk > 1.0e-4)
                std::printf ("license: stuck note survived releaseAllUiNotes\n"), ++fails;
        }
        goa::License::deactivate();

        // Reset the sandbox FILES, but keep the env overrides in place.
        //
        // This used to also _putenv each variable to empty, which dropped the
        // process back onto the real paths for the remaining ~400 lines of the
        // suite. Everything below still activates (see the vowel-filter probe),
        // so that silently wrote the developer's own
        // %APPDATA%/GoaSynth/license.txt and - worse - bound test serials into
        // the MACHINE-WIDE ledger at C:\Users\Public\Documents\GoaSynth\License
        // \machines.txt, which every Windows account on the PC shares. A test
        // must not spend a real machine binding. Deleting the temp files is all
        // that is needed to put the store back to "not licensed".
        lic.deleteFile();
        led.deleteFile();
        trial.deleteFile();
    }

    // Unlicensed instances must render silence.
    {
        GoaSynthAudioProcessor locked;
        juce::AudioBuffer<float> buf (2, 512);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                buf.setSample (ch, i, 0.37f * (float) (ch + 1));

        juce::MidiBuffer emptyMidi;
        locked.processBlock (buf, emptyMidi);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                if (buf.getSample (ch, i) != 0.0f)
                {
                    std::printf ("license: unlicensed instance produced audio\n");
                    ++fails;
                    break;
                }

        // prepareToPlay is not called in this test process; force-prepare via
        // a fresh instance is unnecessary: processBlock must zero regardless.
    }

    // A licensed instance still renders audio (smoke check through the real path).
    // Activating the sandbox store (rather than poking licensedFlag by hand) is
    // load-bearing since processBlock re-checks the store per block: a manually
    // forced flag would be silently corrected back to locked and the block
    // would render silence.
    {
        juce::String smokeErr;
        goa::License::activate (GOA_TEST_MASTER_KEY, smokeErr);
        GoaSynthAudioProcessor inst;
        inst.prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> buf (2, 512);
        buf.clear();
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
        inst.processBlock (buf, midi);
        midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
        inst.processBlock (buf, midi);

        double peak = 0.0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                peak = juce::jmax (peak, (double) std::abs (buf.getSample (ch, i)));

        if (! (peak > 0.001))
            std::printf ("license: licensed instance produced no audio\n"), ++fails;
    }

    file.deleteFile();
    dir.deleteRecursively();

    // ========================================================================
    phase ("12) cloud AI parsing");
    // 12) Cloud AI reply parsing: fenced JSON accepted, junk rejected, unknown
    //     ids dropped, values clamped, choice synonyms understood.
    {
        // Fenced + prose-wrapped JSON extracts fine.
        auto r1 = AiCloudGen::parseReply (
            "Sure! Here is your patch:\n```json\n{\"title\":\"ACID RUSH\","
            "\"description\":\"squelchy\",\"patch\":{\"osc1Wave\":\"Saw\","
            "\"cutoff\":4500.0,\"reso\":0.8,\"filterType\":\"lowpass\"}}\n```\n"
            "Let me know if you want variations!");
        if (! r1.ok || r1.title != "ACID RUSH"
            || r1.patch.count ("cutoff") != 1 || std::abs (r1.patch.at ("cutoff") - 4500.0f) > 1e-3
            || r1.patch.count ("filterType") != 1 || r1.patch.at ("filterType") != 0.0f
            || r1.patch.count ("osc1Wave") != 1 || r1.patch.at ("osc1Wave") != 0.0f)
            std::printf ("cloud: fenced JSON reply did not parse\n"), ++fails;

        // Out-of-range values clamp; unknown ids drop; bogus choice names drop.
        auto r2 = AiCloudGen::parseReply (
            "{\"title\":\"X\",\"patch\":{\"cutoff\":999999.0,\"ampR\":-3.0,"
            "\"madeUpParam\":42,\"osc2Wave\":\"banjo\",\"uniVoices\":99,"
            "\"subOct\":\"-2\"}}");
        if (! r2.ok
            || r2.patch.count ("madeUpParam") != 0 || r2.patch.count ("osc2Wave") != 0
            || r2.patch.count ("cutoff") != 1 || r2.patch.at ("cutoff") != 20000.0f
            || r2.patch.count ("ampR") != 1 || r2.patch.at ("ampR") != 0.01f
            || r2.patch.count ("uniVoices") != 1 || r2.patch.at ("uniVoices") != 7.0f
            || r2.patch.count ("subOct") != 1 || r2.patch.at ("subOct") != -2.0f)
            std::printf ("cloud: sanitizer failed to clamp/drop values\n"), ++fails;

        // Garbage input is rejected with an error, never a half-patch.
        auto r3 = AiCloudGen::parseReply ("no json here at all");
        if (r3.ok || r3.error.isEmpty())
            std::printf ("cloud: garbage reply accepted\n"), ++fails;
        auto r4 = AiCloudGen::parseReply ("{\"title\":\"X\",\"patch\":{}}");
        if (r4.ok)
            std::printf ("cloud: empty patch accepted\n"), ++fails;

        // The prompt template must enumerate the real parameters.
        if (! AiCloudGen::buildPrompt().contains ("osc1Wave")
            || ! AiCloudGen::buildPrompt().contains ("cutoff"))
            std::printf ("cloud: prompt missing parameter vocabulary\n"), ++fails;

        // Model catalog: newest-first ordering with Gemini 3.6 Flash as the
        // Gemini default (2.0 Flash was shut down by Google) and GPT-5.6 Luna
        // leading the OpenAI list.
        const auto gm = AiCloudGen::modelNames (AiCloudGen::Engine::Gemini);
        if (gm.isEmpty() || ! gm[0].contains ("3.6"))
            std::printf ("cloud: Gemini default model is not 3.6 Flash\n"), ++fails;
        const auto om = AiCloudGen::modelNames (AiCloudGen::Engine::OpenAI);
        if (om.isEmpty() || ! om[0].contains ("5.6"))
            std::printf ("cloud: OpenAI default model is not 5.6-era\n"), ++fails;
        if (AiCloudGen::modelNames (AiCloudGen::Engine::Custom).isEmpty())
            std::printf ("cloud: Custom engine must expose a model entry\n"), ++fails;

        // TEST KEY offline validation paths (no network needed).
        auto t1 = AiCloudGen::testConnection (AiCloudGen::Engine::OpenAI, "short", "GPT-5.6 LUNA");
        if (t1.ok || t1.error.isEmpty())
            std::printf ("cloud: testConnection accepted a bogus credential\n"), ++fails;
        auto t2 = AiCloudGen::testConnection (AiCloudGen::Engine::Custom, "notaurl", "SERVER DEFAULT");
        if (t2.ok || ! t2.error.contains ("base URL"))
            std::printf ("cloud: testConnection accepted a non-URL endpoint\n"), ++fails;
        auto t3 = AiCloudGen::testConnection (AiCloudGen::Engine::Gemini, "");
        if (t3.ok || t3.error.isEmpty())
            std::printf ("cloud: testConnection accepted an empty key\n"), ++fails;

        // Reply-envelope extraction: OpenAI choices shape, Gemini candidates
        // shape (multi-part concatenation), and raw-text passthrough.
        const auto oa = AiCloudGen::extractReplyText (AiCloudGen::Engine::OpenAI,
            "{\"choices\":[{\"message\":{\"content\":\"ACID RUSH\"}}]}");
        if (oa != "ACID RUSH")
            std::printf ("cloud: OpenAI envelope not extracted\n"), ++fails;
        const auto gg = AiCloudGen::extractReplyText (AiCloudGen::Engine::Gemini,
            "{\"candidates\":[{\"content\":{\"parts\":[{\"text\":\"ACID \"},{\"text\":\"RUSH\"}]}}]}");
        if (gg != "ACID RUSH")
            std::printf ("cloud: Gemini envelope not extracted\n"), ++fails;
        if (AiCloudGen::extractReplyText (AiCloudGen::Engine::Custom, "plain reply") != "plain reply")
            std::printf ("cloud: raw text must pass through untouched\n"), ++fails;

        // Correction follow-up message carries the reason, details, format rules.
        const auto cm = AiCloudGen::buildCorrectionMessage ("no known parameters survived validation",
                                                            "- unknown parameter id \"foo\"");
        if (! cm.contains ("no known parameters") || ! cm.contains ("ONLY a JSON object")
            || ! cm.contains ("foo"))
            std::printf ("cloud: correction message malformed\n"), ++fails;

        // Reject log records exactly what the sanitizer dropped or clamped.
        juce::String log;
        auto rp = AiCloudGen::parseReply (
            "{\"title\":\"X\",\"patch\":{\"cutoff\":500,\"madeUp\":1,\"reso\":\"bogus\"}}", &log);
        if (! rp.ok || rp.patch.size() != 1
            || ! log.contains ("madeUp") || ! log.contains ("reso"))
            std::printf ("cloud: reject log incomplete\n"), ++fails;
    }

    // ========================================================================
    phase ("13) learned-patch memory");
    // 13) Learned-patch memory: archive cloud successes, match briefs, and
    //     augment offline generations. Runs against a scratch directory.
    {
        auto scratch = juce::File::getSpecialLocation (juce::File::tempDirectory)
                           .getChildFile ("goasynth_learned_test");
        scratch.deleteRecursively();
        scratch.createDirectory();
        learned::setDirOverride (&scratch);

        // Empty bank: no matches, no augmentation.
        if (! learned::bestMatch ("acid bass").empty())
            std::printf ("learned: empty bank produced matches\n"), ++fails;

        // Record two cloud successes.
        std::map<juce::String, float> p1 { { "cutoff", 500.0f }, { "reso", 0.9f }, { "ampD", 0.2f } };
        std::map<juce::String, float> p2 { { "ampA", 2.0f }, { "revMix", 0.8f } };
        if (! learned::record ("squelchy 303 acid bass line", "ACID RUSH", p1, "GEMINI", "gemini-3.6-flash")
            || ! learned::record ("wide dreamy closing pad", "DREAM PAD", p2, "OPENAI", "gpt-5.6-luna"))
            std::printf ("learned: record failed to write\n"), ++fails;

        const auto all = learned::loadAll();

        // Order-independent: the archive is written with UUID filenames, so
        // which entry the OS lists first is not ours to assume (this check used
        // to index [0] and throw std::out_of_range on half the runs).
        auto hasValue = [] (const learned::Entry& e, const char* id, float want)
        {
            const auto it = e.patch.find (id);
            return it != e.patch.end() && it->second == want;
        };
        bool acidOk = false, padOk = false;
        for (const auto& e : all)
        {
            if (e.title == "ACID RUSH" && hasValue (e, "cutoff", 500.0f)
                && hasValue (e, "reso", 0.9f))
                acidOk = true;
            if (e.title == "DREAM PAD" && hasValue (e, "ampA", 2.0f)
                && hasValue (e, "revMix", 0.8f))
                padOk = true;
        }
        if (all.size() != 2 || ! acidOk || ! padOk)
            std::printf ("learned: round-trip through disk failed\n"), ++fails;

        // Brief matching: acid brief finds the acid entry, pad brief the pad.
        const auto acidHits = learned::bestMatch ("dark acid bass re-roll", 2);
        if (acidHits.empty() || acidHits[0].title != "ACID RUSH")
            std::printf ("learned: acid brief did not match acid entry\n"), ++fails;
        const auto padHits = learned::bestMatch ("slow dreamy pad", 2);
        if (padHits.empty() || padHits[0].title != "DREAM PAD")
            std::printf ("learned: pad brief did not match pad entry\n"), ++fails;

        // Unrelated brief scores below the threshold.
        if (! learned::bestMatch ("quantum fusion toaster").empty())
            std::printf ("learned: unrelated brief matched anyway\n"), ++fails;

        // Title-only match also works (brief words do not overlap).
        if (learned::bestMatch ("acid rush style again").empty())
            std::printf ("learned: title match failed\n"), ++fails;

        learned::setDirOverride (nullptr);
        scratch.deleteRecursively();
    }

    // ========================================================================
    phase ("14) scale quantizer");
    // 14) Scale quantizer: arp-scale snapping with root reference.
    {
        // PHRYGIAN (index 2) on root E = {E,F,G,A,B,C,D}. Absolute notes:
        // E4=64, F4=65, F#4=66, G4=67, G#4=68, B4=71, C5=72, C#5=73.
        if (tuning::nearestScaleNote (65, 2, 4) != 65)   // F (b2) stays
            std::printf ("scale: F should stay in Phrygian\n"), ++fails;
        if (tuning::nearestScaleNote (66, 2, 4) != 65)   // F# -> F
            std::printf ("scale: F# did not snap down to F\n"), ++fails;
        if (tuning::nearestScaleNote (67, 2, 4) != 67)   // G stays
            std::printf ("scale: G should stay\n"), ++fails;
        if (tuning::nearestScaleNote (68, 2, 4) != 67)   // G# -> G
            std::printf ("scale: G# did not snap to G\n"), ++fails;
        if (tuning::nearestScaleNote (71, 2, 4) != 71)   // B stays
            std::printf ("scale: B should stay\n"), ++fails;
        // C#5 sits 1 below D5 and 1 above C5, both in scale: tie snaps down.
        if (tuning::nearestScaleNote (73, 2, 4) != 72)
            std::printf ("scale: tie-break snapped the wrong way\n"), ++fails;
        // CHROMATIC (0) never changes anything.
        if (tuning::nearestScaleNote (61, 0, 4) != 61)
            std::printf ("scale: chromatic quantized\n"), ++fails;
        // PENTA MIN {0,3,5,7,10} on A: B is 2 from A but 1 from C -> C.
        if (tuning::nearestScaleNote (71, 8, 9) != 72)
            std::printf ("scale: pentatonic snap wrong\n"), ++fails;
    }

    // ========================================================================
    phase ("15) Scala microtuning");
    // 15) Scala microtuning: parser, octave wrapping, A4 anchoring, fine.
    {
        juce::String desc;
        std::vector<double> deg;
        const auto sclText = juce::String ("! 5-EDO test\n\nfive\n5\n240.\n480.\n720.\n960.\n1200.\n");
        if (! tuning::parseScl (sclText, desc, deg) || desc != "five" || (int) deg.size() != 6)
            std::printf ("tuning: SCL parse failed\n"), ++fails;
        if (std::abs (deg[3] - 720.0) > 1e-6)
            std::printf ("tuning: degree cents wrong\n"), ++fails;

        // Just intonation major third 5/4 = 386.31 cents.
        std::vector<double> degJ;
        tuning::parseScl (juce::String ("ji\n2\n5/4\n2/1\n"), desc, degJ);
        if (std::abs (degJ[1] - 386.3137) > 0.01)
            std::printf ("tuning: ratio parse wrong\n"), ++fails;

        // A4 stays at 440 Hz; C4 sits at the 5-EDO degree; fine shifts up.
        const auto f69 = tuning::freqForNote (deg, 69);
        if (std::abs (f69 - 440.0) > 0.01)
            std::printf ("tuning: A4 not anchored to 440\n"), ++fails;
        const auto fC4 = tuning::freqForNote (deg, 60);
        // C4 is 9 steps below A4 in a 5-note scale: -9 * 240 = -2160 cents.
        if (std::abs (fC4 - 440.0 * std::pow (2.0, -2160.0 / 1200.0)) > 0.01)
            std::printf ("tuning: 5-EDO C4 wrong\n"), ++fails;
        // Note 74 is one full 5-EDO octave above A4: exactly +1200 cents.
        if (std::abs (tuning::freqForNote (deg, 69 + 5) - 880.0) > 0.01)
            std::printf ("tuning: octave wrap wrong\n"), ++fails;
        if (tuning::freqForNote (deg, 60, 100.0) <= fC4)
            std::printf ("tuning: fine offset did not raise pitch\n"), ++fails;

        // 432 Hz base: A4 lands on 432.
        if (std::abs (tuning::freqForNote ({}, 69, 0.0, 432.0) - 432.0) > 0.01)
            std::printf ("tuning: 432 Hz base wrong\n"), ++fails;

        // Malformed input rejected.
        if (tuning::parseScl (juce::String ("bad\n1\n100.\n"), desc, deg))
            std::printf ("tuning: 1-degree scale accepted\n"), ++fails;
        if (tuning::parseScl (juce::String ("bad\n3\n100.\ncat/5\n300.\n"), desc, deg))
            std::printf ("tuning: garbage ratio accepted\n"), ++fails;

        // Lock-free table round trip.
        tuning::ScalaTable t;
        if (! t.loadFromText (sclText))
            std::printf ("tuning: ScalaTable load failed\n"), ++fails;
        if (! t.isLoaded() || t.getDegreeCount() != 5 || t.getName() != "five")
            std::printf ("tuning: ScalaTable metadata wrong\n"), ++fails;  // count = degrees/octave   // 5 degrees = n+1 entries
        if (std::abs (t.frequencyForNote (69) - 440.0) > 0.01)
            std::printf ("tuning: ScalaTable A4 wrong\n"), ++fails;
        t.clear();
        if (t.isLoaded() || std::abs (t.frequencyForNote (69) - 440.0) > 0.01)
            std::printf ("tuning: clear did not restore 12-TET\n"), ++fails;
    }

    // ========================================================================
    phase ("16) vowel filter");
    // 16) Vowel/formant filter renders audibly and morphs.
    {
        juce::String probeErr;
        goa::License::setTestMachineId ("2D6EFDDAA1AC30F510C8");
        goa::License::activate (GOA_TEST_MASTER_KEY, probeErr);   // before construction:
        GoaSynthAudioProcessor probe;                      // ctor snapshots license
        probe.prepareToPlay (48000.0, 512);

        auto setF = [&probe] (const char* id, float v)
        { if (auto* p = probe.apvts.getRawParameterValue (id)) p->store (v); };

        setF (param::vowelOn, 1.0f);
        setF (param::vowelMix, 1.0f);
        setF (param::vowelMorph, 0.0f);          // pure AH
        setF (param::masterGain, 0.0f);

        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
        probe.processBlock (buf, midi);

        double sum = 0.0;
        for (int i = 0; i < 512; ++i)
            sum += std::abs (buf.getSample (0, i)) + std::abs (buf.getSample (1, i));
        if (sum < 1.0e-4)
            std::printf ("vowel: no output with vowel filter on\n"), ++fails;

        // Morph to OO must change the sample stream (formants moved).
        setF (param::vowelMorph, 1.0f);
        juce::AudioBuffer<float> buf2 (2, 512);
        juce::MidiBuffer midi2;
        midi2.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
        probe.processBlock (buf2, midi2);
        double sum2 = 0.0;
        for (int i = 0; i < 512; ++i)
            sum2 += std::abs (buf2.getSample (0, i)) + std::abs (buf2.getSample (1, i));
        if (std::abs (sum - sum2) < 1.0e-6)
            std::printf ("vowel: morph position had no effect\n"), ++fails;

        setF (param::vowelOn, 0.0f);
    }

    phase ("17) pump / filter drive");
    // 15) Sidechain pump curve, filter drive/feedback safety, and the new
    //     character parameters flowing through a live render.
    {
        using goa::pumpGain;

        // Deepest right on the beat, recovered before the next one, unity off.
        if (! (pumpGain (0.0, 0.8f) < 0.25f))
            std::printf ("pump: phase 0 is not the deepest dip\n"), ++fails;
        if (std::abs (pumpGain (0.999, 0.8f) - 1.0f) > 1.0e-3)
            std::printf ("pump: end of period not recovered\n"), ++fails;
        if (pumpGain (0.5, 0.0f) != 1.0f)
            std::printf ("pump: depth 0 is not unity\n"), ++fails;
        if (! (pumpGain (0.2, 0.8f) > pumpGain (0.1, 0.8f)))
            std::printf ("pump: dip is not monotonic\n"), ++fails;
        if (pumpGain (0.0, 4.0f) < 0.049f || pumpGain (0.0, 4.0f) > 0.051f)
            std::printf ("pump: depth cap is not 95%%\n"), ++fails;

        GoaSynthAudioProcessor r;
        r.prepareToPlay (48000.0, 512);

        auto setF = [&r] (const char* id, float v)
        { if (auto* p = r.apvts.getRawParameterValue (id)) p->store (v); };

        setF (param::masterGain, 0.0f);
        setF (param::fDrive, 0.9f);
        setF (param::fFeedback, 0.7f);

        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
        double peak = 0.0;
        for (int blk = 0; blk < 20; ++blk)
        {
            buf.clear();
            r.processBlock (buf, midi);
            midi.clear();
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < buf.getNumSamples(); ++i)
                    peak = juce::jmax (peak,
                        (double) std::fabs (buf.getSample (ch, i)));
        }
        if (peak < 1.0e-4)
            std::printf ("filterchar: no output with drive+feedback\n"), ++fails;
        if (peak > 4.0)
            std::printf ("filterchar: feedback path escaped its safety clamp\n"), ++fails;

        // Analog character must change the pitch path: render with it off vs
        // on and compare the streams (drift makes them differ).
        setF (param::fDrive, 0.0f);
        setF (param::fFeedback, 0.0f);
        juce::AudioBuffer<float> plain (2, 512), wob (2, 512);
        for (int pass = 0; pass < 2; ++pass)
        {
            setF (param::analogAmt, pass == 0 ? 0.0f : 0.9f);
            juce::MidiBuffer m;
            m.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
            r.processBlock (pass == 0 ? plain : wob, m);
        }
        double diff = 0.0;
        for (int i = 0; i < 512; ++i)
            diff += std::abs (plain.getSample (0, i) - wob.getSample (0, i));
        if (diff < 1.0e-5)
            std::printf ("analog: character amount had no audible effect\n"), ++fails;

        // HQ mode: 2x internal oversampling of the osc+filter path. Output
        // must stay audible, differ from the plain path, and remain bounded.
        setF (param::analogAmt, 0.0f);
        juce::AudioBuffer<float> plainBuf (2, 512), hqBuf (2, 512);
        for (int pass = 0; pass < 2; ++pass)
        {
            setF (param::masterHQ, pass == 0 ? 0.0f : 1.0f);
            juce::MidiBuffer m;
            m.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
            r.processBlock (pass == 0 ? plainBuf : hqBuf, m);
        }
        double hqDiff = 0.0, hqPeak = 0.0;
        for (int i = 0; i < 512; ++i)
        {
            hqDiff += std::abs (plainBuf.getSample (0, i) - hqBuf.getSample (0, i));
            hqPeak = juce::jmax (hqPeak, (double) std::fabs (hqBuf.getSample (0, i)));
        }
        if (hqDiff < 1.0e-5)
            std::printf ("hq: oversampling had no effect on the render\n"), ++fails;
        if (hqPeak < 1.0e-4 || hqPeak > 4.0)
            std::printf ("hq: level out of range\n"), ++fails;

        // Flipping HQ mid-render must stay stable (parity state sets are
        // independent, and the smoother already runs rate-normalised).
        setF (param::masterHQ, 1.0f);
        {
            juce::MidiBuffer m;
            m.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
            double pk = 0.0;
            for (int blk = 0; blk < 10; ++blk)
            {
                buf.clear();
                r.processBlock (buf, m);
                for (int i = 0; i < 512; ++i)
                    pk = juce::jmax (pk, (double) std::fabs (buf.getSample (0, i)));
            }
            if (pk > 4.0)
                std::printf ("hq: level escaped after mode flip\n"), ++fails;
        }
        setF (param::masterHQ, 0.0f);

        setF (param::analogAmt, 0.0f);
        setF (param::masterGain, -6.0f);
    }

    phase ("18) macros / curve-lag / bank B / chord / shimmer & duck");
    {
        GoaSynthAudioProcessor r;
        r.prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> buf (2, 512);

        auto setF = [&r] (const char* id, float v)
        { if (auto* p = r.apvts.getRawParameterValue (id)) p->store (v); };
        auto setP = [&r] (const char* id, float v)
        {
            if (auto* p = r.apvts.getParameter (id))
                p->setValueNotifyingHost (p->convertTo0to1 (v));
        };

        // Voice accounting for the note-off symmetry checks below: a note-off
        // that addresses the wrong note leaves the voice sounding forever.
        auto activeVoices = [&r]
        {
            int n = 0;
            for (int i = 0; i < r.synth.getNumVoices(); ++i)
                if (r.synth.getVoice (i)->isVoiceActive())
                    ++n;
            return n;
        };

        // Runs the midi events, then keeps rendering so any release tail has
        // finished before the next voice count. JUCE's Synthesiser::noteOn stops
        // a same-note voice with a tail, so a replaced voice stays active for a
        // few blocks - counting too early would read it as a leak.
        auto render = [&r, &buf] (juce::MidiBuffer& m, int blocks)
        {
            for (int i = 0; i < blocks; ++i)
            {
                buf.clear();
                r.processBlock (buf, m);
                m.clear();
            }
        };

        auto voicesPlaying = [&r] (int note)
        {
            int n = 0;
            for (int i = 0; i < r.synth.getNumVoices(); ++i)
                if (r.synth.getVoice (i)->isVoiceActive()
                      && r.synth.getVoice (i)->getCurrentlyPlayingNote() == note)
                    ++n;
            return n;
        };

        // MACRO A drives the matrix: velocity->cutoff amounts must move the
        // render when the macro knob moves (the same path MIDI CC 14 feeds).
        setF (param::modSrc (0).toRawUTF8(), 9.0f);    // MACRO A
        setF (param::modDst (0).toRawUTF8(), 1.0f);    // CUTOFF
        setF (param::modAmt (0).toRawUTF8(), 1.0f);
        juce::AudioBuffer<float> low (2, 512), high (2, 512);
        for (int pass = 0; pass < 2; ++pass)
        {
            setF (param::macroA, pass == 0 ? 0.0f : 1.0f);
            juce::MidiBuffer m;
            m.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
            r.processBlock (pass == 0 ? low : high, m);
        }
        double macroDiff = 0.0, macroPeak = 0.0;
        for (int i = 0; i < 512; ++i)
        {
            macroDiff += std::abs (low.getSample (0, i) - high.getSample (0, i));
            macroPeak = juce::jmax (macroPeak,
                (double) std::fabs (high.getSample (0, i)));
        }
        if (macroDiff < 1.0e-5)
            std::printf ("macros: MACRO A source had no effect on the render\n"), ++fails;
        if (macroPeak < 1.0e-4 || macroPeak > 4.0)
            std::printf ("macros: level out of range\n"), ++fails;

        // Curve shaping: EXP must differ from LIN at the same partial setting.
        setF (param::macroA, 0.5f);
        juce::AudioBuffer<float> lin (2, 512), expc (2, 512);
        for (int pass = 0; pass < 2; ++pass)
        {
            setF (param::modCurve (0).toRawUTF8(), pass == 0 ? 0.0f : 1.0f);
            juce::MidiBuffer m;
            m.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
            r.processBlock (pass == 0 ? lin : expc, m);
        }
        double curveDiff = 0.0;
        for (int i = 0; i < 512; ++i)
            curveDiff += std::abs (lin.getSample (0, i) - expc.getSample (0, i));
        if (curveDiff < 1.0e-5)
            std::printf ("modcurve: EXP shaping did not change the render\n"), ++fails;
        setF (param::modCurve (0).toRawUTF8(), 0.0f);

        // Lag: a jump in the macro value must slew when lag is high.
        juce::AudioBuffer<float> tight (2, 512), laggy (2, 512);
        for (int pass = 0; pass < 2; ++pass)
        {
            setF (param::modLag (0).toRawUTF8(), pass == 0 ? 0.0f : 0.95f);
            setF (param::macroA, 0.0f);
            { juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
              r.processBlock (pass == 0 ? tight : laggy, m); }
            setF (param::macroA, 1.0f);
            { juce::MidiBuffer m; r.processBlock (pass == 0 ? tight : laggy, m); }
        }
        double lagDiff = 0.0;
        for (int i = 0; i < 512; ++i)
            lagDiff += std::abs (tight.getSample (0, i) - laggy.getSample (0, i));
        if (lagDiff < 1.0e-5)
            std::printf ("modlag: lag had no smoothing effect\n"), ++fails;
        setF (param::modLag (0).toRawUTF8(), 0.0f);

        // Bank B: a routing that lives only in bank B must pass through only
        // when the bank switch is on (bank A stays empty throughout).
        setF (param::modSrc (0).toRawUTF8(), 9.0f);
        setF (param::modAmt (0).toRawUTF8(), 0.0f);
        setF (param::modBSrc (0).toRawUTF8(), 5.0f);   // VELOCITY
        setF (param::modBDst (0).toRawUTF8(), 1.0f);   // CUTOFF
        setF (param::modBAmt (0).toRawUTF8(), 1.0f);
        juce::AudioBuffer<float> bankA (2, 512), bankB (2, 512);
        for (int pass = 0; pass < 2; ++pass)
        {
            setF (param::modBank, pass == 0 ? 0.0f : 1.0f);
            juce::MidiBuffer m;
            m.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
            r.processBlock (pass == 0 ? bankA : bankB, m);
        }
        double bankDiff = 0.0;
        for (int i = 0; i < 512; ++i)
            bankDiff += std::abs (bankA.getSample (0, i) - bankB.getSample (0, i));
        if (bankDiff < 1.0e-5)
            std::printf ("modbank: bank B routing did not take over\n"), ++fails;
        setF (param::modBank, 0.0f);
        setF (param::modBSrc (0).toRawUTF8(), 0.0f);
        setF (param::modBAmt (0).toRawUTF8(), 0.0f);

        // Chord memory + supersaw character + S&H + aftertouch sources: the
        // full new surface must render finite, audible and bounded.
        setF (param::chordMode, 3.0f);     // MAJOR
        setF (param::uniMode, 2.0f);       // HYPER
        setF (param::modSrc (1).toRawUTF8(), 7.0f);    // S&H
        setF (param::modDst (1).toRawUTF8(), 1.0f);
        setF (param::modAmt (1).toRawUTF8(), 0.8f);
        setF (param::modSrc (2).toRawUTF8(), 8.0f);    // AFTERTOUCH
        setF (param::modDst (2).toRawUTF8(), 9.0f);
        setF (param::modAmt (2).toRawUTF8(), 0.5f);
        {
            juce::MidiBuffer m;
            m.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
            m.addEvent (juce::MidiMessage::channelPressureChange (1, 0.8f), 0);
            double pk = 0.0;
            bool fin = true;
            for (int blk = 0; blk < 8; ++blk)
            {
                buf.clear();
                r.processBlock (buf, m);
                m.clear();
                for (int i = 0; i < 512; ++i)
                {
                    const float s = buf.getSample (0, i);
                    fin &= std::isfinite (s);
                    pk = juce::jmax (pk, (double) std::fabs (s));
                }
            }
            if (! fin || pk < 1.0e-4 || pk > 4.0)
                std::printf ("chords/uni/sh: render out of range (pk %f)\n", pk), ++fails;
        }

        // Shimmer + duck with a wet reverb: finite, audible, bounded, and the
        // whole chain must stay silent-safe when the note stops.
        setF (param::chordMode, 0.0f);
        setF (param::uniMode, 0.0f);
        setF (param::modSrc (1).toRawUTF8(), 0.0f);
        setF (param::modAmt (1).toRawUTF8(), 0.8f);
        setF (param::modSrc (2).toRawUTF8(), 0.0f);
        setF (param::modAmt (2).toRawUTF8(), 0.5f);
        setF (param::revMix, 0.6f);
        setF (param::revShimmer, 0.7f);
        setF (param::duckAmt, 0.9f);
        {
            juce::MidiBuffer m;
            m.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
            double pk = 0.0;
            bool fin = true;
            for (int blk = 0; blk < 12; ++blk)
            {
                buf.clear();
                r.processBlock (buf, m);
                m.clear();
                for (int i = 0; i < 512; ++i)
                {
                    const float s = buf.getSample (0, i);
                    fin &= std::isfinite (s);
                    pk = juce::jmax (pk, (double) std::fabs (s));
                }
            }
            if (! fin || pk < 1.0e-4 || pk > 4.0)
                std::printf ("shimmer/duck: render out of range (pk %f)\n", pk), ++fails;
        }
        setF (param::revMix, 0.0f);
        setF (param::revShimmer, 0.0f);
        setF (param::duckAmt, 0.0f);
        setF (param::masterGain, -6.0f);

        // ---- note-off symmetry (all three shipped as stuck/vanishing notes) --
        // Several phases above note-on without a matching note-off, so clear the
        // synth first: the voice counts below must be about this block alone.
        r.synth.allNotesOff (1, false);

        setF (param::voicing, 0.0f);            // POLY
        setF (param::ampR, 0.02f);              // short tail, so silence is reachable
        setF (param::filtR, 0.02f);
        setF (param::chordMode, 0.0f);
        setF (param::scaleLock, 0.0f);

        // (a) Scale Lock: noteOn snaps an out-of-scale key onto the scale, so
        // noteOff has to snap identically or the snapped voice is never stopped.
        setF (param::arpScale, 1.0f);           // MINOR
        setF (param::arpRoot, 0.0f);            // C: note 61 is out of scale, snaps to 60
        setF (param::scaleLock, 1.0f);
        {
            juce::MidiBuffer m;
            m.addEvent (juce::MidiMessage::noteOn (1, 61, 0.9f), 0);
            render (m, 4);

            if (activeVoices() != 1)
                std::printf ("scale lock: note-on started %d voices, expected 1\n",
                             activeVoices()), ++fails;

            m.addEvent (juce::MidiMessage::noteOff (1, 61), 0);
            render (m, 16);

            if (activeVoices() != 0)
                std::printf ("scale lock: note-off left the snapped voice stuck (%d active)\n",
                             activeVoices()), ++fails;
        }
        setF (param::scaleLock, 0.0f);

        // (b) Chord memory: releasing a key that is also another held key's
        // chord tone must not silence that tone. It used to, because releasing
        // the directly-played 5th stopped the voice the root's chord tone was
        // sounding through.
        setF (param::chordMode, 1.0f);          // 5TH
        {
            juce::MidiBuffer m;
            m.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
            render (m, 4);

            m.addEvent (juce::MidiMessage::noteOn (1, 67, 0.9f), 0);  // the 5th, played directly
            render (m, 4);

            // 60 (root) + 67 (its 5th) + 74 (the 5th's own 5th, since every key
            // gets chord memory).
            if (voicesPlaying (60) != 1 || voicesPlaying (67) != 1 || voicesPlaying (74) != 1)
                std::printf ("chord memory: expected 60/67/74 to sound, got 60:%d 67:%d 74:%d\n",
                             voicesPlaying (60), voicesPlaying (67), voicesPlaying (74)), ++fails;

            m.addEvent (juce::MidiMessage::noteOff (1, 67), 0);
            render (m, 4);

            // The regression: the root's 5th survives releasing the key that
            // plays it, while that key's own chord tone is released with it.
            if (voicesPlaying (67) != 1)
                std::printf ("chord memory: releasing the 5th silenced the root's chord tone\n"), ++fails;
            if (voicesPlaying (74) != 0)
                std::printf ("chord memory: the released key's own chord tone kept sounding\n"), ++fails;
            if (voicesPlaying (60) != 1)
                std::printf ("chord memory: the still-held root was stopped early\n"), ++fails;

            m.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
            render (m, 16);

            if (activeVoices() != 0)
                std::printf ("chord memory: %d voice(s) leaked after release\n",
                             activeVoices()), ++fails;
        }

        // (c) The chord tones are remembered per root, so changing CHORD while a
        // key is still down cannot strand the tones that key already started.
        setF (param::chordMode, 4.0f);          // OCT
        {
            juce::MidiBuffer m;
            m.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
            render (m, 4);

            setF (param::chordMode, 0.0f);      // OFF, key still down

            m.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
            render (m, 16);

            if (activeVoices() != 0)
                std::printf ("chord memory: a CHORD change stranded %d voice(s)\n",
                             activeVoices()), ++fails;
        }
        setF (param::chordMode, 0.0f);
    }

    // ========================================================================
    phase ("19) arp gate-length + strum (fake playing host)");
    // A FakePlayHead makes the host transport "playing", so the arp clock runs:
    // per-step gate length must schedule note-offs at the programmed fraction,
    // accents must hold longer, and STRUM must fire every held note per step.
    {
        goa::License::setTestMachineId ("2D6EFDDAA1AC30F510C8");
        juce::String arpErr;
        goa::License::activate (GOA_TEST_MASTER_KEY, arpErr);
        GoaSynthAudioProcessor arp;                        // ctor snapshots license
        FakePlayHead fake;
        arp.testPlayHead = &fake;   // test-only transport injection
        arp.prepareToPlay (48000.0, 512);

        auto setA = [&arp] (const char* id, float v)
        { if (auto* p = arp.apvts.getRawParameterValue (id)) p->store (v); };

        // Arp pattern: step 2 = +0, step 5 = +7 at full velocity (accent),
        // everything else rests; 1/16 steps (choice idx 1 = 0.25 quarters;
        // idx 0 is 1/32).
        setA (param::arpSync, 1.0f);
        setA (param::arpOct, 1.0f);
        setA (param::arpDir, 0.0f);              // UP
        for (int i = 0; i < 16; ++i)
        {
            setA (param::arpStep (i).toRawUTF8(), 0.0f);
            setA (param::arpVel (i).toRawUTF8(), 0.55f);
            setA (param::arpGate (i).toRawUTF8(), 0.75f);
        }
        setA (param::arpStep (1).toRawUTF8(), 1.0f);   // step idx 1: +0 semitone
        setA (param::arpStep (2).toRawUTF8(), 5.0f);   // step idx 2: +4 semitones
        setA (param::arpStep (4).toRawUTF8(), 8.0f);   // step idx 4: +7 semitones
        setA (param::arpVel (4).toRawUTF8(), 1.0f);    // accent

        // Hold one note; render while advancing the fake transport.
        juce::MidiBuffer held;
        held.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);

        int noteOns = 0;
        double offQuartersStep0 = -1.0;   // note-off position of the +0 step
        const double spq = 0.25;          // 1/16 at 120 bpm, in quarter notes

        auto render = [&] (int blocks)
        {
            for (int b = 0; b < blocks; ++b)
            {
                juce::AudioBuffer<float> buf (2, 512);
                juce::MidiBuffer m;
                // A real host keeps the key down; per-block MIDI streams carry
                // no state, so re-assert the held note-ons every block (the
                // processor's removeFirstMatchingValue keeps duplicates from
                // stacking in arpNotesHeld).
                m.addEvents (held, 0, -1, 0);
                arp.processBlock (buf, m);

                for (const auto& ev : m)
                {
                    const auto msg = ev.getMessage();
                    const double q = fake.ppq + ev.samplePosition / 48000.0 * 2.0;
                    if (msg.isNoteOn())  ++noteOns;
                    if (msg.isNoteOff() && msg.getNoteNumber() == 60 && offQuartersStep0 < 0.0)
                        offQuartersStep0 = q;
                }
                fake.ppq += 512 / 48000.0 * 2.0;
            }
        };

        render (40);
        // The fake transport must actually have been consulted (hook sanity).
        if (fake.calls == 0)
            std::printf ("arp19: test play head was never consulted\n"), ++fails;
        // 40 blocks = 0.853 quarters; a programmed step fires at its ENDING
        // boundary: idx1 at ppq 0.50, idx2 at 0.75 (idx4's 1.25 is out of
        // range) - so two note-ons must have been produced.
        if (noteOns < 2)
            std::printf ("arp19: fake-host run produced %d note-ons (calls=%d)\n",
                         noteOns, fake.calls), ++fails;

        // Gate length: the note-off must land at about 75% of its 1/16 step.
        if (offQuartersStep0 > 0.0)
        {
            const double within = std::fmod (offQuartersStep0, spq) / spq;
            if (within < 0.55 || within > 0.95)
                std::printf ("arp19: default gate off at %.2f of the step (want ~0.75)\n",
                             within), ++fails;
        }
        else
            std::printf ("arp19: no note-off observed for the gate check\n"), ++fails;

        // Strum: hold a three-note chord; each boundary now fires the whole
        // chord staggered (top note first, bottom last), transposed by the
        // current step's semitone.
        setA (param::arpDir, 5.0f);                    // STRUM
        {
            juce::MidiBuffer chord;
            chord.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
            chord.addEvent (juce::MidiMessage::noteOn (1, 64, 0.9f), 0);
            chord.addEvent (juce::MidiMessage::noteOn (1, 67, 0.9f), 0);
            held = chord;
        }
        fake.ppq = 0.745;    // the 0.75 boundary (step idx 2, +4) lands block ~1

        int strumOns = 0;
        int firstOnAt = -1, lastOnAt = -1;
        for (int b = 0; b < 8; ++b)
        {
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer m;
            m.addEvents (held, 0, -1, 0);   // keep the chord held (see above)
            arp.processBlock (buf, m);
            for (const auto& ev : m)
                if (ev.getMessage().isNoteOn())
                {
                    ++strumOns;
                    if (firstOnAt < 0) firstOnAt = ev.samplePosition;
                    lastOnAt = ev.samplePosition;
                }
            fake.ppq += 512 / 48000.0 * 2.0;
        }
        // One boundary (a 1/16 spans ~5.9 blocks) fires all three held notes.
        if (strumOns < 3)
            std::printf ("arp19: strum produced only %d note-ons (want >= 3)\n",
                         strumOns), ++fails;
        // The stagger must be audible in the MIDI stream: strings start at
        // different sample offsets (18 ms apart), never all on one sample.
        if (strumOns >= 3 && firstOnAt == lastOnAt)
            std::printf ("arp19: strum notes were not staggered\n"), ++fails;

        // Audio path stays sane with the arp live.
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer m;
        arp.processBlock (buf, m);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                if (! std::isfinite (buf.getSample (ch, i)) || std::fabs (buf.getSample (ch, i)) > 4.0f)
                {
                    std::printf ("arp19: strum render produced non-finite/hot audio\n"), ++fails;
                    ch = 2; break;
                }

        arp.testPlayHead = nullptr;
        goa::License::deactivate();
        goa::License::setTestMachineId ("");
    }

    // ========================================================================
    phase ("20) trancegate shapes (square/smooth/saw/triangle)");
    // Every shape must stay bounded, differ from the others, and keep the
    // output finite; SMOOTH and TRIANGLE must not click (their max per-sample
    // jump stays far below SQUARE's hard 1 ms-slewed edge).
    {
        goa::License::setTestMachineId ("2D6EFDDAA1AC30F510C8");
        juce::String gErr;
        goa::License::activate (GOA_TEST_MASTER_KEY, gErr);
        GoaSynthAudioProcessor gProc;
        FakePlayHead fake;
        gProc.testPlayHead = &fake;
        gProc.prepareToPlay (48000.0, 512);

        auto setG = [&gProc] (const char* id, float v)
        { if (auto* p = gProc.apvts.getRawParameterValue (id)) p->store (v); };

        // Gate pattern: odd steps on, even off, depth 1 (silence), 1/16 steps.
        setG (param::gateSync, 1.0f);
        setG (param::gateDepth, 1.0f);
        setG (param::gateShape, 0.0f);
        for (int i = 0; i < 16; ++i)
            setG (param::gateStep (i).toRawUTF8(), (i % 2) == 0 ? 1.0f : 0.0f);

        // A held note; the gate shapes its volume while the transport runs.
        double prevRms[4]  = { -1.0, -1.0, -1.0, -1.0 };
        double maxJump[4]  = { 0.0, 0.0, 0.0, 0.0 };
        for (int shape = 0; shape < 4; ++shape)
        {
            setG (param::gateShape, (float) shape);
            fake.ppq = 0.0;
            gProc.synth.allNotesOff (1, false);

            juce::MidiBuffer m;
            m.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
            float prev = 0.0f;
            double peak = 0.0;
            double sumSq = 0.0;
            long long samples = 0;
            for (int b = 0; b < 16; ++b)
            {
                juce::AudioBuffer<float> buf (2, 512);
                if (b > 0) { m.clear(); m.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0); }
                gProc.processBlock (buf, m);
                for (int i = 0; i < 512; ++i)
                {
                    const float s = std::fabs (buf.getSample (0, i));
                    maxJump[(size_t) shape] = juce::jmax (maxJump[(size_t) shape],
                        (double) std::fabs (s - prev));
                    prev = s;
                    peak = juce::jmax (peak, (double) s);
                    sumSq += (double) s * s;
                    ++samples;
                }
                fake.ppq += 512 / 48000.0 * 2.0;
            }

            if (peak <= 0.0)
                std::printf ("gate20: shape %d produced silence\n", shape), ++fails;
            prevRms[(size_t) shape] = std::sqrt (sumSq / (double) juce::jmax (samples, 1LL));

            // Finite and bounded by the master limiter's -0.5 dB ceiling.
            if (peak > 0.95)
                std::printf ("gate20: shape %d render too hot (%.2f)\n", shape, peak), ++fails;
        }

        // Shapes must actually differ. They used to be told apart by peak,
        // but the master limiter (goa::MasterLimiter) pins every hot shape's
        // peak at its -0.5 dB ceiling — the shape now lives in the energy, so
        // compare RMS: square's full-level plateaus carry the most of it, the
        // ramping shapes progressively less.
        if (std::abs (prevRms[2] - prevRms[0]) < 1.0e-3
            || std::abs (prevRms[3] - prevRms[1]) < 1.0e-3)
            std::printf ("gate20: shapes produced identical RMS (square=%.4f saw=%.4f smooth=%.4f tri=%.4f)\n",
                         prevRms[0], prevRms[2], prevRms[1], prevRms[3]), ++fails;

        // Click check: SMOOTH and TRIANGLE are band-limited by construction, so
        // their worst per-sample jump |ds| must stay below HALF the raw sample
        // amplitude 0.9 (a shape-faithful edge slew never gets near a full-scale
        // discontinuity; a hard ungated edge would jump ~0.9 in one sample).
        // SQUARE is exempt from this check: its full-level bursts drive the
        // downstream limiter, which reshapes the waveform and can exceed this
        // bound without being a click.
        if (maxJump[1] > 0.45)
            std::printf ("gate20: SMOOTH clicks (jump %.4f)\n", maxJump[1]), ++fails;
        if (maxJump[3] > 0.45)
            std::printf ("gate20: TRIANGLE clicks (jump %.4f)\n", maxJump[3]), ++fails;
        if (maxJump[0] < 1.0e-3 || maxJump[2] < 1.0e-3)
            std::printf ("gate20: SQUARE/SAW jumps degenerate (%.4f/%.4f)\n",
                         maxJump[0], maxJump[2]), ++fails;
        std::printf ("gate20 jumps: square=%.4f smooth=%.4f saw=%.4f tri=%.4f\n",
                     maxJump[0], maxJump[1], maxJump[2], maxJump[3]);

        gProc.testPlayHead = nullptr;
        goa::License::deactivate();
        goa::License::setTestMachineId ("");
    }

    // Factory presets block follows (outside phase 18's scope)

    // Factory presets: every LEAD and RISER must enable master quality (2x
    // oversampling), and no other category should force it on.
    {
        int leadBad = 0, riserBad = 0, otherOn = 0, leads = 0, risers = 0;
        int presetIdx = 0;
        for (const auto& pr : getPresets())
        {
            const auto hq = pr.values.find ("masterHQ");
            const bool on = hq != pr.values.end() && hq->second > 0.5f;
            const juce::String cat (pr.cat);
            if (cat == "lead")      { ++leads; if (! on) ++leadBad; }
            else if (cat == "riser") { ++risers; if (! on) ++riserBad; }
            else if (on)            ++otherOn;
        }
        if (leads != 20 || risers != 19)
            std::printf ("presets: expected 20 LEAD and 19 RISER, got %d/%d\n",
                         leads, risers), ++fails;
        if (leadBad != 0)
            std::printf ("presets: %d LEAD patches missing masterHQ\n", leadBad), ++fails;
        if (riserBad != 0)
            std::printf ("presets: %d RISER patches missing masterHQ\n", riserBad), ++fails;
        if (otherOn != 0)
            std::printf ("presets: %d non-lead/riser patches force masterHQ on\n",
                         otherOn), ++fails;
    }

    // Every key a factory preset writes must be a real parameter id. applyPreset()
    // skips unknown ids silently (it only sets a parameter when
    // apvts.getParameter(id) is non-null), so a typo in a hand-written patch
    // would simply do nothing - indistinguishable from a patch that just sounds
    // odd, and invisible in every other test. With ~175 patches that is far too
    // easy to get wrong by hand, so pin it down.
    {
        GoaSynthAudioProcessor probe;
        std::set<juce::String> unknown;
        int values = 0;

        for (const auto& pr : getPresets())
            for (const auto& [id, v] : pr.values)
            {
                juce::ignoreUnused (v);
                ++values;
                if (probe.apvts.getParameter (id) == nullptr)
                    unknown.insert (id);
            }

        if (! unknown.empty())
        {
            std::printf ("presets: %d parameter id(s) do not exist:",
                         (int) unknown.size());
            for (const auto& id : unknown)
                std::printf (" %s", (const char*) id.toRawUTF8());
            std::printf ("\n");
            ++fails;
        }
        else
        {
            std::printf ("presets: all %d values map to real parameters\n", values);
        }
    }

    // Every factory preset must carry browser tags. The TAG filter is built from
    // getFactoryTags(), so a preset with no entry simply shows no tags - a silent
    // gap in the browser rather than an error anywhere.
    {
        const auto& tags = getFactoryTags();
        std::vector<juce::String> untagged;

        for (const auto& pr : getPresets())
            if (tags.find (pr.name) == tags.end())
                untagged.push_back (pr.name);

        if (! untagged.empty())
        {
            std::printf ("presets: %d factory preset(s) have no browser tags:",
                         (int) untagged.size());
            for (const auto& n : untagged)
                std::printf (" %s", (const char*) n.toRawUTF8());
            std::printf ("\n");
            ++fails;
        }
        else
        {
            std::printf ("presets: all %d factory presets are tagged\n",
                         (int) getPresets().size());
        }
    }

    // ---- new parameters, mod destinations, host program list, MIDI output ----
    // Guards the features added in this round. Each check is here because the
    // failure mode is otherwise invisible: parameters are looked up by string
    // (a typo silently does nothing), the program list is stubbed by default,
    // and the MIDI output buffer is easy to fill with an echo by accident.
    {
        GoaSynthAudioProcessor probe;
        int bad = 0;

        // (1) The new parameters must exist with the defaults the DSP assumes.
        struct PDef { const char* id; float def; };
        const PDef defs[] = {
            { param::pulseWidth, 0.5f }, { param::ringMod, 0.0f },
            { param::eqLow, 0.0f }, { param::eqMid, 0.0f },
            { param::eqMidFreq, 1200.0f }, { param::eqHigh, 0.0f },
        };
        for (const auto& d : defs)
        {
            auto* p = probe.apvts.getParameter (d.id);
            if (p == nullptr)
            {
                std::printf ("new params: missing parameter id '%s'\n", d.id);
                ++bad;
                continue;
            }
            const float got = p->getNormalisableRange().convertFrom0to1 (p->getDefaultValue());
            if (std::abs (got - d.def) > 1.0e-3f)
            {
                std::printf ("new params: '%s' defaults to %.4f, expected %.4f\n",
                             d.id, got, d.def);
                ++bad;
            }
        }
        if (auto* p = probe.apvts.getParameter (param::oscSync))
        {
            if (p->getDefaultValue() > 0.5f)
            {
                std::printf ("new params: oscSync defaults ON\n");
                ++bad;
            }
        }
        else
        {
            std::printf ("new params: missing parameter id '%s'\n", param::oscSync);
            ++bad;
        }

        // (2) Every mod destination must name a real parameter, or a matrix
        // slot aimed at it would move nothing at all.
        const int nDests = (int) param::modDestList().size();
        for (int i = 1; i < nDests; ++i)
        {
            const char* pid = param::modDestList()[(size_t) i].param;
            if (probe.apvts.getParameter (pid) == nullptr)
            {
                std::printf ("mod dests: destination %d ('%s') is not a parameter\n", i, pid);
                ++bad;
            }
        }

        // (3) The host program list must expose the whole factory bank.
        const int nProg    = probe.getNumPrograms();
        const int nFactory = (int) getPresets().size();
        if (nProg != nFactory + 1)
        {
            std::printf ("programs: getNumPrograms() = %d, expected %d (Init + %d factory)\n",
                         nProg, nFactory + 1, nFactory);
            ++bad;
        }
        if (probe.getProgramName (0) != "Init Patch")
        {
            std::printf ("programs: program 0 is '%s', expected 'Init Patch'\n",
                         (const char*) probe.getProgramName (0).toRawUTF8());
            ++bad;
        }
        for (int i = 0; i < nFactory; ++i)
            if (probe.getProgramName (i + 1) != juce::String (getPresets()[(size_t) i].name))
            {
                std::printf ("programs: name %d is '%s', expected '%s'\n", i + 1,
                             (const char*) probe.getProgramName (i + 1).toRawUTF8(),
                             getPresets()[(size_t) i].name);
                ++bad;
                break;
            }

        // Loading a program must actually move the parameters onto that patch
        // (the message-thread load is driven directly here - no message loop).
        probe.setCurrentProgram (1);
        probe.applyPendingProgram();
        {
            const auto& pr = getPresets()[0];
            int mismatch = 0;
            for (const auto& [id, v] : pr.values)
                if (auto* p = probe.apvts.getParameter (id))
                {
                    const float got = p->getNormalisableRange().convertFrom0to1 (p->getValue());
                    if (std::abs (got - v) > 1.0e-2f)
                        ++mismatch;
                }
            if (mismatch > 0)
            {
                std::printf ("programs: program 1 ('%s') left %d value(s) wrong\n",
                             pr.name, mismatch);
                ++bad;
            }
        }
        if (probe.getCurrentProgram() != 1)
        {
            std::printf ("programs: getCurrentProgram() = %d after loading program 1\n",
                         probe.getCurrentProgram());
            ++bad;
        }

        // (4) producesMidi() must be true, and processBlock must NOT echo the
        // host's own notes back out - that is the whole point of collecting the
        // arp's events into a separate buffer.
        if (! probe.producesMidi())
        {
            std::printf ("midi out: producesMidi() is false\n");
            ++bad;
        }
        {
            probe.prepareToPlay (48000.0, 512);
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
            midi.addEvent (juce::MidiMessage::noteOff (1, 60), 256);
            probe.processBlock (buf, midi);
            // The transport is not playing, so the arp generates nothing: the
            // output must be empty rather than a copy of the input.
            if (! midi.isEmpty())
            {
                std::printf ("midi out: %d event(s) echoed back from the input\n",
                             midi.getNumEvents());
                ++bad;
            }
        }

        if (bad == 0)
            std::printf ("new features: parameters + %d mod destinations + %d host programs "
                         "+ MIDI out (no echo) all OK\n", nDests - 1, nProg);
        else
            ++fails;
    }

    std::printf (fails == 0 ? "ROUND TRIP OK\n" : "FAILURES: %d\n", fails);
    std::fflush (stdout);
    std::fflush (stdout);
    return fails == 0 ? 0 : 1;
}
