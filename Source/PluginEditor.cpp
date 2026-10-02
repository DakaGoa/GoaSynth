#include "PluginEditor.h"
#include "AiCloudGen.h"
#include "AiPatchGen.h"
#include "LearnedPatches.h"
#include "License.h"
#include "Presets.h"
#include "UserPresets.h"

#include <juce_audio_formats/juce_audio_formats.h>   // WAV -> wavetable import

#include <cmath>
#include <cstdlib>

namespace goaui
{

// Defined here (declared in PluginProcessor.h): session-restore target theme.
Theme themeForNextEditor = themeUv;

//==============================================================================
// Theme engine. The palette globals are rewritten by setTheme(); components
// with captured colours expose retint() hooks and the editor walks the tree.
//==============================================================================
Palette pal;
Theme activeTheme = themeUv;

juce::Colour bgDark, bgPanel, bgPanelLo, bgHeader, bgInset, border,
             accent, accentA, accentB, textDim, textBright,
             knobTop, knobBottom, knobRim, knobTrack,
             keyWhite, keyBlack, keyDim, bgTop, bgBottom;

static void applyPalette (const Palette& p)
{
    pal = p;
    bgDark     = p.bgDark;      bgPanel    = p.bgPanel;
    bgPanelLo  = p.bgPanelLo;   bgHeader   = p.bgHeader;
    bgInset    = p.bgInset;     border     = p.border;
    accent     = p.accent;      accentA    = p.accentA;
    accentB    = p.accentB;     textDim    = p.textDim;
    textBright = p.textBright;
    knobTop    = p.knobTop;     knobBottom = p.knobBottom;
    knobRim    = p.knobRim;     knobTrack  = p.knobTrack;
    keyWhite   = p.keyWhite;    keyBlack   = p.keyBlack;
    keyDim     = p.keyDim;      bgTop      = p.bgTop;
    bgBottom   = p.bgBottom;
}

// UV Goa: violet / teal / magenta on purple-tinted darks (the house look).
static const Palette& uvPalette()
{
    static const Palette p {
        juce::Colour (0xff0e0f18), juce::Colour (0xff1f2230), juce::Colour (0xff181a28), juce::Colour (0xff151626),
        juce::Colour (0xff111220), juce::Colour (0xff343a58),
        juce::Colour (0xff9b6cff), juce::Colour (0xff36d6c3), juce::Colour (0xffff5f9e), juce::Colour (0xff94a0c4), juce::Colour (0xffeceffb),
        juce::Colour (0xff2b2d45), juce::Colour (0xff171826), juce::Colour (0xff0a0a14), juce::Colour (0xff3b3f66),
        juce::Colour (0xffdfe0ef), juce::Colour (0xff171826), juce::Colour (0xff8a91b8),
        juce::Colour (0xff141024), juce::Colour (0xff0b0a14) };
    return p;
}

// Studio steel: blue-greys, restrained cyan-steel primary and amber secondary.
static const Palette& steelPalette()
{
    static const Palette p {
        juce::Colour (0xff0f1113), juce::Colour (0xff232730), juce::Colour (0xff1a1e26), juce::Colour (0xff171b22),
        juce::Colour (0xff12151b), juce::Colour (0xff3a4252),
        juce::Colour (0xff6fb7d6), juce::Colour (0xff8ea3b8), juce::Colour (0xffd6a44c), juce::Colour (0xff8b96a8), juce::Colour (0xffeef1f4),
        juce::Colour (0xff333a46), juce::Colour (0xff1c2029), juce::Colour (0xff0b0d11), juce::Colour (0xff454f61),
        juce::Colour (0xffe4e8ee), juce::Colour (0xff1a1e25), juce::Colour (0xff7c8698),
        juce::Colour (0xff12171e), juce::Colour (0xff0a0c10) };
    return p;
}

// Warm analog: cream face over warm browns, orange primary, olive secondary,
// rust tertiary — vintage outboard rack vibes.
static const Palette& analogPalette()
{
    static const Palette p {
        juce::Colour (0xff17130f), juce::Colour (0xff2e2820), juce::Colour (0xff251f18), juce::Colour (0xff211b14),
        juce::Colour (0xff1b1611), juce::Colour (0xff4d4232),
        juce::Colour (0xffe8933f), juce::Colour (0xffa8b06a), juce::Colour (0xffcf6a4a), juce::Colour (0xffb0a58e), juce::Colour (0xfff5efe2),
        juce::Colour (0xff4a3d2c), juce::Colour (0xff2a2218), juce::Colour (0xff120d08), juce::Colour (0xff6b5a40),
        juce::Colour (0xfff2ead6), juce::Colour (0xff241c12), juce::Colour (0xffa89877),
        juce::Colour (0xff1e160d), juce::Colour (0xff0f0a06) };
    return p;
}

// Cyberpunk / Dark Club: cyan, hot magenta, neon yellow on deep black.
static const Palette& neonPalette()
{
    static const Palette p {
        juce::Colour (0xff0a0a0f), juce::Colour (0xff12121a), juce::Colour (0xff0e0e16), juce::Colour (0xff0c0c14),
        juce::Colour (0xff08080c), juce::Colour (0xff2a2a3a),
        juce::Colour (0xff00f0ff), juce::Colour (0xffff0055), juce::Colour (0xffffee00), juce::Colour (0xff8899aa), juce::Colour (0xffe0f0ff),
        juce::Colour (0xff1a1a28), juce::Colour (0xff0e0e18), juce::Colour (0xff050508), juce::Colour (0xff303040),
        juce::Colour (0xffc0d0e0), juce::Colour (0xff0a0a10), juce::Colour (0xff667788),
        juce::Colour (0xff080810), juce::Colour (0xff040408) };
    return p;
}

// Light mode / Paper: warm off-white with cobalt, emerald and amber accents.
static const Palette& paperPalette()
{
    static const Palette p {
        juce::Colour (0xfff5f3ef), juce::Colour (0xffffffff), juce::Colour (0xfffaf9f6), juce::Colour (0xfff0eeea),
        juce::Colour (0xffeae6e0), juce::Colour (0xffd1d5db),
        juce::Colour (0xff2563eb), juce::Colour (0xff059669), juce::Colour (0xffd97706), juce::Colour (0xff6b7280), juce::Colour (0xff111827),
        juce::Colour (0xffe5e7eb), juce::Colour (0xfff3f4f6), juce::Colour (0xffd1d5db), juce::Colour (0xff9ca3af),
        juce::Colour (0xff111827), juce::Colour (0xffffffff), juce::Colour (0xff6b7280),
        juce::Colour (0xfff0f0ec), juce::Colour (0xffe8e8e4) };
    return p;
}

// OLED pure black: single red accent for maximum contrast and battery life.
static const Palette& oledPalette()
{
    static const Palette p {
        juce::Colour (0xff000000), juce::Colour (0xff050505), juce::Colour (0xff030303), juce::Colour (0xff040404),
        juce::Colour (0xff020202), juce::Colour (0xff1a1a1a),
        juce::Colour (0xffff3333), juce::Colour (0xffff5555), juce::Colour (0xffff7777), juce::Colour (0xff888888), juce::Colour (0xffffffff),
        juce::Colour (0xff111111), juce::Colour (0xff050505), juce::Colour (0xff000000), juce::Colour (0xff222222),
        juce::Colour (0xffeeeeee), juce::Colour (0xff000000), juce::Colour (0xff666666),
        juce::Colour (0xff000000), juce::Colour (0xff000000) };
    return p;
}

Theme themeFromIndex (int i) noexcept
{
    return (Theme) juce::jlimit (0, (int) themeCount - 1, i);
}

// Palette lookup by theme index (theme dropdown swatches): lets UI code read a
// theme's colours WITHOUT switching the live palette.
static const Palette& paletteForIndex (int i)
{
    switch (themeFromIndex (i))
    {
        case themeSteel:  return steelPalette();
        case themeAnalog: return analogPalette();
        case themeNeon:   return neonPalette();
        case themePaper:  return paperPalette();
        case themeOled:   return oledPalette();
        default:          return uvPalette();
    }
}

static void walkRetint (juce::Component* c);

void setTheme (Theme t, juce::Component* root)
{
    activeTheme = t;
    switch (t)
    {
        case themeSteel:  applyPalette (steelPalette());  break;
        case themeAnalog: applyPalette (analogPalette()); break;
        case themeNeon:   applyPalette (neonPalette());   break;
        case themePaper:  applyPalette (paperPalette());  break;
        case themeOled:   applyPalette (oledPalette());   break;
        default:          applyPalette (uvPalette());     break;
    }
    if (root != nullptr)
        walkRetint (root);
}

static void walkRetint (juce::Component* c)
{
    if (auto* p = dynamic_cast<Panel*> (c))            p->retint();
    else if (auto* k = dynamic_cast<Ctl*> (c))         k->retint();
    else if (auto* tg = dynamic_cast<ToggleCtl*> (c))  tg->retint();
    else if (auto* w = dynamic_cast<WaveDisplay*> (c)) w->retint();
    else if (auto* f = dynamic_cast<FilterGraph*> (c)) f->retint();
    else if (auto* e = dynamic_cast<EnvGraph*> (c))    e->retint();
    else if (auto* l = dynamic_cast<LfoGraph*> (c))    l->retint();
    else if (auto* s = dynamic_cast<StepStrip*> (c))   s->retint();
    else if (auto* m = dynamic_cast<ModOverlay*> (c))  m->retint();
    else if (auto* a = dynamic_cast<AiOverlay*> (c))   a->retint();
    else if (auto* sv = dynamic_cast<SavePresetOverlay*> (c)) sv->retint();
    else if (auto* br = dynamic_cast<PresetBrowserOverlay*> (c)) br->retint();
    else    if      (auto* lc = dynamic_cast<LicenseOverlay*> (c))       lc->retint();
    for (auto* child : c->getChildren())
        walkRetint (child);
}

//==============================================================================
// Mod-dots: which sources currently target a parameter. Scans the 8 matrix
// slots in the APVTS (cheap, UI-thread only); a slot counts when its source
// is chosen and its amount is non-zero. Sources are colour-coded at the
// call site: LFO 1 teal, LFO 2 magenta, envelopes accent, velocity/modwheel
// dim. Return count capped at 3 (three dots fit on a knob).
int modDotSources (juce::AudioProcessorValueTreeState& apvts,
                   const juce::String& paramId, int outSrc[3], float outAmt[3],
                   int outSlot[3])
{
    int n = 0;
    const bool bankB = apvts.getRawParameterValue (param::modBank) != nullptr
                    && apvts.getRawParameterValue (param::modBank)->load() > 0.5f;
    for (int i = 0; i < param::modSlots && n < 3; ++i)
    {
        auto* dst = apvts.getParameter (
            bankB ? param::modBDst (i) : param::modDst (i));
        auto* amt = apvts.getParameter (
            bankB ? param::modBAmt (i) : param::modAmt (i));
        if (dst == nullptr || amt == nullptr)
            continue;
        const int dstIdx = (int) dst->getNormalisableRange().convertFrom0to1 (dst->getValue());
        if (dstIdx <= 0)
            continue;
        const auto& row = param::modDestList()[(size_t) dstIdx];
        const float a = amt->getNormalisableRange().convertFrom0to1 (amt->getValue());
        if (paramId != row.param || std::abs (a) < 0.003f)
            continue;
        auto* src = apvts.getParameter (
            bankB ? param::modBSrc (i) : param::modSrc (i));
        if (src == nullptr)
            continue;
        outSrc[n] = (int) src->getNormalisableRange().convertFrom0to1 (src->getValue());
        outAmt[n] = a;
        if (outSlot != nullptr)
            outSlot[n] = i;
        ++n;
    }
    return n;
}

// Hover text for a control's mod dots: one line per active slot. Built fresh
// on every tooltip query, so it tracks slot edits and preset loads with no
// caching to go stale.
bool modDotTip (juce::AudioProcessorValueTreeState& apvts,
                const juce::String& paramId, juce::String& tip)
{
    int src[3], slot[3];
    float amt[3];
    const int n = modDotSources (apvts, paramId, src, amt, slot);
    if (n == 0)
        return false;
    const bool bankB = apvts.getRawParameterValue (param::modBank) != nullptr
                    && apvts.getRawParameterValue (param::modBank)->load() > 0.5f;

    // The control's own label, best-effort: the destination display name
    // (from the dest list) is the authoritative long name.
    juce::String dstName = paramId;
    const auto& list = param::modDestList();
    for (int i = 1; i < (int) list.size(); ++i)
        if (paramId == list[(size_t) i].param) { dstName = list[(size_t) i].label; break; }

    juce::StringArray lines;
    for (int k = 0; k < n; ++k)
        lines.add ((bankB ? "MOD B" : "MOD ") + juce::String (slot[k] + 1)
                   + ": " + param::modSourceName()[(size_t) src[k]]
                   + " -> " + dstName
                   + " (" + (amt[k] >= 0.0f ? "+" : "")
                   + juce::String (juce::roundToInt (amt[k] * 100.0f)) + ")");
    tip = lines.joinIntoString ("\n");
    return true;
}

// Repaint every dot-capable control under `root`: called when a matrix slot
// changes (pick assign / cycle / double-click reset / preset load)
// so the little source dots stay honest.
static void repaintCtlPaints (juce::Component* root)
{
    if (root == nullptr)
        return;
    if (dynamic_cast<Ctl*> (root) != nullptr || dynamic_cast<ToggleCtl*> (root) != nullptr)
        root->repaint();
    for (auto* c : root->getChildren())
        repaintCtlPaints (c);
}

// Pick-assignment flash: a fading rounded outline around the whole control
// that just received a routing. Called from paintOverChildren (knobs) or
// paint (graphs); the editor's 30 fps timer keeps the fade animating.
static void paintFlash (juce::Graphics& g, juce::Component& c, float fade)
{
    if (fade <= 0.0f)
        return;
    auto b = c.getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (accentB.withAlpha (0.55f * fade));
    g.drawRoundedRectangle (b, 5.0f, 2.0f + 2.0f * fade);
    g.setColour (accentB.withAlpha (0.12f * fade));
    g.fillRoundedRectangle (b, 5.0f);
}

// One animated mod dot (shared by Ctl and ToggleCtl): pulses with the live
// source value x amount — an LFO-driven dot breathes at the LFO rate, a
// velocity dot pops when you hit the key. Base size 3 px, plus a soft ring
// once the depth passes 60%.
static void paintModDot (juce::Graphics& g, juce::Point<float> centre,
                         int source, float amount, const goa::GoaSynth* engine)
{
    float live = 0.0f;
    if (engine != nullptr)
    {
        switch (source)
        {
            case 1: live = engine->uiLfo1.load (std::memory_order_relaxed); break;
            case 2: live = engine->uiLfo2.load (std::memory_order_relaxed); break;
            case 3: live = engine->uiEnvF.load (std::memory_order_relaxed); break;
            case 4: live = engine->uiEnvA.load (std::memory_order_relaxed); break;
            case 5: live = engine->uiVelocity.load (std::memory_order_relaxed); break;
            case 6: live = engine->modWheel.load (std::memory_order_relaxed); break;
            default: break;
        }
    }
    // Bipolar sources: pulse on magnitude; unipolar (env/vel) pulse as-is.
    const float mag = source == 1 || source == 2 ? std::abs (live) : juce::jlimit (0.0f, 1.0f, live);
    const float depth = juce::jlimit (0.0f, 1.0f, std::abs (amount));
    const float pulse = 0.35f + 0.65f * mag;            // 0.35..1 brightness
    const float radius = 2.5f + 1.5f * depth * pulse;   // 2.5..~5 px
    juce::Colour c;
    switch (source)
    {
        case 1:    c = accentA; break;
        case 2:    c = accentB; break;
        case 3:    case 4: c = accent; break;
        case 7:    c = accentB.brighter (0.2f); break;
        case 8:    c = accentA.brighter (0.2f); break;
        case 9:    case 10: c = textBright; break;
        default:   c = textDim; break;
    }
    g.setColour (c.withAlpha (0.28f + 0.6f * pulse));
    g.fillEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
    g.setColour (c.withAlpha (0.85f));
    g.fillEllipse (centre.x - radius * 0.5f, centre.y - radius * 0.5f, radius, radius);
    if (depth > 0.6f)   // strong routing gets a ring around the dot
    {
        g.setColour (c.withAlpha (0.35f + 0.3f * pulse));
        g.drawEllipse (centre.x - radius - 2.0f, centre.y - radius - 2.0f,
                       (radius + 2.0f) * 2.0f, (radius + 2.0f) * 2.0f, 1.0f);
    }
}

//==============================================================================
Ctl::Ctl (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId,
          const juce::String& text, bool comboBox)
    : useCombo (comboBox), paramId (paramId), modSource (&apvts)
{
    setRepaintsOnMouseActivity (true);
    label.setText (text, juce::dontSendNotification);
    label.setFont (juce::Font (juce::FontOptions (9.0f)));
    label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (label);

    // Base tooltip: the parameter's own human name ("OSC A Wave", "Filter
    // Cutoff"...). The knob's live value is appended on a second line and
    // refreshes while the pointer hovers. MOD dots override the first line
    // while a routing is active.
    if (auto* p = apvts.getParameter (paramId))
        baseTip = p->getName (64);
    setTip (baseTip);

    if (useCombo)
    {
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (paramId)))
            combo.addItemList (choice->getAllValueStrings(), 1);
        addAndMakeVisible (combo);
        cAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, paramId, combo);
    }
    else
    {
        slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        addAndMakeVisible (slider);
        sAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, paramId, slider);
    }
    retint();
}

// Live-value line for a parameter: converts the normalised position into the
// unit the engine actually uses. Rebuilt on every paint (30 fps while the
// backdrop animates), so a hover always shows the current value.
// dropUnit squeezes the text for the tiny on-knob readout ("1.5k Hz" ->
// "1.5k", "0.60 Hz" -> "0.60"); the tooltip keeps the full form.
// Envelope times span 1 ms..5 s: compact form for the on-knob readout
// ("2.5s" above a second, milliseconds below), s/ms suffix kept in tooltips.
static juce::String envCompact (float v)
{
    return v >= 1.0f ? juce::String (v, 1) + "s" : juce::String (juce::roundToInt (v * 1000.0f)) + "m";
}

juce::String liveValueText (juce::AudioProcessorValueTreeState& apvts,
                            const juce::String& id, bool dropUnit)
{
    auto* p = apvts.getParameter (id);
    if (p == nullptr)
        return {};
    const float v = p->getNormalisableRange().convertFrom0to1 (p->getValue());
    const auto f = [] (float x, int dec) { return juce::String (x, dec); };

    const auto unit = [&] (const juce::String& s) { return dropUnit ? juce::String() : " " + s; };

    if (id == param::cutoff || id == param::cutoff2)
    {
        // Compact readout form: 1500 -> 1.5k. Tooltips keep the full number.
        if (dropUnit)
            return v >= 1000.0f ? f (v / 1000.0f, 1) + "k" : f (v, 0);
        return f (v, 0) + unit ("Hz");
    }
    if (id == param::delayTime || id == param::glide)  return f (v, 0) + unit ("ms");
    if (id == param::osc1Fine || id == param::osc2Fine || id == param::uniDetune)
        return dropUnit ? juce::String (juce::roundToInt (v))   // "12", "-50"
                        : f (v, 1) + unit ("ct");
    if (id == param::masterGain || id == param::ottOut) return f (v, 1) + unit ("dB");
    // Master EQ: three gains in dB and a mid frequency in Hz. Without these they
    // fell through to the 0..1 "%" fallback and read "-1500 %". The gain is
    // snapped to exactly zero within a hair of it, or a neutral band would
    // render as "-0.0 dB".
    if (id == param::eqLow || id == param::eqMid || id == param::eqHigh)
    {
        const float g = std::abs (v) < 0.05f ? 0.0f : v;
        return f (g, 1) + unit ("dB");
    }
    if (id == param::eqMidFreq)
        return dropUnit ? (v >= 1000.0f ? f (v / 1000.0f, 1) + "k" : f (v, 0))
                        : f (v, 0) + unit ("Hz");
    if (id == param::lfo1Rate || id == param::lfo2Rate
        || id == param::chorusRate || id == param::phRate)
        return f (v, 2) + unit ("Hz");
    if (id == param::ampA || id == param::ampD || id == param::ampR
        || id == param::filtA || id == param::filtD || id == param::filtR)
        return dropUnit ? envCompact (v) : f (v, 2) + " s";
    if (id == param::envAmt)   // ENV AMT knob spans 0..5 octaves, not seconds
        return f (v, 1) + unit ("oct");
    if (id == param::ampS || id == param::filtS)
        return juce::String (juce::roundToInt (v * 100.0f)) + unit ("%");
    if (id == param::osc1Oct || id == param::osc2Oct || id == param::subOct)
        return juce::String (juce::roundToInt (v)) + unit ("oct");
    if (id == param::tuningFine)
        return dropUnit ? juce::String (juce::roundToInt (v))
                        : (v >= 0.0f ? "+" : "") + f (v, 1) + unit ("ct");
    if (id == param::uniVoices || id == param::polyMax || id == param::bendRange)
        return juce::String (juce::roundToInt (v));
    if (id == param::duckAmt || id == param::revShimmer)
        return juce::String (juce::roundToInt (v * 100.0f)) + unit ("%");
    // Everything else is a 0..1 amount (mixes, depths, sizes, phases):
    // percentage is the honest unit for both the tooltip and the readout.
    return juce::String (juce::roundToInt (v * 100.0f)) + unit ("%");
}

void Ctl::retint()
{
    label.setColour (juce::Label::textColourId, textDim);
    combo.setColour (juce::ComboBox::backgroundColourId, bgPanelLo);
    combo.setColour (juce::ComboBox::textColourId, textBright);
    combo.setColour (juce::ComboBox::arrowColourId, accent);
    combo.setColour (juce::ComboBox::outlineColourId, border);
    slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    slider.setColour (juce::Slider::rotarySliderOutlineColourId, bgInset);
    slider.setColour (juce::Slider::trackColourId, accent);
    slider.setColour (juce::Slider::thumbColourId, textBright);
    combo.repaint();
    slider.repaint();
    label.repaint();
}

void Ctl::setTip (const juce::String& t)
{
    slider.setTooltip (t);
    combo.setTooltip (t);
    label.setTooltip (t);
}

void ToggleCtl::setTip (const juce::String& t)
{
    btn.setTooltip (t);
    label.setTooltip (t);
}

void Ctl::resized()
{
    auto b = getLocalBounds();
    if (useCombo)
    {
        if (b.getHeight() <= 26)
        {
            combo.setBounds (b);
            label.setBounds ({});
            label.setVisible (false);   // hidden, not a visible 0x0 component
        }
        else
        {
            label.setVisible (true);
            label.setBounds (b.removeFromTop (12));
            combo.setBounds (b.reduced (2, 2));
        }
    }
    else
    {
        // Name on the first line, live readout on the second (painted by
        // paint()), rotary below. These two used to share a single 12px band,
        // so in a narrow cell the value was painted straight over the name -
        // the FX row read "RATEGs" / "MID00" / "LO100". Stacking them costs no
        // knob size: the rotary is width-limited in every panel (min(w,h) is
        // the cell width), so the drawn circle is unchanged.
        label.setBounds (b.removeFromTop (labelLine));
        b.removeFromTop (readoutLine);
        slider.setBounds (b);
    }
}


// Little source dots in the knob's top-right corner: one per active matrix
// slot routing this parameter, animated by the live source value x amount
// (see paintModDot). The tooltip names each routing (source -> destination
// and amount); children are transparent so the dots stay visible.
void Ctl::paint (juce::Graphics& g)
{
    int src[3];
    float amt[3];
    const int n = modDotSources (*modSource, paramId, src, amt);
    for (int k = 0; k < n; ++k)
        paintModDot (g, { (float) getWidth() - 8.0f - k * 8.0f, 6.0f },
                     src[k], amt[k], liveEngine);

    // Knob hover glow: a soft radial gradient under the rotary.
    if (isMouseOver() && ! useCombo)
    {
        auto sb = slider.getBounds().toFloat();
        auto centre = sb.getCentre();
        float rad = juce::jmin (sb.getWidth(), sb.getHeight()) * 0.55f;
        juce::ColourGradient glow (accent.withAlpha (0.10f), centre.x, centre.y,
                                   accent.withAlpha (0.0f), centre.x + rad, centre.y + rad, true);
        g.setGradientFill (glow);
        g.fillEllipse (centre.x - rad, centre.y - rad, rad * 2.0f, rad * 2.0f);
    }

    // Mod-depth arcs: for each routing into this knob, a short arc around the
    // knob's perimeter showing where the source pushes the value RIGHT NOW —
    // the corner dot says WHAT modulates, the arc says HOW FAR it swings.
    // Same 9-to-3-over-the-top sweep as the value arc, radius just outside it.
    if (! useCombo && getWidth() >= 34)
    {
        auto* par = modSource->getParameter (paramId);
        if (par != nullptr)
        {
        // The arcs mirror the LAF's rotary geometry, which is drawn inside
        // the slider's bounds — so centre + radius come from there.
        const auto sb = slider.getBounds().toFloat();
        const float knobRad = juce::jmin (sb.getWidth(), sb.getHeight()) * 0.42f;
        const auto centre = sb.getCentre();
        const float rad = knobRad + 7.0f;
        const float startA = juce::MathConstants<float>::pi;
        const float endA = juce::MathConstants<float>::twoPi;
        const float base = par->getNormalisableRange().convertFrom0to1 (par->getValue());
        const float span = endA - startA;
        const auto angleOf = [&] (float x01) -> float
        {
            const float nv = par->getNormalisableRange().convertTo0to1 (x01);
            if (! (nv == nv))          // NaN guard (log ranges below range start)
                return startA;
            return startA + span * juce::jlimit (0.0f, 1.0f, nv);
        };
        for (int k = 0; k < n; ++k)
        {
            // Live source value (last-writer-wins engine snapshot, same as the
            // dots); modwheel and macros are always-live.
            float live = 0.0f;
            if (liveEngine != nullptr)
                switch (src[k])
                {
                    case 1: live = liveEngine->uiLfo1.load (std::memory_order_relaxed); break;
                    case 2: live = liveEngine->uiLfo2.load (std::memory_order_relaxed); break;
                    case 3: live = liveEngine->uiEnvF.load (std::memory_order_relaxed); break;
                    case 4: live = liveEngine->uiEnvA.load (std::memory_order_relaxed); break;
                    case 5: live = liveEngine->uiVelocity.load (std::memory_order_relaxed); break;
                    case 6: live = liveEngine->modWheel.load (std::memory_order_relaxed); break;
                    case 7: live = liveEngine->shValue.load (std::memory_order_relaxed); break;
                    case 8: live = liveEngine->uiAt.load (std::memory_order_relaxed); break;
                    case 9: live = liveEngine->macroA.load (std::memory_order_relaxed); break;
                    case 10: live = liveEngine->macroB.load (std::memory_order_relaxed); break;
                    default: break;
                }
            const float range = [&] {
                const auto& list = param::modDestList();
                for (int i = 1; i < (int) list.size(); ++i)
                    if (paramId == list[(size_t) i].param)
                        return list[(size_t) i].range;
                return 1.0f;
            }();
            const float depth = std::abs (amt[k]) * range;
            if (depth < 1.0e-4f)
                continue;
            const float a0 = angleOf (base - depth);
            const float a1 = angleOf (base + depth);
            juce::Path arc;
            arc.addCentredArc (centre.x, centre.y, rad, rad, 0.0f,
                               juce::jmin (a0, a1), juce::jmax (a0, a1), true);
            juce::Colour c = src[k] == 1 ? accentA : src[k] == 2 ? accentB : accent;
            g.setColour (c.withAlpha (0.75f));
            g.strokePath (arc, juce::PathStrokeType (2.0f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        }
    }

    // Live readout on its OWN line, directly under the name (see resized()):
    // "the live value under the name" as documented, and the two can no longer
    // collide however narrow the cell gets. Trimmed on the right so it stays
    // clear of the mod dots in the top-right corner.
    if (! useCombo)
    {
        const juce::String txt = liveValueText (*modSource, paramId, true);
        auto band = getLocalBounds().removeFromTop (labelLine + readoutLine)
                        .removeFromBottom (readoutLine).toFloat()
                        .withTrimmedRight (n > 0 ? (float) n * 8.0f + 3.0f : 0.0f);
        g.setColour (textBright.withAlpha (0.8f));
        g.setFont (juce::Font (juce::FontOptions (7.0f, juce::Font::bold)));
        g.drawText (txt, band, juce::Justification::centredRight);
    }

    // Parameter lock mark, top-left corner: filled = locked (excluded from
    // randomise and preset loads), hollow = lock mode armed, so it is obvious
    // that a click will toggle the lock rather than move the value.
    if (locked || lockMode)
    {
        const juce::Rectangle<float> lk (2.0f, 2.0f, 7.0f, 7.0f);
        g.setColour (locked ? accent : border.withAlpha (0.55f));
        if (locked)
            g.fillRoundedRectangle (lk, 1.5f);
        else
            g.drawRoundedRectangle (lk.reduced (0.5f), 1.5f, 1.0f);
    }

    // Live-value tooltip: name + value in engine units, refreshed every
    // paint. MOD routings take the first line, value stays second.
    const juce::String valueLine = useCombo
        ? combo.getText()
        : liveValueText (*modSource, paramId);
    juce::String tip;
    if (modDotTip (*modSource, paramId, tip))
        tip = tip + (baseTip.isNotEmpty() ? "\n" + baseTip : "")
                  + (valueLine.isNotEmpty() ? " \u2014 " + valueLine : "");
    else
        tip = baseTip + (valueLine.isNotEmpty() ? " \u2014 " + valueLine : "");
    if (locked)
        tip += "\nLOCKED \u2014 excluded from randomise and preset loads";
    setTip (tip);
}

// Pick flash on top of the knob (children stay visible above the glow).
void Ctl::paintOverChildren (juce::Graphics& g)
{
    paintFlash (g, *this, flashFade());
}

// Lock mode: the knob and combo stop taking mouse clicks, so the click falls
// through to this component and toggles the lock instead of moving the value.
void Ctl::setLockMode (bool on)
{
    lockMode = on;
    slider.setInterceptsMouseClicks (! on, ! on);
    combo.setInterceptsMouseClicks (! on, ! on);
    repaint();
}

void Ctl::mouseDown (const juce::MouseEvent&)
{
    // Only reachable while lock mode is armed (otherwise the slider/combo take
    // the event), which is exactly when a click means "toggle my lock".
    if (lockMode && onLockClick)
        onLockClick();
}

// Same animated dots + tooltips for toggles (VOWEL, P.RAND, etc.).
void ToggleCtl::paint (juce::Graphics& g)
{
    int src[3];
    float amt[3];
    const int n = modDotSources (*modSource, paramId, src, amt);
    for (int k = 0; k < n; ++k)
        paintModDot (g, { (float) getWidth() - 8.0f - k * 8.0f, 6.0f },
                     src[k], amt[k], liveEngine);

    // Toggles show ON/OFF live (a bool param's value is 0/1).
    auto* p = modSource->getParameter (paramId);
    const bool on = p != nullptr && p->getValue() > 0.5f;
    const juce::String state = juce::String (on ? "ON" : "OFF");

    juce::String tip;
    if (modDotTip (*modSource, paramId, tip))
        setTip (tip + (baseTip.isNotEmpty() ? "\n" + baseTip : "")
                  + " \u2014 " + state);
    else
        setTip (baseTip + " \u2014 " + state);
}

// Pick flash for toggles.
void ToggleCtl::paintOverChildren (juce::Graphics& g)
{
    paintFlash (g, *this, flashFade());
}

//==============================================================================
// The mini-switch skin: a pill track with a sliding thumb, drawn inside the
// rectangle handed out by ToggleCtl::resized(). Colours come straight from the
// palette globals so setTheme() + retint() re-skin it with no extra work.
void SwitchLAF::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b,
                                  bool highlighted, bool)
{
    const auto r = b.getLocalBounds().toFloat();
    const bool on = b.getToggleState();

    auto track = bgInset;
    auto borderC = border;
    if (highlighted)
    {
        track = bgInset.brighter (0.05f);
        borderC = border.brighter (0.25f);
    }

    g.setColour (track);
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (borderC);
    g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.5f, 1.0f);

    // Caption of the state the thumb is NOT covering.
    const juce::String offText = b.getToggleState()
        ? juce::String() : b.getButtonText();
    if (offText.isNotEmpty())
    {
        g.setColour (textDim);
        g.setFont (juce::Font (juce::FontOptions (7.0f, juce::Font::bold)));
        g.drawText (offText, r, juce::Justification::centred);
    }

    // Thumb: covers the active side; tick drawn only when engaged.
    const auto thumb = juce::Rectangle<float> (r.getHeight() * 0.64f, r.getHeight())
                           .translated (on ? r.getRight() - r.getHeight() * 0.82f
                                           : r.getX() + r.getHeight() * 0.18f,
                                        r.getY());
    g.setColour (on ? accent : bgPanel.brighter (0.08f));
    g.fillRoundedRectangle (thumb, thumb.getHeight() * 0.5f);
    if (on)
    {
        g.setColour (bgDark);
        const float inset = thumb.getHeight() * 0.30f;
        const float cy = thumb.getCentreY();
        g.drawLine (thumb.getX() + inset, cy, thumb.getRight() - inset, cy, 1.4f);
        g.drawLine (thumb.getCentreX(), thumb.getY() + inset,
                    thumb.getCentreX(), thumb.getBottom() - inset, 1.4f);
    }
}

ToggleCtl::ToggleCtl (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId,
                      const juce::String& text)
    : paramId (paramId), modSource (&apvts)
{
    btn.setButtonText ({});
    btn.setLookAndFeel (&laf);           // mini-switch strip skin
    btn.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    addAndMakeVisible (btn);

    // Base tooltip: the parameter's own human name (overridden by MOD dots).
    if (auto* p = apvts.getParameter (paramId))
        baseTip = p->getName (64);
    setTip (baseTip);

    label.setText (text, juce::dontSendNotification);
    label.setFont (juce::Font (juce::FontOptions (9.0f)));
    label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (label);

    att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, paramId, btn);
    retint();
}

ToggleCtl::~ToggleCtl()
{
    btn.setLookAndFeel (nullptr);
}

void ToggleCtl::retint()
{
    btn.setColour (juce::ToggleButton::textColourId, textDim);
    btn.setColour (juce::ToggleButton::tickColourId, accent);
    btn.setColour (juce::ToggleButton::tickDisabledColourId, border);
    label.setColour (juce::Label::textColourId, textDim);
    btn.repaint();
    label.repaint();
}

void ToggleCtl::resized()
{
    auto b = getLocalBounds();
    label.setBounds (b.removeFromTop (12));

    // Roomy knob cells get a centred full-width mini-switch strip; short
    // cells (LOCK / QUALITY in the bottom bar) get a compact full-width
    // strip with the OFF caption riding on the strip itself.
    auto strip = b.withTrimmedTop (4).withTrimmedBottom (6);
    if (strip.getHeight() >= 10)
    {
        const int h = juce::jmin (18, strip.getHeight());
        btn.setBounds (juce::Rectangle<int> (strip.getX(), strip.getCentreY() - h / 2,
                                             strip.getWidth(), h));
        btn.setButtonText (strip.getWidth() >= 46 ? "OFF" : juce::String());
    }
    else
    {
        const int h = juce::jlimit (6, 18, b.getHeight());
        btn.setBounds (b.withSizeKeepingCentre (b.getWidth(), h));
        btn.setButtonText (b.getWidth() >= 34 ? "OFF" : juce::String());
    }
}

//==============================================================================
Panel::Panel (const juce::String& titleText, ColRole r)
    : title (titleText), role (r), headCol (roleColour (r))
{
    setOpaque (false);
    setRepaintsOnMouseActivity (true);
}

void Panel::retint()
{
    headCol = roleColour (role);
    repaint();
}

void Panel::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    juce::ColourGradient grad (bgPanel.brighter (0.04f), b.getX(), b.getY(),
                               bgPanelLo, b.getX(), b.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (b, 6.0f);

    // Panel lift: subtle inner highlight on hover.
    if (isMouseOver())
    {
        g.setColour (border.brighter (0.25f).withAlpha (0.45f));
        g.drawHorizontalLine (1, b.getX() + 4.0f, b.getRight() - 4.0f);
    }

    auto head = b.removeFromTop (18.0f);
    g.setColour (bgHeader);
    g.fillRoundedRectangle (head, 6.0f);
    g.fillRect (head.withTrimmedTop (7.0f));

    g.setColour (headCol);
    g.fillEllipse (head.getX() + 7.0f, head.getCentreY() - 2.5f, 5.0f, 5.0f);

    g.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
    g.drawText (title.toUpperCase(), head.reduced (17.0f, 0.0f), juce::Justification::centredLeft);

    g.setColour (headCol.withAlpha (0.35f));
    g.fillRect (b.getX() + 6.0f, head.getBottom() - 1.0f, b.getWidth() - 12.0f, 1.0f);

    g.setColour (border.withAlpha (0.85f));
    g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, 1.0f);
}

//==============================================================================
WaveDisplay::WaveDisplay (GoaSynthAudioProcessor& processor, int oscIdx,
                          const juce::String& waveParamId, const juce::String& wtPosParamId,
                          ColRole r)
    : proc (processor), oscIndex (oscIdx), role (r), col (roleColour (r))
{
    waveValue = proc.apvts.getRawParameterValue (waveParamId);
    posValue  = proc.apvts.getRawParameterValue (wtPosParamId);
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    startTimerHz (15);
}

bool WaveDisplay::userMode() const
{
    return (int) goa::ld (waveValue) == 5;
}

void WaveDisplay::timerCallback()
{
    const int   v = (int) goa::ld (waveValue);
    const float p = goa::ld (posValue);
    if (v != lastWave || p != lastPos)
    {
        lastWave = v;
        lastPos = p;
        repaint();
    }
}

juce::Rectangle<float> WaveDisplay::stripRect() const
{
    auto b = getLocalBounds().toFloat();
    return { b.getX() + 4.0f, b.getBottom() - 15.0f, b.getWidth() - 8.0f, 11.0f };
}

juce::Rectangle<float> WaveDisplay::curveRect() const
{
    auto b = getLocalBounds().toFloat();
    return { b.getX() + 1.0f, b.getY() + 1.0f, b.getWidth() - 2.0f,
             b.getHeight() - 19.0f };
}

juce::Rectangle<float> WaveDisplay::toolsRect() const
{
    const auto s = stripRect();
    return { s.getX(), s.getY() - 16.0f, s.getWidth(), 14.0f };
}

WaveDisplay::Zone WaveDisplay::zoneAt (juce::Point<float> pos) const
{
    if (pos.y >= stripRect().getY() - 2.0f)
        return zStrip;
    if (pos.y >= toolsRect().getY() - 2.0f)
        return zTools;
    return zCurve;
}

int WaveDisplay::toolAt (juce::Point<float> pos) const
{
    const auto tb = toolsRect();
    if (pos.y < tb.getY() || pos.y > tb.getBottom())
        return -1;
    const int i = (int) ((pos.x - tb.getX()) / 28.0f);
    const float frac = (pos.x - tb.getX()) - 28.0f * (float) i;
    return (i >= 0 && i < 8 && frac <= 26.0f) ? i : -1;
}

int WaveDisplay::frameAt (juce::Point<float> pos) const
{
    const auto s = stripRect();
    const float t = (pos.x - s.getX()) / juce::jmax (1.0f, s.getWidth());
    return juce::jlimit (0, goa::UserWave::numFrames - 1, (int) (t * (float) goa::UserWave::numFrames));
}

void WaveDisplay::beginSession()
{
    if (editing)
        return;
    if (auto* w = proc.wavetable (oscIndex))
    {
        w->beginEdit();
        editing = true;
    }
}

void WaveDisplay::endSession()
{
    if (! editing)
        return;
    editing = false;
    proc.commitWaves();   // publishes the edit surface and rebuilds audio tables
    repaint();
}

void WaveDisplay::mouseDown (const juce::MouseEvent& e)
{
    if (! userMode())
        return;
    const auto zone = zoneAt (e.position);
    if (zone == zTools)
    {
        if (const int tool = toolAt (e.position); tool >= 0)
            applyTool (tool);
        return;
    }
    if (zone == zStrip)
    {
        selectedFrame = frameAt (e.position);
        repaint();
        return;
    }
    beginSession();
    dragging = true;
    applyStroke (e.position, e.mods.isRightButtonDown());
}

void WaveDisplay::mouseMove (const juce::MouseEvent& e)
{
    if (! userMode())
        return;
    const int t = zoneAt (e.position) == zTools ? toolAt (e.position) : -1;
    if (t != hoverTool)
    {
        hoverTool = t;
        setMouseCursor (t >= 0 ? juce::MouseCursor::PointingHandCursor
                               : juce::MouseCursor::CrosshairCursor);
        repaint();
    }
}

void WaveDisplay::mouseExit (const juce::MouseEvent&)
{
    if (hoverTool != -1)
    {
        hoverTool = -1;
        repaint();
    }
}

void WaveDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (! userMode())
        return;
    if (dragging)
    {
        applyStroke (e.position, e.mods.isRightButtonDown());
        return;
    }
    if (e.mods.isLeftButtonDown() && zoneAt (e.position) == zStrip)
    {
        selectedFrame = frameAt (e.position);
        repaint();
    }
}

void WaveDisplay::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
    endSession();
}

void WaveDisplay::mouseDoubleClick (const juce::MouseEvent&)
{
    if (! userMode())
        return;
    if (auto* w = proc.wavetable (oscIndex))
    {
        w->reset();              // saw in frame 0, empty frames, publishes
        proc.commitWaves();
        repaint();
    }
}

void WaveDisplay::applyTool (int tool)
{
    // Tool 7 is not a frame transform: it hands off to the editor, which owns
    // the file chooser. Handled before beginEdit() so a cancelled dialog
    // cannot leave an uncommitted edit session behind.
    if (tool == 7)
    {
        if (onLoadWav)
            onLoadWav();
        return;
    }

    auto* w = proc.wavetable (oscIndex);
    if (w == nullptr)
        return;
    w->beginEdit();
    using SK = goa::UserWave::ShapeKind;
    switch (tool)
    {
        case 0:  w->smoothFrame (selectedFrame); break;
        case 1:  w->flipFrame (selectedFrame); break;
        case 2:  w->normalizeFrame (selectedFrame); break;
        case 3:  w->generateFrame (selectedFrame, SK::pulse25); break;
        case 4:  w->generateFrame (selectedFrame, SK::pulse50); break;
        case 5:  w->generateFrame (selectedFrame, SK::formant); break;
        case 6:  w->generateFrame (selectedFrame, SK::spikes); break;
        default: return;
    }
    proc.commitWaves();   // publishes the edit surface and rebuilds audio tables
    repaint();
}

void WaveDisplay::applyStroke (juce::Point<float> pos, bool erase)
{
    auto* w = proc.wavetable (oscIndex);
    if (w == nullptr)
        return;
    const auto cb = curveRect();
    const float x01 = juce::jlimit (0.0f, 1.0f,
                                   (pos.x - cb.getX()) / juce::jmax (1.0f, cb.getWidth()));
    const float y01 = juce::jlimit (0.0f, 1.0f,
                                   (cb.getBottom() - pos.y) / juce::jmax (1.0f, cb.getHeight()));
    w->write (selectedFrame, x01, erase ? 0.0f : y01 * 2.0f - 1.0f, 3);
    repaint();
}

void WaveDisplay::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (bgInset);
    g.fillRoundedRectangle (b, 3.0f);
    g.setColour (border.withAlpha (0.7f));
    g.drawRoundedRectangle (b.reduced (0.5f), 3.0f, 1.0f);

    const int w = juce::jlimit (0, 5, (int) goa::ld (waveValue));

    if (w == 5)
    {
        if (auto* wv = proc.wavetable (oscIndex))
        {
            const auto cb = curveRect();
            const float mid = cb.getCentreY();
            const float amp = cb.getHeight() * 0.40f;
            const int N = 256;
            auto buildCurve = [&] (juce::Path& p, auto sampleFn)
            {
                for (int i = 0; i <= N; ++i)
                {
                    const float t = (float) i / (float) N;
                    const float v = sampleFn (t);
                    const float x = cb.getX() + t * cb.getWidth();
                    const float y = mid - juce::jlimit (-1.0f, 1.0f, v) * amp;
                    if (i == 0) p.startNewSubPath (x, y);
                    else        p.lineTo (x, y);
                }
            };

            // Ghost outlines of the other frames (nearest frames brighter).
            for (int f = 0; f < goa::UserWave::numFrames; ++f)
            {
                if (f == selectedFrame && editing)
                    continue;
                const float closeness = 1.0f - std::abs (f - selectedFrame)
                                              / (float) goa::UserWave::numFrames;
                juce::Path gp;
                buildCurve (gp, [wv, f] (float t) { return wv->sampleFrame (t, f); });
                g.setColour (col.withAlpha (0.06f + 0.14f * closeness));
                g.strokePath (gp, juce::PathStrokeType (1.0f));
            }

            // Live morphed curve at the current WT position.
            const float livePos = juce::jlimit (0.0f, 1.0f, goa::ld (posValue));
            {
                juce::Path p;
                buildCurve (p, [wv, livePos] (float t) { return wv->sample (t, livePos); });
                g.setColour (col.withAlpha (0.30f));
                g.strokePath (p, juce::PathStrokeType (3.5f));
                g.setColour (col);
                g.strokePath (p, juce::PathStrokeType (1.5f));
            }

            // Frame being edited, drawn bright on top (from the edit surface).
            {
                juce::Path p;
                const float* ed = wv->editFrame (selectedFrame);
                buildCurve (p, [ed] (float t)
                {
                    const float tt = juce::jlimit (0.0f, 0.99999f, t);
                    const int i = (int) (tt * (float) (goa::UserWave::size - 1));
                    return ed[i];
                });
                g.setColour (textBright.withAlpha (0.9f));
                g.strokePath (p, juce::PathStrokeType (1.2f));
            }

            // Sketch grid + hint.
            g.setColour (border.withAlpha (0.35f));
            g.drawLine ({ cb.getX(), mid, cb.getRight(), mid }, 1.0f);
            for (int i = 1; i < 8; ++i)
            {
                const float x = cb.getX() + cb.getWidth() * (float) i / 8.0f;
                g.drawLine ({ x, cb.getY() + 2.0f, x, cb.getBottom() - 2.0f }, 1.0f);
            }
            g.setColour (textDim.withAlpha (0.8f));
            g.setFont (juce::Font (juce::FontOptions (9.0f)));
            g.drawText (juce::String ("FRAME ") + juce::String (selectedFrame + 1)
                            + "  \u2014  DRAG DRAW \u2022 RIGHT-DRAG ERASE \u2022 DBL-CLICK RESET",
                        cb.reduced (6.0f, 4.0f), juce::Justification::bottomLeft);

            // Tools row: transforms, one-click shapes, then the WAV importer.
            {
                static constexpr const char* labels[8] =
                    { "SMOOTH", "FLIP", "NORM", "P25", "P50", "FORM", "SPK", "WAV" };
                const auto tb = toolsRect();
                for (int i = 0; i < 8; ++i)
                {
                    auto cell = juce::Rectangle<float> (tb.getX() + 28.0f * (float) i,
                                                        tb.getY(), 26.0f, tb.getHeight());
                    g.setColour (i == hoverTool ? col.withAlpha (0.30f) : bgPanel);
                    g.fillRoundedRectangle (cell, 2.0f);
                    g.setColour (i == hoverTool ? textBright : border.withAlpha (0.9f));
                    g.drawRoundedRectangle (cell.reduced (0.5f), 2.0f, 1.0f);
                    g.setColour (i == hoverTool ? textBright : textDim);
                    g.setFont (juce::Font (juce::FontOptions (7.5f)));
                    g.drawText (labels[i], cell, juce::Justification::centred);
                }
            }

            // Frame strip.
            const auto s = stripRect();
            const float cw = s.getWidth() / (float) goa::UserWave::numFrames;
            for (int f = 0; f < goa::UserWave::numFrames; ++f)
            {
                const auto cell = juce::Rectangle<float> (s.getX() + cw * f, s.getY(),
                                                          cw - 2.0f, s.getHeight());
                g.setColour (f == selectedFrame ? col.withAlpha (0.55f) : bgPanel);
                g.fillRoundedRectangle (cell, 2.0f);

                bool hasContent = false;
                for (int k = 0; k <= 8 && ! hasContent; ++k)
                    if (std::abs (wv->sampleFrame ((float) k / 8.0f, f)) > 0.01f)
                        hasContent = true;
                g.setColour (hasContent ? col.brighter (0.25f) : border.withAlpha (0.6f));
                g.drawRoundedRectangle (cell.reduced (0.5f), 2.0f, 1.0f);

                g.setColour (f == selectedFrame ? textBright : textDim.withAlpha (0.7f));
                g.setFont (juce::Font (juce::FontOptions (8.0f)));
                g.drawText (juce::String (f + 1), cell, juce::Justification::centred);
            }
        }
        return;
    }

    const float mid = b.getCentreY();
    const float amp = b.getHeight() * 0.36f;
    juce::Path p;
    const int N = 240;
    for (int i = 0; i <= N; ++i)
    {
        const float t = (float) i / (float) N;
        float v = 0.0f;
        switch (w)
        {
            case 1:  v = t < 0.5f ? 1.0f : -1.0f; break;
            case 2:  v = t < 0.3f ? 1.0f : -1.0f; break;
            case 3:  v = 1.0f - 4.0f * std::fabs (t - 0.5f); break;
            case 4:  v = std::sin (juce::MathConstants<float>::twoPi * t); break;
            default: v = 2.0f * t - 1.0f; break;
        }
        const float x = b.getX() + t * b.getWidth();
        if (i == 0) p.startNewSubPath (x, mid - v * amp);
        else        p.lineTo (x, mid - v * amp);
    }
    g.setColour (col.withAlpha (0.28f));
    g.strokePath (p, juce::PathStrokeType (3.5f));
    g.setColour (col);
    g.strokePath (p, juce::PathStrokeType (1.5f));
}

//==============================================================================
FilterGraph::FilterGraph (juce::AudioProcessorValueTreeState& apvts, ColRole r)
    : role (r), col (roleColour (r))
{
    cut = apvts.getRawParameterValue (param::cutoff);
    reso = apvts.getRawParameterValue (param::reso);
    type = apvts.getRawParameterValue (param::filterType);
    setTooltip ("Live filter response \u2014 follows the TYPE, CUTOFF and RESO knobs");
    startTimerHz (20);
}

void FilterGraph::timerCallback()
{
    const float c = goa::ld (cut);
    const float r = goa::ld (reso);
    const int t = (int) goa::ld (type);
    if (c != lastCut || r != lastReso || t != lastType)
    {
        lastCut = c;
        lastReso = r;
        lastType = t;
        repaint();
    }
}

void FilterGraph::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (bgInset);
    g.fillRoundedRectangle (b, 3.0f);
    g.setColour (border.withAlpha (0.7f));
    g.drawRoundedRectangle (b.reduced (0.5f), 3.0f, 1.0f);

    g.setColour (border.withAlpha (0.30f));
    for (int i = 1; i < 4; ++i)
    {
        const float x = b.getX() + b.getWidth() * (float) i / 4.0f;
        g.drawLine ({ x, b.getY() + 3.0f, x, b.getBottom() - 3.0f }, 1.0f);
    }

    const float cHz = juce::jlimit (20.0f, 20000.0f, goa::ld (cut));
    const float Q = 0.5f + goa::ld (reso) * 14.5f;
    const int ty = juce::jlimit (0, 4, (int) goa::ld (type));

    const float mid = b.getCentreY();
    const float scale = b.getHeight() * 0.0125f;
    const int Wpx = juce::jmax (8, (int) b.getWidth());

    juce::Path curve;
    for (int px = 0; px <= Wpx; px += 2)
    {
        const float f = 20.0f * std::pow (10.0f, 3.0f * (float) px / (float) Wpx);
        const float x = f / cHz;
        const float dd = 1.0f - x * x;
        const float qq = x / Q;
        const float lp = 1.0f / std::sqrt (dd * dd + qq * qq);
        float mag = lp;
        if (ty == 1) mag = lp * lp;
        else if (ty == 2)
        {
            const float hx = 1.0f / juce::jmax (x, 0.0001f);
            const float hd = 1.0f - hx * hx;
            const float hq = hx / Q;
            mag = 1.0f / std::sqrt (hd * hd + hq * hq);
        }
        else if (ty == 3) mag = qq / std::sqrt (dd * dd + qq * qq);
        else if (ty == 4) mag = std::fabs (dd) / std::sqrt (dd * dd + qq * qq);

        const float db = 20.0f * std::log10 (juce::jlimit (0.001f, 10.0f, mag));
        const float y = juce::jlimit (b.getY() + 2.0f, b.getBottom() - 2.0f, mid - db * scale);
        if (px == 0) curve.startNewSubPath ((float) px, y);
        else         curve.lineTo ((float) px, y);
    }

    juce::Path fill = curve;
    fill.lineTo ((float) Wpx, b.getBottom());
    fill.lineTo (0.0f, b.getBottom());
    fill.closeSubPath();
    g.setColour (col.withAlpha (0.15f));
    g.fillPath (fill);

    // Glow stroke for the filter curve.
    g.setColour (col.withAlpha (0.35f));
    g.strokePath (curve, juce::PathStrokeType (3.5f,
        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (col.brighter (0.15f));
    g.strokePath (curve, juce::PathStrokeType (1.5f,
        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float xc = std::log10 (cHz / 20.0f) / 3.0f * b.getWidth() + b.getX();
    g.setColour (textDim.withAlpha (0.55f));
    g.drawLine ({ xc, b.getY() + 2.0f, xc, b.getBottom() - 2.0f }, 1.0f);
}

//==============================================================================
EnvGraph::EnvGraph (juce::AudioProcessorValueTreeState& apvts, const juce::String& idA,
                    const juce::String& idD, const juce::String& idS, const juce::String& idR,
                    ColRole r)
    : role (r), col (roleColour (r))
{
    pA = apvts.getParameter (idA);
    pD = apvts.getParameter (idD);
    pS = apvts.getParameter (idS);
    pR = apvts.getParameter (idR);
    setTooltip ("Drag to reshape the envelope; the MOD matrix can route it as a"
                " source (SRC cell, then click this graph)");
    startTimerHz (30); // live repaint so preset/AI changes show instantly
}

float EnvGraph::norm (juce::RangedAudioParameter* p) const
{
    return p != nullptr ? p->convertTo0to1 (p->getValue()) : 0.0f;
}

void EnvGraph::setNorm (juce::RangedAudioParameter* p, float v) const
{
    if (p != nullptr)
        p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, v));
}

EnvGraph::Geo EnvGraph::geo() const
{
    const float w = (float) getWidth();
    const float h = (float) getHeight();
    Geo G;
    G.w = w;
    G.hTop = h * 0.14f;
    G.hBot = h * 0.92f;
    G.wA = norm (pA) * w * 0.35f;
    G.wD = norm (pD) * w * 0.25f;
    G.wS = w * 0.15f;
    G.wR = norm (pR) * w * 0.25f;
    G.susY = G.hBot - norm (pS) * (G.hBot - G.hTop);
    return G;
}

void EnvGraph::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (bgInset);
    g.fillRoundedRectangle (b, 3.0f);
    paintFlash (g, *this, flashFade());   // MOD source-pick confirmation
    g.setColour (border.withAlpha (0.7f));
    g.drawRoundedRectangle (b.reduced (0.5f), 3.0f, 1.0f);

    const auto G = geo();
    const float xA = G.wA, xD = G.wA + G.wD, xS = xD + G.wS, xR = xS + G.wR;

    juce::Path shape;
    shape.startNewSubPath (0.0f, G.hBot);
    shape.lineTo (0.0f, G.hTop);
    shape.lineTo (xA, G.hTop);
    shape.lineTo (xD, G.susY);
    shape.lineTo (xS, G.susY);
    shape.lineTo (juce::jmin (xR, G.w), G.hBot);
    shape.closeSubPath();

    g.setColour (col.withAlpha (0.16f));
    g.fillPath (shape);
    g.setColour (col);
    g.strokePath (shape, juce::PathStrokeType (1.6f));

    const float nodes[3][2] = { { xA, G.hTop }, { xD, G.susY }, { xR, G.hBot } };
    for (auto& n : nodes)
    {
        g.setColour (col.withAlpha (0.30f));
        g.fillEllipse (n[0] - 4.5f, n[1] - 4.5f, 9.0f, 9.0f);
        g.setColour (textBright);
        g.fillEllipse (n[0] - 2.5f, n[1] - 2.5f, 5.0f, 5.0f);
    }
}

void EnvGraph::mouseDown (const juce::MouseEvent& e)
{
    const auto G = geo();
    const float xA = G.wA, xD = G.wA + G.wD, xS = xD + G.wS, xR = xS + G.wR;
    const float mx = e.position.x, my = e.position.y;

    if      (std::abs (mx - xA) < 14.0f)                     dragging = atk;
    else if (std::abs (mx - xD) < 14.0f)                     dragging = dec;
    else if (mx > xS - 6.0f && std::abs (mx - xR) < 20.0f)   dragging = rel;
    else if (std::abs (my - G.susY) < 14.0f)                 dragging = sus;
    else                                                     dragging = mx < xS ? dec : rel;

    if (dragging == atk) startNorm = norm (pA);
    else if (dragging == dec) startNorm = norm (pD);
    else if (dragging == rel) startNorm = norm (pR);
}

void EnvGraph::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging == none)
        return;
    const auto G = geo();
    if (dragging == sus)
    {
        setNorm (pS, (G.hBot - e.position.y) / (G.hBot - G.hTop));
    }
    else
    {
        juce::RangedAudioParameter* p = dragging == atk ? pA : dragging == dec ? pD : pR;
        const float span = dragging == atk ? G.w * 0.35f : G.w * 0.25f;
        setNorm (p, startNorm + (e.position.x - e.mouseDownPosition.x) / span);
    }
    repaint();
}

void EnvGraph::mouseUp (const juce::MouseEvent&)
{
    dragging = none;
}

//==============================================================================
LfoGraph::LfoGraph (juce::AudioProcessorValueTreeState& apvts, const juce::String& rateId,
                    const juce::String& waveId, ColRole r)
    : role (r), col (roleColour (r)), rateParamId (rateId)
{
    rate = apvts.getRawParameterValue (rateId);
    wave = apvts.getRawParameterValue (waveId);
    setTooltip ("LFO shape and current phase; the MOD matrix can route this LFO"
                " as a source (SRC cell, then click this scope)");
    startTimerHz (30);
}

void LfoGraph::timerCallback() { repaint(); }

float LfoGraph::shapeAt (int w, float p) const
{
    switch (w)
    {
        case 1:  return 1.0f - 4.0f * std::fabs (p - 0.5f);
        case 2:  return p < 0.5f ? 1.0f : -1.0f;
        case 3:
        {
            const float h = std::fmod (std::sin (p * 91.7f) * 43758.5453f, 1.0f);
            return h * 2.0f - 1.0f;
        }
        default: return std::sin (juce::MathConstants<float>::twoPi * p);
    }
}

void LfoGraph::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (bgInset);
    g.fillRoundedRectangle (b, 3.0f);
    paintFlash (g, *this, flashFade());   // MOD source-pick confirmation
    g.setColour (border.withAlpha (0.7f));
    g.drawRoundedRectangle (b.reduced (0.5f), 3.0f, 1.0f);

    const int w = juce::jlimit (0, 3, (int) goa::ld (wave));
    const float rateHz = juce::jmax (0.02f, goa::ld (rate));
    const float mid = b.getCentreY();
    const float amp = b.getHeight() * 0.38f;

    juce::Path p;
    const int N = 240;
    for (int i = 0; i <= N; ++i)
    {
        const float t = (float) i / (float) N;
        const float tt = (w == 3) ? (std::floor (t * 16.0f) + 0.5f) / 16.0f : t;
        const float v = shapeAt (w, tt);
        const float x = b.getX() + t * b.getWidth();
        if (i == 0) p.startNewSubPath (x, mid - v * amp);
        else        p.lineTo (x, mid - v * amp);
    }
    g.setColour (col.withAlpha (0.28f));
    g.strokePath (p, juce::PathStrokeType (3.0f));
    g.setColour (col);
    g.strokePath (p, juce::PathStrokeType (1.5f));

    const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    const float ph = (float) std::fmod (now * (double) rateHz, 1.0);
    const float ptt = (w == 3) ? (std::floor (ph * 16.0f) + 0.5f) / 16.0f : ph;
    const float yv = mid - shapeAt (w, ptt) * amp;
    const float xv = b.getX() + ph * b.getWidth();

    g.setColour (textDim.withAlpha (0.35f));
    g.drawLine ({ xv, b.getY() + 2.0f, xv, b.getBottom() - 2.0f }, 1.0f);
    g.setColour (textBright);
    g.fillEllipse (xv - 3.0f, yv - 3.0f, 6.0f, 6.0f);
}

//==============================================================================
// 16-step trancegate / arp sequencer strips.
//==============================================================================
StepStrip::StepStrip (GoaSynthAudioProcessor& p, Kind k, ColRole r)
    : proc (p), kind (k), role (r), col (roleColour (r))
{
    setTooltip (kind == gateStrip
        ? "Trancegate: click/drag steps to gate the sound; right-click a step for"
          " gate settings; SYNC/OCT/SHAPE cycle their params; COPY mirrors the arp"
          " pattern; SHAPE picks the edge: hard square, smooth, trance saw or"
          " triangle"
        : "Arp sequencer: click/drag steps for semitones, lower band sets velocity,"
          " the band above it sets gate length (5..100% of the step); right-click"
          " accents; SYNC/OCT/DIR/SCALE/FILL cycle their params; STRUM direction"
          " plays the held chord as a staggered strum each step");
    if (kind == gateStrip)
    {
        syncPar  = proc.apvts.getParameter (param::gateSync);
        octPar   = proc.apvts.getParameter (param::gateDepth);
        shapePar = proc.apvts.getParameter (param::gateShape);
    }
    else
    {
        syncPar  = proc.apvts.getParameter (param::arpSync);
        octPar   = proc.apvts.getParameter (param::arpOct);
        dirPar   = proc.apvts.getParameter (param::arpDir);
        scalePar = proc.apvts.getParameter (param::arpScale);
        rootPar  = proc.apvts.getParameter (param::arpRoot);
    }
    for (int i = 0; i < 16; ++i)
        steps[(size_t) i] = proc.apvts.getParameter (
            kind == gateStrip ? param::gateStep (i) : param::arpStep (i));
    if (kind == arpStrip)
    {
        for (int i = 0; i < 16; ++i)
        {
            vels[(size_t) i]  = proc.apvts.getParameter (param::arpVel (i));
            gates[(size_t) i] = proc.apvts.getParameter (param::arpGate (i));
        }
    }
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    startTimerHz (30);
}

void StepStrip::timerCallback()
{
    const float ph = proc.gatePlayhead.load (std::memory_order_relaxed);
    const int st = proc.gateStep.load (std::memory_order_relaxed);
    const int sc = scalePar != nullptr
        ? (int) scalePar->getNormalisableRange().convertFrom0to1 (scalePar->getValue()) : -1;
    const int rt = rootPar != nullptr
        ? (int) rootPar->getNormalisableRange().convertFrom0to1 (rootPar->getValue()) : -1;
    if (ph != lastPh || st != lastLive || sc != lastScaleIdx || rt != lastRootIdx
        || (kind == arpStrip && patternStepsChanged()))
    {
        lastPh = ph;
        lastLive = st;
        lastScaleIdx = sc;
        lastRootIdx = rt;
        repaint();
    }
}

juce::Rectangle<float> StepStrip::syncCell() const
{
    return getLocalBounds().toFloat().removeFromLeft (48).reduced (2.0f);
}

juce::Rectangle<float> StepStrip::octCell() const
{
    return getLocalBounds().toFloat()
        .withTrimmedLeft (48).removeFromLeft (48).reduced (2.0f);
}

juce::Rectangle<float> StepStrip::copyCell() const
{
    return getLocalBounds().toFloat()
        .withTrimmedLeft (88).removeFromLeft (42).reduced (2.0f);
}

juce::Rectangle<float> StepStrip::patCell() const
{
    return getLocalBounds().toFloat()
        .withTrimmedLeft (130).removeFromLeft (52).reduced (2.0f);
}

juce::Rectangle<float> StepStrip::shapeCell() const
{
    return getLocalBounds().toFloat()
        .withTrimmedLeft (182).removeFromLeft (48).reduced (2.0f);
}

juce::Rectangle<float> StepStrip::dirCell() const
{
    return getLocalBounds().toFloat()
        .withTrimmedLeft (96).removeFromLeft (42).reduced (2.0f);
}

juce::Rectangle<float> StepStrip::scaleCell() const
{
    return getLocalBounds().toFloat()
        .withTrimmedLeft (138).removeFromLeft (48).reduced (2.0f);
}

juce::Rectangle<float> StepStrip::fillCell() const
{
    return getLocalBounds().toFloat()
        .withTrimmedLeft (186).removeFromLeft (40).reduced (2.0f);
}

// In-key generative fill: writes 3-6 empty steps with in-scale semitones
// (CHROMATIC uses everything) and matching velocities. Leaves musical space
// instead of machine-gunning every rest, so existing patterns stay intact.
void StepStrip::fillInKey()
{
    const int scaleIdx = scalePar != nullptr
        ? juce::jlimit (0, tuning::numScales() - 1,
            (int) scalePar->getNormalisableRange().convertFrom0to1 (scalePar->getValue()))
        : 0;
    juce::Random rng (juce::Random::getSystemRandom().nextInt64());

    std::vector<int> cand;                       // choice indices (semitone + 1)
    if (scaleIdx == 0)
    {
        for (int s = 1; s <= 12; ++s) cand.push_back (s);
    }
    else
    {
        for (const int s : tuning::scales()[(size_t) scaleIdx].semis)
            if (s >= 0 && s <= 11) cand.push_back (s + 1);
    }
    if (cand.empty())
        return;

    const int maxWrites = rng.nextInt (juce::Range<int> (3, 7));   // 3..6
    int writes = 0;
    for (int i = 0; i < 16 && writes < maxWrites; ++i)
    {
        auto* par = steps[(size_t) i];
        if (par == nullptr || par->getValue() > 0.5f)
            continue;                            // never overwrite the composer
        if (rng.nextFloat() > 0.55f)
            continue;                            // leave breathing room
        par->beginChangeGesture();
        par->setValueNotifyingHost (par->getNormalisableRange()
                                        .convertTo0to1 ((float) cand[(size_t)
                                            rng.nextInt ((int) cand.size())]));
        par->endChangeGesture();
        if (vels[(size_t) i] != nullptr)
        {
            vels[(size_t) i]->beginChangeGesture();
            vels[(size_t) i]->setValueNotifyingHost (
                vels[(size_t) i]->getNormalisableRange()
                    .convertTo0to1 (0.6f + 0.4f * rng.nextFloat()));
            vels[(size_t) i]->endChangeGesture();
        }
        ++writes;
    }
    repaint();
}

bool StepStrip::patternStepsChanged()
{
    if (kind != arpStrip)
        return false;
    bool changed = false;
    for (int i = 0; i < 16; ++i)
    {
        const auto v = (juce::uint8) juce::jlimit (0, 255,
            (int) juce::roundToInt ((steps[(size_t) i] != nullptr
                ? steps[(size_t) i]->getValue() : 0.0f) * 255.0f));
        if (v != lastSteps[(size_t) i])
        {
            lastSteps[(size_t) i] = v;
            changed = true;
        }
    }
    return changed;
}

// Classic trancegate shapes. Each entry: division index into the gateSync
// list, then 16 on/off steps. MANUAL (0) means hand-edited.
static const juce::StringArray patternNames()
{
    return { "MANUAL", "UPLIFT", "OFF-BEAT", "ROLLER", "SLOWROCK", "16THS", "OFF+4TH" };
}

static const std::array<int, 16>& patternSteps (int idx)
{
    static const std::array<std::array<int, 16>, 7> patterns = {{
        {},                                          // 0 MANUAL: unused
        { 0,0,0,1, 0,0,0,1, 0,0,0,1, 0,0,0,1 },      // 1 UPLIFT: 3-on/1-off
        { 0,1,1,1, 0,1,1,1, 0,1,1,1, 0,1,1,1 },      // 2 OFF-BEAT: house off-beat
        { 0,1,0,1, 0,1,0,1, 0,1,0,1, 0,1,0,1 },      // 3 ROLLER: driving 8ths
        { 0,0,0,0, 1,0,0,0, 0,0,0,0, 1,0,0,0 },      // 4 SLOWROCK: half-time stabs
        { 1,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1 },      // 5 16THS: constant gate
        { 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,1,1 },      // 6 OFF+4TH: long swell into the bar
    }};
    return patterns[(size_t) juce::jlimit (0, 6, idx)];
}

static int patternSync (int idx)
{
    // Division index into gateSyncs: 0=1/32, 1=1/16, 3=1/8, 4=1/8T.
    switch (idx)
    {
        case 1: case 3: case 5: case 6: return 1;   // 1/16
        case 2:                            return 3;   // 1/8
        case 4:                            return 4;   // 1/8T
        default:                           return 1;
    }
}

// Mirror the arp strip's steps onto the gate: any programmed note (value >= 1)
// becomes a gate-open step, rests close it. Hand-edit after copying to taste.
void StepStrip::copyFromArp()
{
    for (int i = 0; i < 16; ++i)
    {
        auto* src = proc.apvts.getParameter (param::arpStep (i));
        auto* dst = steps[(size_t) i];
        if (src == nullptr || dst == nullptr)
            continue;
        const int semi = (int) src->getNormalisableRange().convertFrom0to1 (src->getValue());
        dst->beginChangeGesture();
        dst->setValueNotifyingHost (semi >= 1 ? 1.0f : 0.0f);
        dst->endChangeGesture();
    }
    patternIdx = 0;
    repaint();
}

void StepStrip::applyPattern (int idx)
{
    patternIdx = juce::jlimit (0, 6, idx);
    if (patternIdx == 0)
        return;

    if (auto* sync = syncPar)
    {
        sync->beginChangeGesture();
        sync->setValueNotifyingHost (
            sync->getNormalisableRange().convertTo0to1 ((float) patternSync (patternIdx)));
        sync->endChangeGesture();
    }
    for (int i = 0; i < 16; ++i)
    {
        if (auto* par = steps[(size_t) i])
        {
            par->beginChangeGesture();
            par->setValueNotifyingHost (
                patternSteps (patternIdx)[(size_t) i] != 0 ? 1.0f : 0.0f);
            par->endChangeGesture();
        }
    }
    repaint();
}

juce::Rectangle<float> StepStrip::cellRect (int i) const
{
    // Gate strip has COPY + PATTERN cells before the steps; arp has DIR + SCALE
    // + FILL.
    auto b = getLocalBounds().toFloat().withTrimmedLeft (kind == gateStrip ? 230.0f : 226.0f);
    const float w = b.getWidth() / 16.0f;
    return b.removeFromLeft (w * (float) (i + 1)).removeFromRight (w).reduced (1.5f);
}

juce::Rectangle<float> StepStrip::velRect (int i) const
{
    const auto c = cellRect (i);
    return { c.getX(), c.getBottom() - 7.0f, c.getWidth(), 7.0f };
}

// Gate-length band: a 6 px strip directly above the velocity band.
juce::Rectangle<float> StepStrip::gateRect (int i) const
{
    const auto c = cellRect (i);
    return { c.getX(), c.getBottom() - 14.0f, c.getWidth(), 6.0f };
}

int StepStrip::stepAt (juce::Point<float> pos) const
{
    for (int i = 0; i < 16; ++i)
        if (cellRect (i).contains (pos))
            return i;
    return -1;
}

void StepStrip::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    g.setColour (goaui::bgInset);
    g.fillRoundedRectangle (b, 3.0f);
    g.setColour (goaui::border);
    g.drawRoundedRectangle (b, 3.0f, 1.0f);

    auto labelled = [&g] (juce::Rectangle<float> cell, const juce::String& txt,
                          juce::Colour c)
    {
        g.setColour (goaui::bgPanel);
        g.fillRoundedRectangle (cell, 2.0f);
        g.setColour (c);
        g.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
        g.drawText (txt, cell, juce::Justification::centred);
    };

    if (syncPar != nullptr)
        labelled (syncCell(), syncPar->getCurrentValueAsText().toUpperCase(), col);
    if (octPar != nullptr)
        labelled (octCell(),
                  kind == gateStrip
                      ? "DEPTH " + juce::String (juce::roundToInt (octPar->getNormalisableRange().convertFrom0to1 (octPar->getValue()) * 100.0f)) + "%"
                      : "OCT " + octPar->getCurrentValueAsText(),
                  col);
    if (kind == gateStrip)
        labelled (copyCell(), "COPY", goaui::accentA.withAlpha (0.9f));
    if (kind == gateStrip)
        labelled (patCell(), patternNames()[(size_t) patternIdx],
                  patternIdx == 0 ? goaui::textDim : goaui::accentA);
    if (kind == gateStrip && shapePar != nullptr)
    {
        const int sh = juce::jlimit (0, 3,
            (int) shapePar->getNormalisableRange().convertFrom0to1 (shapePar->getValue()));
        labelled (shapeCell(),
                  juce::StringArray { "SQR", "SMTH", "SAW", "TRI" }[(size_t) sh],
                  sh == 0 ? goaui::textDim : goaui::accent);
    }
    if (kind == arpStrip && dirPar != nullptr)
        labelled (dirCell(), dirPar->getCurrentValueAsText().toUpperCase(), goaui::accent);

    // SCALE cell: name + a mini keyboard of 12 LEDs showing which degrees of
    // the current scale the enabled arp steps actually hit (green dot under
    // each used pitch class, lit brighter for the root).
    if (kind == arpStrip)
    {
        const int scaleIdx = scalePar != nullptr
            ? juce::jlimit (0, tuning::numScales() - 1,
                (int) scalePar->getNormalisableRange().convertFrom0to1 (scalePar->getValue()))
            : 0;
        const int rootPc = rootPar != nullptr
            ? juce::jlimit (0, 11,
                (int) rootPar->getNormalisableRange().convertFrom0to1 (rootPar->getValue()))
            : 0;

        auto sc = scaleCell();
        juce::String name = tuning::scales()[(size_t) scaleIdx].name;
        if (name.length() > 8) name = name.substring (0, 8);
        const auto nameRow = sc.removeFromTop (12.0f);
        labelled (nameRow, name, goaui::accent);

        auto row = sc.withTrimmedTop (2.0f);
        const float ledW = row.getWidth() / 12.0f;

        std::array<bool, 12> inScale {};
        for (int pc = 0; pc < 12; ++pc)
            inScale[(size_t) pc] =
                tuning::nearestScaleNote (rootPc + pc, scaleIdx, rootPc) == rootPc + pc;

        std::array<bool, 12> used {};
        for (const auto* par : steps)
            if (par != nullptr && par->getValue() > 0.5f)
            {
                const int semi = juce::jlimit (1, 24,
                    (int) par->getNormalisableRange().convertFrom0to1 (par->getValue()));
                const int pc = ((rootPc + semi - 1) % 12 + 12) % 12;
                used[(size_t) pc] = true;
            }

        for (int pc = 0; pc < 12; ++pc)
        {
            auto led = row.removeFromLeft (ledW).withSizeKeepingCentre (ledW - 1.5f, 4.0f);
            const bool root = pc == rootPc;
            if (used[(size_t) pc])
                g.setColour (root ? goaui::accentB : goaui::accent);   // hit degree
            else if (inScale[(size_t) pc])
                g.setColour (goaui::accent.withAlpha (0.30f));         // scale, unused
            else
                g.setColour (goaui::bgPanelLo);                        // off-scale
            g.fillRoundedRectangle (led, 1.2f);
        }
    }

    if (kind == arpStrip)
        labelled (fillCell(), "FILL", goaui::accentA.withAlpha (0.9f));

    const float ph = proc.gatePlayhead.load (std::memory_order_relaxed);
    const int liveStep = proc.gateStep.load (std::memory_order_relaxed);

    for (int i = 0; i < 16; ++i)
    {
        auto cell = cellRect (i);
        const bool on = steps[(size_t) i] != nullptr
                        && steps[(size_t) i]->getValue() > 0.5f
                        && (kind == gateStrip || steps[(size_t) i]->getValue() > 0.0f);
        const bool beat = (i % 4) == 0;
        const bool live = kind == gateStrip && liveStep == i;

        if (kind == gateStrip)
        {
            const float hgt = on ? cell.getHeight() : cell.getHeight() * 0.55f;
            auto r = cell.withSizeKeepingCentre (cell.getWidth(), hgt);
            g.setColour (on ? (beat ? col : col.withAlpha (0.72f))
                            : goaui::bgPanelLo);
            g.fillRoundedRectangle (r, 2.0f);
        }
        else
        {
            const int semi = steps[(size_t) i] != nullptr
                ? (int) steps[(size_t) i]->getNormalisableRange()
                        .convertFrom0to1 (steps[(size_t) i]->getValue()) - 1
                : -1;
            auto body = cell.withTrimmedBottom (16.0f);  // room for gate + vel bands
            if (semi < 0)
            {
                g.setColour (goaui::bgPanelLo);
                g.fillRoundedRectangle (body, 2.0f);
            }
            else
            {
                g.setColour (beat ? col.withAlpha (0.95f) : col.withAlpha (0.65f));
                g.fillRoundedRectangle (body, 2.0f);
                g.setColour (goaui::bgDark);
                g.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
                g.drawText (semi == 0 ? "0" : "+" + juce::String (semi),
                            body, juce::Justification::centred);
            }

            // Per-step gate-length bar (above the velocity bar): width shows
            // what fraction of the step the note holds, from a 5% stab to a
            // 100% legato tie.
            {
                const float gf = gates[(size_t) i] != nullptr
                    ? gates[(size_t) i]->getNormalisableRange()
                            .convertFrom0to1 (gates[(size_t) i]->getValue())
                    : 0.75f;
                auto gb = gateRect (i);
                g.setColour (goaui::bgPanelLo);
                g.fillRoundedRectangle (gb, 1.5f);
                auto gFill = gb.withWidth (juce::jmax (1.5f, gb.getWidth() * gf));
                g.setColour (semi < 0 ? col.withAlpha (0.25f) : goaui::accent.withAlpha (0.85f));
                g.fillRoundedRectangle (gFill, 1.5f);
            }

            // Per-step velocity bar (accent = full bar + bright crown).
            const float v = vels[(size_t) i] != nullptr
                ? vels[(size_t) i]->getValue() : 0.9f;
            auto vb = velRect (i);
            g.setColour (goaui::bgPanelLo);
            g.fillRoundedRectangle (vb, 1.5f);
            auto fill = vb.withTop (vb.getBottom()
                                    - juce::jmax (1.5f, vb.getHeight() * v));
            g.setColour (semi < 0 ? col.withAlpha (0.25f) : col.withAlpha (0.9f));
            g.fillRoundedRectangle (fill, 1.5f);
            if (semi >= 0 && v > 0.95f)
            {
                // Accent crown: 100% velocity steps play louder and hold
                // longer (TB-303 style), so they get a bolder marker.
                g.setColour (goaui::textBright);
                g.fillRect (juce::Rectangle<float> (cell.getX(), cell.getY(),
                                                    cell.getWidth(), 1.5f));
                g.setColour (goaui::accentA.withAlpha (0.9f));
                g.fillRect (juce::Rectangle<float> (cell.getX(), cell.getY() + 1.5f,
                                                    cell.getWidth(), 1.5f));
            }
            if (hoverStep == i)
            {
                g.setColour (goaui::textBright.withAlpha (0.8f));
                g.drawRoundedRectangle (vb, 1.5f, 1.0f);
            }
        }

        if (live && ph >= 0.0f)
        {
            // Active step glow: soft outer halo + bright outline + crown.
            g.setColour (goaui::accent.withAlpha (0.25f));
            g.drawRoundedRectangle (cell.expanded (2.0f), 3.0f, 2.5f);
            g.setColour (goaui::textBright.withAlpha (0.9f));
            g.drawRoundedRectangle (cell, 2.0f, 1.6f);
            g.setColour (goaui::textBright.withAlpha (0.55f));
            g.fillRect (cell.removeFromTop (2.5f));
        }
    }
}

void StepStrip::setVelocity (int step, float v01)
{
    if (step < 0 || step >= 16 || vels[(size_t) step] == nullptr)
        return;
    auto* par = vels[(size_t) step];
    par->beginChangeGesture();
    par->setValueNotifyingHost (par->getNormalisableRange()
                                    .convertTo0to1 (juce::jlimit (0.05f, 1.0f, v01)));
    par->endChangeGesture();
    repaint();
}

void StepStrip::poke (juce::Point<float> pos, bool erase)
{
    for (int i = 0; i < 16; ++i)
    {
        if (cellRect (i).contains (pos))
        {
            if (auto* par = steps[(size_t) i])
            {
                if (kind == gateStrip)
                {
                    patternIdx = 0;   // hand edits leave the preset pattern
                    const bool want = erase ? false : ! (par->getValue() > 0.5f);
                    par->beginChangeGesture();
                    par->setValueNotifyingHost (want ? 1.0f : 0.0f);
                    par->endChangeGesture();
                }
                else
                {
                    const int cur = (int) par->getNormalisableRange()
                            .convertFrom0to1 (par->getValue());
                    const int next = erase ? 0
                        : juce::jlimit (0, 12, (cur + 1) % 13);
                    par->beginChangeGesture();
                    par->setValueNotifyingHost (
                        par->getNormalisableRange().convertTo0to1 ((float) next));
                    par->endChangeGesture();
                }
            }
            repaint();
            return;
        }
    }
}

void StepStrip::cycleAt (juce::Point<float> pos, bool fine)
{
    auto cycle = [fine] (juce::RangedAudioParameter* p, int dir, int n)
    {
        if (p == nullptr)
            return;
        int idx = (int) p->getNormalisableRange().convertFrom0to1 (p->getValue());
        idx = ((idx + dir + n) % n + n) % n;
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getNormalisableRange().convertTo0to1 ((float) idx));
        p->endChangeGesture();
    };

    if (syncCell().contains (pos) && syncPar != nullptr)
        cycle (syncPar, fine ? -1 : 1, 6);
    else if (kind == arpStrip && dirCell().contains (pos) && dirPar != nullptr)
        cycle (dirPar, fine ? -1 : 1, 6);   // UP..CONVERGE + STRUM
    else if (kind == arpStrip && scaleCell().contains (pos) && scalePar != nullptr)
        cycle (scalePar, fine ? -1 : 1, tuning::numScales());
    else if (kind == arpStrip && fillCell().contains (pos))
        fillInKey();
    else if (kind == gateStrip && copyCell().contains (pos))
    {
        copyFromArp();
    }
    else if (kind == gateStrip && patCell().contains (pos))
    {
        applyPattern ((patternIdx + (fine ? -1 : 1) + 7) % 7);
    }
    else if (kind == gateStrip && shapeCell().contains (pos) && shapePar != nullptr)
    {
        cycle (shapePar, fine ? -1 : 1, 4);
    }
    else if (octCell().contains (pos) && octPar != nullptr)
    {
        if (kind == gateStrip)
        {
            const float cur = octPar->getNormalisableRange()
                                  .convertFrom0to1 (octPar->getValue());
            const float next = juce::jlimit (0.0f, 1.0f,
                cur + (fine ? -0.1f : 0.1f));
            octPar->beginChangeGesture();
            octPar->setValueNotifyingHost (octPar->getNormalisableRange()
                                               .convertTo0to1 (next));
            octPar->endChangeGesture();
        }
        else
            cycle (octPar, fine ? -1 : 1, 4);
    }
    else
        poke (pos, fine);
    repaint();
}

void StepStrip::setGate (int step, float frac)
{
    if (step < 0 || step >= 16 || gates[(size_t) step] == nullptr)
        return;
    auto* par = gates[(size_t) step];
    par->beginChangeGesture();
    par->setValueNotifyingHost (par->getNormalisableRange()
                                    .convertTo0to1 (juce::jlimit (0.05f, 1.0f, frac)));
    par->endChangeGesture();
    repaint();
}

void StepStrip::mouseDown (const juce::MouseEvent& e)
{
    const bool right = juce::ModifierKeys::currentModifiers.isRightButtonDown();
    lastCell = -1;
    velDragging = false;
    gateDragging = false;

    if (kind == arpStrip && ! right)
    {
        const int step = stepAt (e.position);
        if (step >= 0 && velRect (step).contains (e.position))
        {
            velDragging = true;
            lastCell = step;
            dragStartY = e.position.y;
            dragStartVel = vels[(size_t) step] != nullptr
                ? vels[(size_t) step]->getValue() : 0.9f;
            return;
        }
        if (step >= 0 && gateRect (step).contains (e.position))
        {
            gateDragging = true;
            lastCell = step;
            dragStartY = e.position.y;
            dragStartGate = gates[(size_t) step] != nullptr
                ? gates[(size_t) step]->getNormalisableRange()
                        .convertFrom0to1 (gates[(size_t) step]->getValue())
                : 0.75f;
            return;
        }
    }

    if (right || syncCell().contains (e.position) || octCell().contains (e.position)
        || (kind == gateStrip && (copyCell().contains (e.position)
                                  || patCell().contains (e.position)
                                  || shapeCell().contains (e.position)))
        || (kind == arpStrip && (dirCell().contains (e.position)
                                 || scaleCell().contains (e.position)
                                 || fillCell().contains (e.position))))
    {
        cycleAt (e.position, right);
    }
    else if (kind == arpStrip && right)
    {
        // Right-click on an arp step toggles a full-velocity accent.
        const int step = stepAt (e.position);
        if (step >= 0)
            setVelocity (step,
                         vels[(size_t) step] != nullptr
                             && vels[(size_t) step]->getValue() > 0.95f ? 0.55f : 1.0f);
    }
    else
    {
        dragToggle = true;
        if (const int step = stepAt (e.position); step >= 0)
            dragToggle = ! (steps[(size_t) step]->getValue() > 0.5f);
        poke (e.position, ! dragToggle);
    }
}

void StepStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (velDragging)
    {
        const float pxPerFull = 60.0f;   // drag distance for a 0..1 sweep
        const float delta = (dragStartY - e.position.y) / pxPerFull;
        setVelocity (lastCell, dragStartVel + delta);
        return;
    }
    if (gateDragging)
    {
        // Horizontal drag for gate length: right = longer, left = shorter.
        const float pxPerFull = 60.0f;
        const float delta = (e.position.x - e.mouseDownPosition.x) / pxPerFull;
        setGate (lastCell, dragStartGate + delta);
        return;
    }

    if (e.position != e.mouseDownPosition && ! syncCell().contains (e.position)
        && ! octCell().contains (e.position))
    {
        const int cell = stepAt (e.position);
        if (cell != lastCell)
        {
            lastCell = cell;
            if (cell >= 0)
                poke (e.position, ! dragToggle);
        }
    }
}

void StepStrip::mouseMove (const juce::MouseEvent& e)
{
    if (kind != arpStrip)
        return;
    const int step = stepAt (e.position);
    const int h = (step >= 0 && velRect (step).contains (e.position)) ? step : -1;
    if (h != hoverStep)
    {
        hoverStep = h;
        setMouseCursor (h >= 0 ? juce::MouseCursor::UpDownResizeCursor
                               : juce::MouseCursor::PointingHandCursor);
        repaint();
    }
}

//==============================================================================
int Keyboard::whiteSemi (int i)
{
    static const int s[7] = { 0, 2, 4, 5, 7, 9, 11 };
    return s[i % 7];
}

int Keyboard::noteForWhite (int i) const
{
    return startNote + (i / 7) * 12 + whiteSemi (i);
}

bool Keyboard::blackAfter (int i) const
{
    if (i >= numWhite - 1)
        return false;
    return whiteSemi (i + 1) - whiteSemi (i) == 2;
}

//==============================================================================
// Header meter: two output level bars (L over R) with DAW-style peak-hold
// ticks. Reads the true per-channel peaks the audio thread publishes.
void LevelMeter::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const float gutterW = 12.0f;
    const float barH = juce::jmax (3.0f, (b.getHeight() - 5.0f) * 0.5f);
    auto lRow  = b.removeFromTop (barH);
    b.removeFromTop (5.0f);
    auto rRow  = b;

    // Tiny channel tags in a left gutter, matching the header's dim text.
    g.setFont (juce::Font (juce::FontOptions (8.0f, juce::Font::bold)));
    g.setColour (goaui::textDim);
    g.drawText ("L", lRow.removeFromLeft (gutterW), juce::Justification::centredRight);
    g.drawText ("R", rRow.removeFromLeft (gutterW), juce::Justification::centredRight);

    auto drawBar = [&] (juce::Rectangle<float> row, float v, float peak, juce::Colour fill)
    {
        auto bar = row;
        g.setColour (goaui::bgInset);
        g.fillRoundedRectangle (bar, 2.0f);
        if (v > 0.002f)
        {
            g.setColour (fill);
            g.fillRoundedRectangle (bar.withWidth (juce::jmax (2.0f, bar.getWidth() * v)), 2.0f);
        }
        // Peak-hold tick: a 1.5 px vertical mark that sits at the recent peak
        // for ~1.2 s, then falls back at ~12 dB/s, exactly how DAWs make
        // transients readable against fast-moving programme material.
        if (peak > 0.004f)
        {
            const float px = juce::jmin (bar.getWidth() - 1.0f, bar.getWidth() * peak);
            g.setColour (goaui::textBright.withAlpha (0.85f));
            g.fillRect (bar.getX() + px - 0.75f, bar.getY(), 1.5f, bar.getHeight());
        }
        g.setColour (goaui::border);
        g.drawRoundedRectangle (bar.reduced (0.5f), 2.0f, 1.0f);
    };

    // Green up to about -6 dBFS, amber to -1, red above: the same convention
    // as every DAW meter, so a glance needs no legend.
    auto levelColour = [] (float v)
    {
        return v > 0.89f ? juce::Colour (0xffe24b4a)
             : v > 0.50f ? juce::Colour (0xffef9f27)
                         : juce::Colour (0xff1d9e75);
    };

    drawBar (lRow, levelL, peakHoldL, levelColour (juce::jmax (levelL, peakHoldL)));
    drawBar (rRow, levelR, peakHoldR, levelColour (juce::jmax (levelR, peakHoldR)));
}

void LevelMeter::timerCallback()
{
    // True per-channel peaks (atomics published by processBlock); the same
    // smoothing as before - fast attack, slow release.
    const float l  = proc.uiPeakL.load (std::memory_order_relaxed);
    const float r  = proc.uiPeakR.load (std::memory_order_relaxed);
    const float newL = juce::jlimit (0.0f, 1.0f, l);
    const float newR = juce::jlimit (0.0f, 1.0f, r);

    auto follow = [] (float& disp, float target)
    {
        const float a = target > disp ? 0.35f : 0.045f;
        disp += a * (target - disp);
    };
    follow (levelL, newL);
    follow (levelR, newR);

    // Peak-hold: grab new peaks instantly; hold 36 frames (~1.2 s at 30 Hz);
    // afterwards decay about 12 dB per second (a factor of ~0.63 per frame).
    auto hold = [] (float& held, int& age, float target)
    {
        if (target >= held || target > 0.002f)
        {
            if (target >= held)
            {
                held = target;
                age = 0;
                return;
            }
        }
        if (++age > 36)
            held *= 0.63f;
        if (held < 0.001f)
            held = 0.0f;
    };
    hold (peakHoldL, peakHoldAgeL, newL);
    hold (peakHoldR, peakHoldAgeR, newR);

    // Repaint only on a visible change: this runs 30x a second forever.
    if (std::abs (newL - levelL) > 0.002f || std::abs (newR - levelR) > 0.002f
        || peakHoldAgeL <= 1 || peakHoldAgeR <= 1)
        repaint();
}

void Keyboard::paint (juce::Graphics& g)
{
    g.fillAll (bgInset);
    const float ww = (float) getWidth() / (float) numWhite;

    for (int i = 0; i < numWhite; ++i)
    {
        auto r = juce::Rectangle<float> (i * ww, 0.0f, ww, (float) getHeight());
        const bool pressed = held == noteForWhite (i);
        g.setColour (pressed ? accent.withAlpha (0.45f) : keyWhite);
        g.fillRect (r.reduced (0.5f, 0.0f));
        if (i % 7 == 0)
        {
            g.setColour (keyDim);
            g.setFont (juce::FontOptions (8.0f));
            static const char* names[7] = { "C", "D", "E", "F", "G", "A", "B" };
            g.drawText (juce::String (names[i % 7]) + juce::String (startNote / 12 - 1),
                        r, juce::Justification::centredBottom);
        }
    }

    for (int i = 0; i < numWhite - 1; ++i)
    {
        if (! blackAfter (i))
            continue;
        const float x = (i + 1) * ww - ww * 0.3f;
        auto r = juce::Rectangle<float> (x, 0.0f, ww * 0.6f, (float) getHeight() * 0.62f);
        g.setColour (held == noteForWhite (i) + 1 ? accent.withAlpha (0.65f) : keyBlack);
        g.fillRect (r);
        g.setColour (border);
        g.drawRect (r, 1.0f);
    }
}

int Keyboard::hitTest (juce::Point<float> pos) const
{
    const float ww = (float) getWidth() / (float) numWhite;
    for (int i = 0; i < numWhite - 1; ++i)
    {
        if (! blackAfter (i))
            continue;
        const float x = (i + 1) * ww - ww * 0.3f;
        if (pos.y < (float) getHeight() * 0.62f && pos.x >= x && pos.x < x + ww * 0.6f)
            return noteForWhite (i) + 1;
    }
    const int i = juce::jlimit (0, numWhite - 1, (int) (pos.x / ww));
    return noteForWhite (i);
}

void Keyboard::press (int note)
{
    if (note == held)
        return;
    release();
    held = note;
    proc.uiNoteOn (held, 0.9f);
    repaint();
}

void Keyboard::release()
{
    if (held >= 0)
        proc.uiNoteOff (held);
    held = -1;
    repaint();
}

void Keyboard::setOctave (int oct)
{
    octave = juce::jlimit (1, 6, oct);
    release();
    startNote = 12 * (octave + 1);
    repaint();
}

//==============================================================================
void GoaLAF::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                               float startAngle, float endAngle, juce::Slider&)
{
    juce::ignoreUnused (startAngle, endAngle);

    // Sweep: minimum = 9 o'clock (fully left), maximum = 3 o'clock (fully right),
    // clockwise over the top. Pointer, value arc and track all share this geometry.
    const float startA = juce::MathConstants<float>::pi;
    const float endA = juce::MathConstants<float>::twoPi;

    const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float rad = juce::jmin (r.getWidth(), r.getHeight()) * 0.42f;
    if (rad < 3.0f)
        return;
    const float cx = r.getCentreX();
    const float cy = r.getCentreY();
    const float p = juce::jlimit (0.0f, 1.0f, pos);

    juce::ColourGradient body (knobTop, cx, cy - rad,
                               knobBottom, cx, cy + rad, false);
    g.setGradientFill (body);
    g.fillEllipse (cx - rad, cy - rad, rad * 2.0f, rad * 2.0f);
    g.setColour (knobRim);
    g.drawEllipse (cx - rad, cy - rad, rad * 2.0f, rad * 2.0f, 1.0f);

    juce::Path track;
    track.addCentredArc (cx, cy, rad + 3.0f, rad + 3.0f, 0.0f, startA, endA, true);
    g.setColour (knobTrack);
    g.strokePath (track, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path val;
    val.addCentredArc (cx, cy, rad + 3.0f, rad + 3.0f, 0.0f, startA,
                       startA + (endA - startA) * p, true);
    g.setColour (accent.withAlpha (0.22f));
    g.strokePath (val, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (accent);
    g.strokePath (val, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float ang = startA + (endA - startA) * p;
    const float sn = std::sin (ang);
    const float cs = std::cos (ang);
    g.setColour (textBright.withAlpha (0.9f));
    g.drawLine (juce::Line<float> (cx + cs * rad * 0.35f, cy + sn * rad * 0.35f,
                                   cx + cs * rad * 0.88f, cy + sn * rad * 0.88f), 1.6f);
    g.fillEllipse (cx + cs * rad * 0.88f - 1.6f, cy + sn * rad * 0.88f - 1.6f, 3.2f, 3.2f);
}

void GoaLAF::drawComboBox (juce::Graphics& g, int w, int h, bool,
                           int, int, int, int, juce::ComboBox& box)
{
    auto b = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (b, 3.0f);
    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (b, 3.0f, 1.0f);

    const float ax = b.getRight() - 11.0f;
    const float cy = b.getCentreY();
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.drawLine ({ ax, cy - 2.2f, ax + 3.5f, cy + 1.8f }, 1.5f);
    g.drawLine ({ ax + 3.5f, cy + 1.8f, ax + 7.0f, cy - 2.2f }, 1.5f);
}

juce::Font GoaLAF::getComboBoxFont (juce::ComboBox&)
{
    return juce::Font (juce::FontOptions (11.0f));
}

void GoaLAF::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& baseIn,
                                   bool highlighted, bool down)
{
    auto base = baseIn;
    auto r = button.getLocalBounds().toFloat().reduced (0.5f);
    if (down)
        base = base.darker (0.30f);
    else if (highlighted)
        base = base.brighter (0.12f);
    if (auto* tb = dynamic_cast<juce::ToggleButton*> (&button);
        tb != nullptr && tb->getToggleState())
        base = base.brighter (0.15f);

    g.setColour (base);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (border.withAlpha (0.9f));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

//==============================================================================
AiOverlay::AiOverlay()
{
    setInterceptsMouseClicks (true, true);
    setAlwaysOnTop (true);

    head.setText ("AI PATCH DESIGNER", juce::dontSendNotification);
    head.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    head.setColour (juce::Label::textColourId, accent);
    head.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (head);

    prompt.setTextToShowWhenEmpty ("describe the sound... e.g. dark rolling acid bass for a night set", textDim);
    prompt.setColour (juce::TextEditor::backgroundColourId, bgInset);
    prompt.setColour (juce::TextEditor::outlineColourId, border);
    prompt.setColour (juce::TextEditor::focusedOutlineColourId, accent);
    prompt.setColour (juce::TextEditor::textColourId, textBright);
    prompt.setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.35f));
    prompt.setFont (juce::Font (juce::FontOptions (13.0f)));
    prompt.setIndents (8, 6);
    prompt.onReturnKey = [this] { goBtn.triggerClick(); };
    addAndMakeVisible (prompt);

    goBtn.setColour (juce::TextButton::buttonColourId, bgPanelLo);
    goBtn.setColour (juce::TextButton::textColourOffId, accent);
    goBtn.onClick = [this]
    {
        if (busy || onGenerate == nullptr)
            return;
        onGenerate (prompt.getText(), varIdx, cloudEngine, modelIdx, keyEditor.getText());
    };
    addAndMakeVisible (goBtn);

    // Variation strength selector: SUBTLE / NORMAL / WILD segmented control.
    varLabel.setText ("VARIATION", juce::dontSendNotification);
    varLabel.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
    varLabel.setColour (juce::Label::textColourId, textDim);
    varLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (varLabel);

    auto wireVar = [this] (juce::TextButton& b, int idx)
    {
        b.setClickingTogglesState (true);
        b.setRadioGroupId (777);
        b.setColour (juce::TextButton::buttonColourId, bgPanelLo);
        b.setColour (juce::TextButton::buttonOnColourId, accent.withAlpha (0.25f));
        b.setColour (juce::TextButton::textColourOffId, textDim);
        b.setColour (juce::TextButton::textColourOnId, textBright);
        b.onClick = [this, idx] { varIdx = idx; };
        addAndMakeVisible (b);
    };
    wireVar (varSubtle, 0);
    wireVar (varNormal, 1);
    wireVar (varWild,   2);
    varNormal.setToggleState (true, juce::dontSendNotification);

    // ---- cloud AI row ------------------------------------------------------
    cloudLabel.setText ("AI ENGINE", juce::dontSendNotification);
    cloudLabel.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
    cloudLabel.setColour (juce::Label::textColourId, textDim);
    cloudLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (cloudLabel);

    engineBtn.setColour (juce::TextButton::buttonColourId, bgPanelLo);
    engineBtn.setColour (juce::TextButton::textColourOffId, accentA);
    engineBtn.setTooltip ("Cycle the AI engine: offline designer / Google Gemini / OpenAI / custom endpoint (Ollama, LM Studio)");
    engineBtn.onClick = [this]
    {
        cloudEngine = (cloudEngine + 1) % 4;
        updateEngineUi();
        saveCloudSettings();
    };
    addAndMakeVisible (engineBtn);

    keyEditor.setTextToShowWhenEmpty ("paste your API key... (or endpoint URL for CUSTOM)", textDim);
    keyEditor.setColour (juce::TextEditor::backgroundColourId, bgInset);
    keyEditor.setColour (juce::TextEditor::outlineColourId, border);
    keyEditor.setColour (juce::TextEditor::focusedOutlineColourId, accent);
    keyEditor.setColour (juce::TextEditor::textColourId, textBright);
    keyEditor.setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.35f));
    keyEditor.setFont (juce::Font (juce::FontOptions (11.0f)));
    keyEditor.setIndents (8, 4);
    addAndMakeVisible (keyEditor);

    keySaveBtn.setColour (juce::TextButton::buttonColourId, bgPanelLo);
    keySaveBtn.setColour (juce::TextButton::textColourOffId, textDim);
    keySaveBtn.setTooltip ("Store the key or endpoint on this computer so it survives restarts");
    keySaveBtn.onClick = [this]
    {
        saveCloudSettings();
        keyStatus.setText (cloudEngine == 0 ? "OFFLINE MODE — no key needed"
                                            : "KEY SAVED on this computer",
                           juce::dontSendNotification);
    };
    addAndMakeVisible (keySaveBtn);

    modelBtn.setColour (juce::TextButton::buttonColourId, bgPanelLo);
    modelBtn.setColour (juce::TextButton::textColourOffId, accentA);
    modelBtn.onClick = [this] { cycleModel(); };
    addAndMakeVisible (modelBtn);

    testBtn.setColour (juce::TextButton::buttonColourId, bgPanelLo);
    testBtn.setColour (juce::TextButton::textColourOffId, accentA);
    testBtn.setTooltip ("Send a small live request to verify the key and model");
    testBtn.onClick = [this] { runTestKey(); };
    addAndMakeVisible (testBtn);

    keyStatus.setFont (juce::Font (juce::FontOptions (9.0f)));
    keyStatus.setColour (juce::Label::textColourId, textDim);
    keyStatus.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (keyStatus);

    // Restore the remembered engine + key, if any.
    loadCloudSettings();
    updateEngineUi();

    closeBtn.setColour (juce::TextButton::buttonColourId, bgPanelLo);
    closeBtn.setColour (juce::TextButton::textColourOffId, textDim);
    closeBtn.onClick = [this] { setVisible (false); };
    addAndMakeVisible (closeBtn);

    result.setText ("TYPE A BRIEF AND PRESS GENERATE \u2014 try \u201cacid bass\u201d, \u201cfull-on lead\u201d, "
                    "\u201cdarkpsy screech\u201d, \u201cwide pad\u201d, \u201claser riser\u201d, \u201cdeep sub\u201d",
                    juce::dontSendNotification);
    result.setFont (juce::Font (juce::FontOptions (10.0f)));
    result.setColour (juce::Label::textColourId, textDim);
    result.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (result);
}

void AiOverlay::paint (juce::Graphics& g)
{
    g.setColour (bgDark.withAlpha (0.90f));
    g.fillAll();

    auto card = getLocalBounds().withSizeKeepingCentre (660, 232).toFloat();
    g.setColour (bgPanel);
    g.fillRoundedRectangle (card, 6.0f);
    g.setColour (border);
    g.drawRoundedRectangle (card.reduced (0.5f), 6.0f, 1.0f);
    g.setGradientFill (uvGradient ({ card.getX() + 10.0f, card.getY(),
                                     card.getWidth() - 20.0f, 2.0f }));
    g.fillRect (card.getX() + 10.0f, card.getY(), card.getWidth() - 20.0f, 2.0f);
}

void AiOverlay::resized()
{
    auto card = cardBounds();
    closeBtn.setBounds (card.removeFromTop (24).removeFromRight (30).reduced (6, 6));
    auto body = card.reduced (16, 8);
    head.setBounds (body.removeFromTop (24));
    body.removeFromTop (4);

    auto row = body.removeFromTop (32);
    goBtn.setBounds (row.removeFromRight (106).reduced (0, 3));
    row.removeFromLeft (8);
    prompt.setBounds (row);
    body.removeFromTop (8);

    auto varRow = body.removeFromTop (22);
    varLabel.setBounds (varRow.removeFromLeft (78));
    varRow.removeFromLeft (8);
    varSubtle.setBounds (varRow.removeFromLeft (74).reduced (0, 1));
    varRow.removeFromLeft (6);
    varNormal.setBounds (varRow.removeFromLeft (74).reduced (0, 1));
    varRow.removeFromLeft (6);
    varWild.setBounds (varRow.removeFromLeft (74).reduced (0, 1));
    body.removeFromTop (8);

    // Cloud AI row: label, engine cycle, credential editor, save, status.
    auto cloudRow = body.removeFromTop (24);
    cloudLabel.setBounds (cloudRow.removeFromLeft (78));
    cloudRow.removeFromLeft (8);
    engineBtn.setBounds (cloudRow.removeFromLeft (150).reduced (0, 2));
    cloudRow.removeFromLeft (6);
    keySaveBtn.setBounds (cloudRow.removeFromRight (88).reduced (0, 2));
    cloudRow.removeFromRight (6);
    testBtn.setBounds (cloudRow.removeFromRight (88).reduced (0, 2));
    cloudRow.removeFromRight (6);
    modelBtn.setBounds (cloudRow.removeFromRight (168).reduced (0, 2));
    cloudRow.removeFromRight (6);
    keyEditor.setBounds (cloudRow);
    body.removeFromTop (4);
    keyStatus.setBounds (body.removeFromTop (14));
    body.removeFromTop (2);
    result.setBounds (body.removeFromTop (20));
}

juce::Rectangle<int> AiOverlay::cardBounds() const
{
    return getLocalBounds().withSizeKeepingCentre (660, 232);
}

void AiOverlay::mouseDown (const juce::MouseEvent& e)
{
    if (! cardBounds().contains (e.getPosition()))
        setVisible (false);
}

void AiOverlay::retint()
{
    head.setColour (juce::Label::textColourId, accent);
    prompt.setColour (juce::TextEditor::backgroundColourId, bgInset);
    prompt.setColour (juce::TextEditor::outlineColourId, border);
    prompt.setColour (juce::TextEditor::focusedOutlineColourId, accent);
    prompt.setColour (juce::TextEditor::textColourId, textBright);
    prompt.setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.35f));
    goBtn.setColour (juce::TextButton::buttonColourId, bgPanelLo);
    goBtn.setColour (juce::TextButton::textColourOffId, accent);
    for (auto* b : { &varSubtle, &varNormal, &varWild })
    {
        b->setColour (juce::TextButton::buttonColourId, bgPanelLo);
        b->setColour (juce::TextButton::buttonOnColourId, accent.withAlpha (0.25f));
        b->setColour (juce::TextButton::textColourOffId, textDim);
        b->setColour (juce::TextButton::textColourOnId, textBright);
    }
    closeBtn.setColour (juce::TextButton::buttonColourId, bgPanelLo);
    closeBtn.setColour (juce::TextButton::textColourOffId, textDim);
    engineBtn.setColour (juce::TextButton::buttonColourId, bgPanelLo);
    engineBtn.setColour (juce::TextButton::textColourOffId, accentA);
    keyEditor.setColour (juce::TextEditor::backgroundColourId, bgInset);
    keyEditor.setColour (juce::TextEditor::outlineColourId, border);
    keyEditor.setColour (juce::TextEditor::focusedOutlineColourId, accent);
    keyEditor.setColour (juce::TextEditor::textColourId, textBright);
    keyEditor.setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.35f));
    keySaveBtn.setColour (juce::TextButton::buttonColourId, bgPanelLo);
    keySaveBtn.setColour (juce::TextButton::textColourOffId, textDim);
    testBtn.setColour (juce::TextButton::buttonColourId, bgPanelLo);
    testBtn.setColour (juce::TextButton::textColourOffId, accentA);
    modelBtn.setColour (juce::TextButton::buttonColourId, bgPanelLo);
    modelBtn.setColour (juce::TextButton::textColourOffId, accentA);
    cloudLabel.setColour (juce::Label::textColourId, textDim);
    keyStatus.setColour (juce::Label::textColourId, textDim);
    result.setColour (juce::Label::textColourId, textDim);
    repaint();
}

void AiOverlay::updateEngineUi()
{
    static constexpr const char* names[] = { "LOCAL", "GEMINI", "OPENAI", "CUSTOM" };
    static const char* hints[] =
    {
        "offline designer - no key or network needed",
        "Google Gemini - key from aistudio.google.com (newest models on MODEL)",
        "OpenAI - paste your API key (newest models on MODEL)",
        "custom OpenAI-compatible endpoint URL, e.g. http://localhost:11434/v1"
    };
    engineBtn.setButtonText (juce::String ("ENGINE: ") + names[cloudEngine % 4]);
    keyEditor.setTextToShowWhenEmpty (cloudEngine == 3 ? "http://localhost:11434/v1"
                                                       : "paste your API key...",
                                      textDim);
    keyStatus.setText (hints[cloudEngine % 4], juce::dontSendNotification);
    keyEditor.setVisible (cloudEngine != 0);
    keySaveBtn.setVisible (cloudEngine != 0);
    testBtn.setVisible (cloudEngine != 0);

    // MODEL button: hidden in LOCAL mode, cycles the newest models per engine.
    using E = AiCloudGen::Engine;
    const auto eng = cloudEngine == 1 ? E::Gemini : cloudEngine == 2 ? E::OpenAI : E::Custom;
    modelBtn.setVisible (cloudEngine != 0);
    if (cloudEngine != 0)
    {
        const auto names2 = AiCloudGen::modelNames (eng);
        const int n = juce::jmax (1, names2.size());
        modelIdx = juce::jlimit (0, n - 1, modelIdx);
        modelBtn.setButtonText ("MODEL: " + (names2.isEmpty() ? juce::String ("SERVER DEFAULT")
                                                              : names2[modelIdx]));
    }
}

void AiOverlay::cycleModel()
{
    using E = AiCloudGen::Engine;
    const auto eng = cloudEngine == 1 ? E::Gemini : cloudEngine == 2 ? E::OpenAI : E::Custom;
    const int n = juce::jmax (1, AiCloudGen::modelNames (eng).size());
    modelIdx = (modelIdx + 1) % n;
    updateEngineUi();
    saveCloudSettings();
}

// TEST KEY: a minimal live round-trip so users can verify their credential
// and the selected model before spending a full generation. Runs on a
// background thread like GENERATE; the result lands via keyStatus + result.
void AiOverlay::runTestKey()
{
    if (busy)
        return;

    using E = AiCloudGen::Engine;
    const auto eng = cloudEngine == 1 ? E::Gemini : cloudEngine == 2 ? E::OpenAI : E::Custom;
    const auto cred = keyEditor.getText();

    if (eng != E::Local && ! AiCloudGen::credentialLooksValid (eng, cred))
    {
        showError ("paste a valid API key (or endpoint URL) first");
        return;
    }

    const auto models = AiCloudGen::modelNames (eng);
    const juce::String model = models.isEmpty() ? juce::String()
                               : models[juce::jlimit (0, models.size() - 1, modelIdx)];

    setBusy (true);
    keyStatus.setText ("TESTING connection...", juce::dontSendNotification);
    result.setText ("", juce::dontSendNotification);

    juce::Component::SafePointer<AiOverlay> safe (this);
    juce::Thread::launch ([safe, eng, cred, model]()
    {
        auto res = AiCloudGen::testConnection (eng, cred, model);
        juce::MessageManager::callAsync ([safe, res]()
        {
            if (safe == nullptr)
                return;
            safe->setBusy (false);
            if (res.ok)
            {
                safe->keyStatus.setText ("TEST PASSED", juce::dontSendNotification);
                safe->result.setText ("OK  \u2022  " + res.description,
                                      juce::dontSendNotification);
                safe->result.setColour (juce::Label::textColourId, accentA);
            }
            else
            {
                safe->showError (res.error);
                safe->result.setText ("TEST FAILED  \u2022  " + res.error.toUpperCase(),
                                      juce::dontSendNotification);
                safe->result.setColour (juce::Label::textColourId, textDim);
            }
        });
    });
}

void AiOverlay::setBusy (bool b)
{
    busy = b;
    goBtn.setEnabled (! b);
    goBtn.setButtonText (b ? "WORKING..." : "GENERATE");
    testBtn.setEnabled (! b);
}

void AiOverlay::showResult (const juce::String& title, const juce::String& desc)
{
    result.setText ((title + "  \u2022  " + desc).trim(), juce::dontSendNotification);
    result.setColour (juce::Label::textColourId, accentA);
}

void AiOverlay::showError (const juce::String& msg)
{
    result.setText (msg.toUpperCase(), juce::dontSendNotification);
    result.setColour (juce::Label::textColourId, textDim);
    keyStatus.setText (msg, juce::dontSendNotification);
}

//==============================================================================
// Cloud settings persistence: engine choice + credential live in
// %APPDATA%\GoaSynth\ai.txt (per-user). The file stores the engine index and
// the credential on one line; a missing file means LOCAL + no key.
//==============================================================================
juce::File cloudSettingsFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("GoaSynth").getChildFile ("ai.txt");
}

void AiOverlay::loadCloudSettings()
{
    const auto f = cloudSettingsFile();
    if (! f.existsAsFile())
        return;
    const auto lines = juce::StringArray::fromLines (f.loadFileAsString());
    if (lines.size() >= 1)
        cloudEngine = juce::jlimit (0, 3, lines[0].trim().getIntValue());
    if (lines.size() >= 2)
        keyEditor.setText (lines[1].trim());
    if (lines.size() >= 3)
        modelIdx = juce::jlimit (0, 15, lines[2].trim().getIntValue());
}

void AiOverlay::saveCloudSettings()
{
    auto f = cloudSettingsFile();
    f.getParentDirectory().createDirectory();
    f.replaceWithText (juce::String (cloudEngine) + "\n"
                       + keyEditor.getText().trim() + "\n"
                       + juce::String (modelIdx) + "\n");
}

//==============================================================================
// Modulation matrix overlay: 8 rows, each SRC cell and DST cell cycles through
// its list on click; the AMT cell drags vertically for a bipolar amount.
//==============================================================================
ModOverlay::ModOverlay (GoaSynthAudioProcessor& p) : proc (p)
{
    setInterceptsMouseClicks (true, true);
    setAlwaysOnTop (true);
    setWantsKeyboardFocus (true);   // so ESC can cancel pick mode / close

    head.setText ("MOD MATRIX \u2014 BANK A", juce::dontSendNotification);
    head.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    head.setColour (juce::Label::textColourId, accent);
    head.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (head);

    // Interaction cheat-sheet along the card's bottom edge (UX audit): every
    // affordance is invisible in a painted matrix, so spell it out. It runs on
    // two lines because the full sheet is ~753px at 9pt and the card only has
    // 528px of inner width - on one line the tail (the ESC hint) was ellipsized
    // away. Splitting keeps every affordance readable.
    for (auto* h : { &hint, &hint2 })
    {
        h->setColour (juce::Label::textColourId, textDim);
        h->setFont (juce::Font (juce::FontOptions (9.0f)));
        h->setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (*h);
    }
    hint.setText ("click SRC/DST to cycle \u00b7 right-click backwards \u00b7 click DST + a knob,"
                  " or SRC + an LFO/ENV graph, to assign",
                  juce::dontSendNotification);
    hint2.setText ("click the amount bar to jump \u00b7 shift-drag = fine \u00b7"
                   " double-click amount = 0 \u00b7 ESC closes",
                   juce::dontSendNotification);

    closeBtn.setColour (juce::TextButton::buttonColourId, bgPanelLo);
    closeBtn.setColour (juce::TextButton::textColourOffId, textDim);
    closeBtn.onClick = [this] { setVisible (false); };
    addAndMakeVisible (closeBtn);
    retint();
}

void ModOverlay::retint()
{
    head.setColour (juce::Label::textColourId, accent);
    hint.setColour (juce::Label::textColourId, textDim);
    hint2.setColour (juce::Label::textColourId, textDim);
    closeBtn.setColour (juce::TextButton::buttonColourId, bgPanelLo);
    closeBtn.setColour (juce::TextButton::textColourOffId, textDim);
    repaint();
}

juce::Rectangle<int> ModOverlay::cardBounds() const
{
    return getLocalBounds().withSizeKeepingCentre (560, 420);
}

// Positions the MOD MATRIX title and the close button. Without this both sit
// at zero size (invisible) — only the painted card showed, and the ✕ did
// nothing. The painted matrix cells live in rowsRect(), below this strip.
void ModOverlay::resized()
{
    auto card = cardBounds();
    // Generous close target: the old 18px ✕ was easy to miss (UX audit).
    closeBtn.setBounds (card.removeFromTop (26).removeFromRight (44).reduced (5, 3));
    head.setBounds (card.reduced (16, 0).removeFromTop (24));

    // Interaction cheat-sheet along the card's bottom edge: two lines, 15px
    // each, which is what rowsRect() reserves at the bottom of the grid.
    auto foot = cardBounds().removeFromBottom (30).reduced (16, 0);
    hint.setBounds (foot.removeFromTop (15));
    hint2.setBounds (foot.removeFromTop (15));
}

juce::Rectangle<float> ModOverlay::rowsRect() const
{
    // Top 40 clears the title strip; the bottom 30 is the two-line cheat-sheet
    // band that resized() gives the hint labels. Without that trim the grid ran
    // to within 4px of the card's bottom edge and the hint text was painted
    // straight over the last row's cells (caught by OverlayTest's
    // painted-geometry audit).
    return cardBounds().toFloat().reduced (16.0f)
               .withTrimmedTop (40.0f)
               .withTrimmedBottom (30.0f);
}

ModOverlay::RowGeo ModOverlay::rowGeo (int row) const
{
    const auto all = rowsRect();
    const float h = all.getHeight() / (float) param::modSlots;
    auto r = all.withTrimmedTop (h * (float) row).withHeight (h)
                .reduced (0.0f, 2.0f);
    RowGeo g;
    g.src = r.removeFromLeft (96.0f).reduced (2.0f);
    r.removeFromLeft (4.0f);
    g.dst = r.removeFromLeft (96.0f).reduced (2.0f);
    r.removeFromLeft (4.0f);
    g.curve = r.removeFromLeft (38.0f).reduced (1.0f);
    r.removeFromLeft (4.0f);
    g.lag = r.removeFromLeft (38.0f).reduced (1.0f);
    r.removeFromLeft (4.0f);
    g.amt = r.reduced (2.0f);
    return g;
}

int ModOverlay::rowAt (juce::Point<float> pos) const
{
    for (int i = 0; i < param::modSlots; ++i)
    {
        const auto g = rowGeo (i);
        if (g.src.contains (pos) || g.dst.contains (pos) || g.amt.contains (pos))
            return i;
    }
    return -1;
}

void ModOverlay::paint (juce::Graphics& g)
{
    g.setColour (bgDark.withAlpha (0.90f));
    g.fillAll();

    auto card = cardBounds().toFloat();
    g.setColour (bgPanel);
    g.fillRoundedRectangle (card, 6.0f);
    g.setColour (border);
    g.drawRoundedRectangle (card.reduced (0.5f), 6.0f, 1.0f);
    g.setGradientFill (uvGradient ({ card.getX() + 10.0f, card.getY(),
                                     card.getWidth() - 20.0f, 2.0f }));
    g.fillRect (card.getX() + 10.0f, card.getY(), card.getWidth() - 20.0f, 2.0f);

    auto labelled = [&g] (juce::Rectangle<float> cell, const juce::String& txt,
                          juce::Colour c, bool filled = true)
    {
        if (filled)
        {
            g.setColour (bgInset);
            g.fillRoundedRectangle (cell, 3.0f);
            g.setColour (border);
            g.drawRoundedRectangle (cell, 3.0f, 1.0f);
        }
        g.setColour (c);
        g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        g.drawText (txt, cell, juce::Justification::centred);
    };

    const auto rows = rowsRect();
    labelled ({ rows.getX(), rows.getY() - 16.0f, 96.0f, 14.0f }, "SOURCE", textDim, false);
    labelled ({ rows.getX() + 100.0f, rows.getY() - 16.0f, 96.0f, 14.0f }, "DEST", textDim, false);
    labelled ({ rows.getX() + 200.0f, rows.getY() - 16.0f, 38.0f, 14.0f }, "CURVE", textDim, false);
    labelled ({ rows.getX() + 242.0f, rows.getY() - 16.0f, 38.0f, 14.0f }, "LAG", textDim, false);
    labelled ({ rows.getX() + 284.0f, rows.getY() - 16.0f,
                rows.getWidth() - 284.0f, 14.0f },
              "AMOUNT  (drag, bipolar)", textDim, false);

    // Pick mode banner: sits between the title strip and the column labels.
    if (pickRow >= 0)
        labelled ({ rows.getX(), rows.getY() - 32.0f, rows.getWidth(), 14.0f },
                  pickSrc ? "PICK A SOURCE: click an LFO scope, ENV graph or MACRO knob \u2014"
                            " same SRC or ESC cancels"
                          : "PICK A KNOB: click any control in the synth to route it here \u2014"
                            " same DST or ESC cancels",
                  accentB, false);

    for (int i = 0; i < param::modSlots; ++i)
    {
        const auto g_ = rowGeo (i);
        auto* src = proc.apvts.getParameter (
            editingBankB ? param::modBSrc (i) : param::modSrc (i));
        auto* dst = proc.apvts.getParameter (
            editingBankB ? param::modBDst (i) : param::modDst (i));
        auto* amt = proc.apvts.getParameter (
            editingBankB ? param::modBAmt (i) : param::modAmt (i));
        auto* cur = proc.apvts.getParameter (param::modCurve (i));
        auto* lag = proc.apvts.getParameter (param::modLag (i));
        if (src == nullptr || dst == nullptr || amt == nullptr
            || cur == nullptr || lag == nullptr)
            continue;

        const int srcIdx = (int) src->getNormalisableRange().convertFrom0to1 (src->getValue());
        const int dstIdx = (int) dst->getNormalisableRange().convertFrom0to1 (dst->getValue());
        const float amtV = amt->getNormalisableRange().convertFrom0to1 (amt->getValue());
        const bool active = srcIdx != 0 && dstIdx != 0;
        const int curIdx = (int) cur->getNormalisableRange().convertFrom0to1 (cur->getValue());
        const float lagV = lag->getNormalisableRange().convertFrom0to1 (lag->getValue());

        labelled (g_.src, param::modSourceName()[(size_t) srcIdx],
                  active ? accentA : textDim);
        labelled (g_.dst, param::modDestList()[(size_t) dstIdx].label,
                  active ? accent : textDim);
        static const char* curveNames[3] = { "LIN", "EXP", "SIN" };
        labelled (g_.curve, curveNames[juce::jlimit (0, 2, curIdx)],
                  curIdx != 0 ? textBright : textDim);
        labelled (g_.lag, lagV > 0.005f
                              ? juce::String (juce::roundToInt (lagV * 100.0f))
                              : juce::String ("\u2013"),
                  lagV > 0.005f ? textBright : textDim);

        // Armed row: outline the cell awaiting a click assignment.
        if (pickRow == i)
        {
            g.setColour (accentB);
            g.drawRoundedRectangle ((pickSrc ? g_.src : g_.dst).expanded (2.0f), 4.0f, 2.0f);
        }

        // Bipolar amount bar: centre notch = 0, fill goes left/right.
        auto bar = g_.amt;
        g.setColour (bgInset);
        g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (border);
        g.drawRoundedRectangle (bar, 3.0f, 1.0f);
        const float midX = bar.getCentreX();
        g.setColour (border);
        g.fillRect (midX - 0.5f, bar.getY() + 2.0f, 1.0f, bar.getHeight() - 4.0f);
        auto fill = bar.reduced (2.0f);
        if (amtV >= 0.0f)
            fill = fill.withLeft (midX).withRight (midX + (fill.getWidth() * amtV * 0.5f));
        else
            fill = fill.withRight (midX).withLeft (midX + (fill.getWidth() * amtV * 0.5f));
        g.setColour (active ? accent.withAlpha (0.75f) : textDim.withAlpha (0.3f));
        if (std::abs (amtV) > 0.003f)
            g.fillRoundedRectangle (fill, 2.0f);
        g.setColour (active ? textBright : textDim);
        g.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
        g.drawText (juce::String (juce::roundToInt (amtV * 100.0f)), bar,
                    juce::Justification::centred);

        // Hover feedback: the whole row brightens (paint must mirror the
        // hover state tracked in mouseMove, or hovering looks like nothing).
        if (hoverRow == i)
        {
            g.setColour (accent.withAlpha (0.10f));
            g.fillRoundedRectangle (rowGeo (i).src.getUnion (rowGeo (i).amt), 3.0f);
        }
    }

    // Just-assigned confirm: a fading fill on the cell that took the routing,
    // plus a brief pointer from that cell toward the control it landed on —
    // the knob itself is dimmed under this overlay's backdrop, so the card
    // carries the confirmation.
    //
    // Drawn AFTER the rows, deliberately: this used to run inside the flashing
    // row's own pass, so every row listed below it filled its cells straight
    // over the pointer — most of the line was buried under the rows in between
    // and the arrow visibly pointed at nothing.
    if (cellFlashRow >= 0 && cellFlashRow < param::modSlots
        && juce::Time::getMillisecondCounter() < cellFlashUntil)
    {
        const float fade = (float) (cellFlashUntil
            - juce::Time::getMillisecondCounter()) / 700.0f;
        const auto g_ = rowGeo (cellFlashRow);
        const auto cell = (cellFlashSrc ? g_.src : g_.dst);

        flashPointerLine = {};   // no pointer unless a target is known

        g.setColour (accentB.withAlpha (0.55f * fade));
        g.fillRoundedRectangle (cell, 3.0f);
        g.setColour (accentB.withAlpha (0.9f * fade));
        g.drawRoundedRectangle (cell.expanded (1.0f), 4.0f, 2.0f);

        if (! cellFlashTarget.isOrigin())
        {
            const auto a = cell.getCentre();
            const auto b = cellFlashTarget;
            const juce::Point<float> dir = b - a;
            const float len = dir.getDistanceFromOrigin();
            if (len > 1.0f)
            {
                const auto u = dir / len;
                const float headLen = 6.0f;   // arrowhead length, px
                // Stop short of the target's centre: the tip points AT the
                // knob instead of covering it.
                const juce::Point<float> tip  = b - u * 10.0f;
                const juce::Point<float> base = a + u * (cell.getWidth() * 0.5f + 3.0f);
                flashPointerLine = juce::Line<float> (base, tip);   // for OverlayTest
                g.setColour (accentB.withAlpha (0.7f * fade));
                g.drawLine (juce::Line<float> (base, tip - u * headLen), 4.0f);
                // Arrowhead: two strokes angled back from the tip.
                const float ca = std::cos (juce::degreesToRadians (30.0f));
                const float sa = std::sin (juce::degreesToRadians (30.0f));
                const juce::Point<float> wing1 (u.x * ca - u.y * sa,  u.x * sa + u.y * ca);
                const juce::Point<float> wing2 (u.x * ca + u.y * sa, -u.x * sa + u.y * ca);
                g.drawLine (juce::Line<float> (tip, tip - wing1 * headLen), 1.5f);
                g.drawLine (juce::Line<float> (tip, tip - wing2 * headLen), 1.5f);
                g.setColour (accentB.withAlpha (0.85f * fade));
                g.fillEllipse (tip.x - 1.5f, tip.y - 1.5f, 3.0f, 3.0f);
            }
        }
    }
}

void ModOverlay::mouseDown (const juce::MouseEvent& e)
{
    // BANK A/B tabs: click the card header to switch which bank the card
    // edits (the engine reads the live modBank switch in the bottom bar —
    // editing here never changes what the engine runs until you flip it).
    const auto card = cardBounds();
    if (e.position.y < (float) card.getY() + 26.0f)
    {
        const bool wantB = e.position.x > (float) card.getCentreX();
        if (editingBankB != wantB)
        {
            editingBankB = wantB;
            head.setText (editingBankB ? "MOD MATRIX \u2014 BANK B"
                                       : "MOD MATRIX \u2014 BANK A",
                          juce::dontSendNotification);
            repaintCtlPaints (getParentComponent());   // dots read the edited bank
            repaint();
        }
        return;
    }

    if (! cardBounds().contains (e.getPosition()))
    {
        // Outside the card while picking: the click "passes through" to the
        // synth UI, so resolve it against the editor's controls and assign.
        if (pickRow >= 0)
        {
            if (pickSrc)
                tryAssignSource (e.position, e.mods.isRightButtonDown());
            else
                tryAssignDestination (e.position, e.mods.isRightButtonDown());
        }
        else
            setVisible (false);
        return;
    }
    updateCursor (e.position);
    dragRow = rowAt (e.position);

    if (dragRow < 0)
        return;
    const auto g_ = rowGeo (dragRow);
    if (g_.amt.contains (e.position))
    {
        auto* amt = proc.apvts.getParameter (
            editingBankB ? param::modBAmt (dragRow) : param::modAmt (dragRow));
        if (amt == nullptr) { dragRow = -1; return; }
        cancelPick();   // interacting with the row ends a pending pick
        if (e.mods.isShiftDown())
        {
            // Fine mode: relative drag, 4x coarser than the normal sweep.
            dragStartAmt = amt->getNormalisableRange().convertFrom0to1 (amt->getValue());
            dragStartY = e.position.y;
        }
        else
        {
            // Plain click jumps the amount straight to the click point, so a
            // drag starts where the thumb appears (no dead catch-up travel).
            const float v = amountFromClickX (g_.amt, e.position.x);
            amt->beginChangeGesture();
            amt->setValueNotifyingHost (amt->getNormalisableRange().convertTo0to1 (v));
            amt->endChangeGesture();
            dragStartAmt = v;
            dragStartY = e.position.y;
        }
        repaint();
        return;
    }
    dragRow = -1;

    if (pickRow >= 0)
    {
        // Armed cell again: cancel. Other cell kind: switch what is picked.
        // Right-click always keeps the classic cycle behaviour.
        if (e.mods.isRightButtonDown())
        {
            cycleAtCell (e.position);
        }
        else if (( pickSrc && g_.src.contains (e.position))
                 || (! pickSrc && g_.dst.contains (e.position)))
        {
            cancelPick();
        }
        else if (pickSrc && g_.dst.contains (e.position))
        {
            armPick (dragRow);        // switch to destination pick
        }
        else if (! pickSrc && g_.src.contains (e.position))
        {
            armPickSource (dragRow);  // switch to source pick
        }
        else
        {
            cycleAtCell (e.position);
        }
    }
    else
    {
        cycleAtCell (e.position);   // click = cycle src/dst
    }
}

// Double-click resets instead of forcing users to hunt the neutral value:
// amount -> 0, SRC/DST -> OFF (the first list entry in both).
void ModOverlay::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (pickRow >= 0 || ! cardBounds().contains (e.getPosition()))
        return;
    const int row = rowAt (e.position);
    if (row < 0)
        return;
    const auto g_ = rowGeo (row);
    if (g_.amt.contains (e.position))
    {
        if (auto* amt = proc.apvts.getParameter (
                editingBankB ? param::modBAmt (row) : param::modAmt (row)))
        {
            amt->beginChangeGesture();
            amt->setValueNotifyingHost (amt->getNormalisableRange().convertTo0to1 (0.0f));
            amt->endChangeGesture();
        }
    }
    else if (g_.src.contains (e.position))
        setSlotIndex (row, true, 0);    // OFF
    else if (g_.dst.contains (e.position))
        setSlotIndex (row, false, 0);   // OFF
    repaint();
}

void ModOverlay::cycleAtCell (juce::Point<float> pos)
{
    const int row = rowAt (pos);
    if (row < 0)
        return;
    const auto g_ = rowGeo (row);
    const bool fine = juce::ModifierKeys::currentModifiers.isRightButtonDown();
    auto cycle = [fine] (juce::RangedAudioParameter* p, int n)
    {
        if (p == nullptr)
            return;
        int idx = (int) p->getNormalisableRange().convertFrom0to1 (p->getValue());
        idx = ((idx + (fine ? -1 : 1) + n) % n + n) % n;
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getNormalisableRange().convertTo0to1 ((float) idx));
        p->endChangeGesture();
    };
    if (g_.src.contains (pos))
        cycle (proc.apvts.getParameter (
            editingBankB ? param::modBSrc (row) : param::modSrc (row)),
               param::modSourceName().size());
    else if (g_.dst.contains (pos))
        cycle (proc.apvts.getParameter (
            editingBankB ? param::modBDst (row) : param::modDst (row)),
               (int) param::modDestList().size());
    else if (g_.curve.contains (pos))
        cycle (proc.apvts.getParameter (param::modCurve (row)), 3);
    else if (g_.lag.contains (pos))
    {
        // Lag cycles through sensible presets rather than a free sweep:
        // 0, 25, 50, 75, 100 %.
        auto* p = proc.apvts.getParameter (param::modLag (row));
        if (p != nullptr)
        {
            const float v = p->getNormalisableRange().convertFrom0to1 (p->getValue());
            static constexpr float steps[] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
            int idx = 0;
            for (int s = 0; s < 5; ++s)
                if (v <= steps[s] + 0.01f) { idx = s; break; }
            const float next = steps[(idx + (fine ? -1 : 1) + 5) % 5];
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->getNormalisableRange().convertTo0to1 (next));
            p->endChangeGesture();
        }
    }
    repaint();
}

void ModOverlay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragRow < 0)
        return;
    auto* amt = proc.apvts.getParameter (
        editingBankB ? param::modBAmt (dragRow) : param::modAmt (dragRow));
    if (amt == nullptr)
        return;
    // Shift = fine: 560 px for the full sweep instead of 140 (queried live,
    // so the modifier can be toggled mid-drag).
    const float px = e.mods.isShiftDown() ? 560.0f : 140.0f;
    const float delta = (dragStartY - e.position.y) / px;
    const float v = juce::jlimit (-1.0f, 1.0f, dragStartAmt + delta);
    amt->beginChangeGesture();
    amt->setValueNotifyingHost (amt->getNormalisableRange().convertTo0to1 (v));
    amt->endChangeGesture();
    repaint();
}

void ModOverlay::mouseUp (const juce::MouseEvent&) { dragRow = -1; }

void ModOverlay::mouseMove (const juce::MouseEvent& e)
{
    const int h = rowAt (e.position);
    if (h != hoverRow) { hoverRow = h; repaint(); }
    updateCursor (e.position);
}

void ModOverlay::mouseExit (const juce::MouseEvent&)
{
    if (hoverRow != -1) { hoverRow = -1; repaint(); }
}

//==============================================================================
// Pick modes: a DST cell arms destination-pick (click any knob/toggle in the
// synth UI to route it), a SRC cell arms source-pick (click an LFO scope or
// envelope graph). The overlay resolves the control under the click
// geometrically; clicks inside the card behave normally, so picking can
// never drag a knob by accident.
void ModOverlay::armPick (int row)
{
    pickRow = juce::jlimit (-1, param::modSlots - 1, row);
    pickSrc = false;
    repaint();
}

void ModOverlay::armPickSource (int row)
{
    pickRow = juce::jlimit (-1, param::modSlots - 1, row);
    pickSrc = true;
    repaint();
}

void ModOverlay::cancelPick()
{
    if (pickRow >= 0)
    {
        pickRow = -1;
        pickSrc = false;
        repaint();
    }
}

void ModOverlay::setDestIndex (int row, int index) { setSlotIndex (row, false, index); }

void ModOverlay::setSlotIndex (int row, bool src, int index)
{
    auto* p = proc.apvts.getParameter (
        src ? (editingBankB ? param::modBSrc (row) : param::modSrc (row))
            : (editingBankB ? param::modBDst (row) : param::modDst (row)));
    if (p == nullptr)
        return;
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->getNormalisableRange().convertTo0to1 ((float) index));
    p->endChangeGesture();
    repaint();
    repaintCtlPaints (getParentComponent());   // dot targets may have changed
}

// Amount value for a click on the bar: the centre notch is 0, each half
// spans -1..+1 (matching the bipolar paint, 2 px edge padding).
float ModOverlay::amountFromClickX (juce::Rectangle<float> bar, float x) const
{
    const float half = (bar.getWidth() - 4.0f) * 0.5f;
    if (half <= 0.0f)
        return 0.0f;
    return juce::jlimit (-1.0f, 1.0f, (x - bar.getCentreX()) / half);
}

// Hover cursors: pointing hand over the cycle/assign cells, up-down resize
// over the amount bars — the painted cells give no other affordance.
void ModOverlay::updateCursor (juce::Point<float> pos)
{
    const int row = cardBounds().contains (pos.toInt()) ? rowAt (pos) : -1;
    if (row < 0)
    {
        setMouseCursor (juce::MouseCursor::NormalCursor);
        return;
    }
    setMouseCursor (rowGeo (row).amt.contains (pos)
                        ? juce::MouseCursor::UpDownResizeCursor
                        : juce::MouseCursor::PointingHandCursor);
}

bool ModOverlay::tryAssignDestination (juce::Point<float> pos, bool rightButton)
{
    if (pickRow < 0 || pickSrc)
        return false;
    const int row = pickRow;
    cancelPick();
    if (rightButton)
        return false;   // right-click outside the card just cancels

    auto* hit = hitControlAt (pos);
    while (hit != nullptr)
    {
        Ctl*       ctl = dynamic_cast<Ctl*>       (hit);
        ToggleCtl* tg  = dynamic_cast<ToggleCtl*> (hit);
        const juce::String* id = nullptr;
        if (ctl != nullptr)      id = &ctl->paramId;
        else if (tg != nullptr)  id = &tg->paramId;
        // MACRO A/B knobs are SOURCES, not destinations: picking with a SRC
        // cell armed and clicking a macro knob routes the macro into the row.
        if (pickSrc && ctl != nullptr && id != nullptr
            && (*id == param::macroA || *id == param::macroB))
        {
            setSlotIndex (row, true, *id == param::macroA ? 9 : 10);
            ctl->triggerFlash();
            cellFlashRow = row; cellFlashSrc = true;
            cellFlashUntil = juce::Time::getMillisecondCounter() + 700;
            flashPointerLine = {};
            cellFlashTarget = targetPointFor (hit);
            repaintCtlPaints (getParentComponent());
            return true;
        }
        if (id != nullptr)
        {
            if (id->isEmpty())
                return false;   // routable control type without a param
            const auto& list = param::modDestList();
            for (int i = 1; i < (int) list.size(); ++i)   // 0 is OFF
                if (*id == list[(size_t) i].param)
                {
                    setDestIndex (row, i);
                    if (ctl != nullptr) ctl->triggerFlash();
                    if (tg != nullptr)  tg->triggerFlash();
                    cellFlashRow = row; cellFlashSrc = false;   // visible confirm
                    cellFlashUntil = juce::Time::getMillisecondCounter() + 700;
                    flashPointerLine = {};                       // this flash owns the pointer
                    cellFlashTarget = targetPointFor (hit);   // pointer line in the flash
                    repaintCtlPaints (getParentComponent());   // dots changed
                    return true;   // consumed: the synth must not react
                }
            return false;   // clicked control's param is not a destination
        }
        hit = hit->getParentComponent();   // nested sliders/combos climb to their Ctl
    }
    return false;
}

bool ModOverlay::tryAssignSource (juce::Point<float> pos, bool rightButton)
{
    if (pickRow < 0 || ! pickSrc)
        return false;
    const int row = pickRow;
    cancelPick();
    if (rightButton)
        return false;   // right-click outside the card just cancels

    auto* hit = hitControlAt (pos);
    while (hit != nullptr)
    {
        if (auto* g = dynamic_cast<LfoGraph*> (hit))
        {
            // LFO 1 / LFO 2 by their rate parameter id.
            const int idx = g->rateParamId == param::lfo1Rate ? 1
                          : g->rateParamId == param::lfo2Rate ? 2 : 0;
            if (idx != 0)
            {
                setSlotIndex (row, true, idx);
                g->triggerFlash();
                cellFlashRow = row; cellFlashSrc = true;
                cellFlashUntil = juce::Time::getMillisecondCounter() + 700;
                flashPointerLine = {};                       // this flash owns the pointer
                cellFlashTarget = targetPointFor (hit);
                repaintCtlPaints (getParentComponent());   // dots changed
                return true;
            }
            return false;   // a third LFO graph would not be routable
        }
        if (auto* e = dynamic_cast<EnvGraph*> (hit))
        {
            // ENV A edits ampA, ENV F edits filtA (see attackParamId()).
            setSlotIndex (row, true,
                          e->attackParamId() == param::ampA ? 4 : 3);
            e->triggerFlash();                cellFlashRow = row; cellFlashSrc = true;
                cellFlashUntil = juce::Time::getMillisecondCounter() + 700;
                flashPointerLine = {};                       // this flash owns the pointer
                cellFlashTarget = targetPointFor (hit);
                repaintCtlPaints (getParentComponent());
                return true;
        }
        hit = hit->getParentComponent();
    }
    return false;   // clicked nothing source-like: pick ends, no change
}

bool ModOverlay::keyPressed (const juce::KeyPress& key)
{
    if (key.getKeyCode() == juce::KeyPress::escapeKey)
    {
        if (pickRow >= 0)
            cancelPick();
        else
            setVisible (false);
        return true;
    }
    return false;
}

// Shared topmost-hit resolve for both pick modes: a plain getComponentAt on
// the editor just finds this overlay (it covers everything), so walk the
// editor's tree ourselves, skipping this overlay's subtree.
juce::Component* ModOverlay::hitControlAt (juce::Point<float> pos)
{
    auto* editor = getParentComponent();
    if (editor == nullptr)
        return nullptr;

    juce::Component* hit = nullptr;
    std::function<void (juce::Component*, juce::Point<int>)> search =
        [&] (juce::Component* parent, juce::Point<int> p)
    {
        for (int i = parent->getNumChildComponents(); --i >= 0;)   // topmost first
        {
            auto* c = parent->getChildComponent (i);
            if (c == this || c == nullptr || ! c->isVisible())
                continue;                                   // skip the overlay itself
            if (! c->getBoundsInParent().contains (p))
                continue;
            hit = c;
            search (c, p - c->getPosition());               // descend into the child
            return;
        }
    };
    search (editor, pos.toInt());
    return hit;
}

// Centre of the clicked control, mapped into this overlay's coordinates for
// the flash pointer line. The overlay covers the whole editor and is resized
// to its bounds, so local coordinates here are already editor coordinates —
// one getLocalPoint from the editor converts the target's centre (hit may sit
// anywhere in the tree, so getBoundsInParent alone is only editor-relative
// for the editor's immediate children).
juce::Line<float> ModOverlay::flashPointer() const noexcept
{
    // Only the live flash has a pointer: after the 700 ms confirmation the
    // stored segment is stale, and reporting it would let a reader (the test)
    // scan a line nobody is drawing any more.
    if (juce::Time::getMillisecondCounter() >= cellFlashUntil)
        return {};

    return flashPointerLine;
}

juce::Point<float> ModOverlay::targetPointFor (juce::Component* target) const
{
    auto* editor = getParentComponent();
    if (editor == nullptr || target == nullptr)
        return {};
    return editor->getLocalPoint (target, target->getLocalBounds().getCentre()).toFloat();
}

//==============================================================================
// Activation screen: shown in place of the synth UI while unlicensed.
LicenseOverlay::LicenseOverlay()
{
    setInterceptsMouseClicks (true, true);
    setAlwaysOnTop (true);
    dismissible = false;

    // Trial "peek" close: invisible in forced mode (expired / no trial).
    closeBtn.onClick = [this] { setVisible (false); };
    addAndMakeVisible (closeBtn);
    closeBtn.setVisible (false);

    setHeadline (false);   // default: never activated; showLicenseOverlay() adds "TRIAL EXPIRED"
    head.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    head.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (head);

    machineLabel.setText ("YOUR MACHINE ID \u2014 send this to the seller to receive your serial",
                          juce::dontSendNotification);
    machineLabel.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
    machineLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (machineLabel);

    machineId.setFont (juce::Font (juce::FontOptions (19.0f, juce::Font::bold)));
    machineId.setText (goa::License::machineId(), juce::dontSendNotification);
    machineId.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (machineId);

    copyBtn.onClick = [this]
    {
        juce::SystemClipboard::copyTextToClipboard (goa::License::machineId());
        copyBtn.setButtonText ("COPIED");
    };
    addAndMakeVisible (copyBtn);

    serial.setTextToShowWhenEmpty ("paste your serial key", textDim);
    serial.setFont (juce::Font (juce::FontOptions (13.0f)));
    serial.setIndents (10, 7);
    serial.onReturnKey = [this] { activateBtn.triggerClick(); };
    addAndMakeVisible (serial);

    activateBtn.onClick = [this]
    {
        juce::String err;
        if (goa::License::activate (serial.getText(), err))
        {
            error.setText ("", juce::dontSendNotification);
            if (onActivated != nullptr)
                onActivated();
        }
        else
        {
            error.setText (err.toUpperCase(), juce::dontSendNotification);
        }
    };
    addAndMakeVisible (activateBtn);

    // .goalicense import: pick the file, then run it through the normal
    // activation path (the file is just a courier for the signed serial).
    chooseFileBtn.onClick = [this]
    {
        if (fileChooser != nullptr)
            return;

        auto chooser = std::make_shared<juce::FileChooser> ("Open license file",
                                                            juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                                            "*." + juce::String (goa::License::fileExtension));
        fileChooser = chooser;

        juce::Component::SafePointer<LicenseOverlay> safeThis { this };
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [safeThis, chooser] (const juce::FileChooser& fc)
        {
            if (safeThis == nullptr)
                return;
            safeThis->fileChooser = nullptr;
            safeThis->importLicenseFile (fc.getResult());
        });
    };
    addAndMakeVisible (chooseFileBtn);

    fileNote.setFont (juce::Font (juce::FontOptions (10.0f)));
    fileNote.setJustificationType (juce::Justification::centred);
    fileNote.setColour (juce::Label::textColourId, textDim);
    addAndMakeVisible (fileNote);

    error.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    error.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (error);

    hint.setText ("ONE SERIAL = ONE COMPUTER \u2014 or IMPORT the .goalicense file the seller sent you",
                  juce::dontSendNotification);
    hint.setFont (juce::Font (juce::FontOptions (9.0f)));
    hint.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (hint);
}

void LicenseOverlay::setHeadline (bool trialExpired)
{
    head.setText (juce::String ("GOASYNTH \u2014 ")
                      + (trialExpired ? "TRIAL EXPIRED \u2014 " : "") + "ACTIVATION REQUIRED",
                  juce::dontSendNotification);
}

void LicenseOverlay::paint (juce::Graphics& g)
{
    g.setColour (bgDark.withAlpha (0.96f));
    g.fillAll();

    auto card = cardBounds().toFloat();
    g.setColour (bgPanel);
    g.fillRoundedRectangle (card, 6.0f);
    g.setColour (border);
    g.drawRoundedRectangle (card.reduced (0.5f), 6.0f, 1.0f);
    g.setGradientFill (uvGradient ({ card.getX() + 10.0f, card.getY(),
                                     card.getWidth() - 20.0f, 2.0f }));
    g.fillRect (card.getX() + 10.0f, card.getY(), card.getWidth() - 20.0f, 2.0f);
}

void LicenseOverlay::resized()
{
    // Peek-mode × sits in the window's top-right corner, clear of the card.
    closeBtn.setBounds (getLocalBounds().removeFromTop (28).removeFromRight (34).reduced (5));

    auto card = cardBounds();
    card.reduce (18, 12);
    head.setBounds (card.removeFromTop (28));
    machineLabel.setBounds (card.removeFromTop (16));

    auto idRow = card.removeFromTop (32);
    copyBtn.setBounds (idRow.removeFromRight (74).reduced (5, 5));
    machineId.setBounds (idRow);
    card.removeFromTop (10);

    serial.setBounds (card.removeFromTop (32));
    card.removeFromTop (6);

    // Activation pair, centred: primary action + the file import beside it.
    auto btnRow = card.removeFromTop (30);
    auto pair = btnRow.withSizeKeepingCentre (250, 30);
    activateBtn.setBounds (pair.removeFromLeft (150).reduced (0, 2));
    pair.removeFromLeft (8);
    chooseFileBtn.setBounds (pair.removeFromLeft (92).reduced (6, 5));
    card.removeFromTop (5);

    fileNote.setBounds (card.removeFromTop (30));
    error.setBounds (card.removeFromTop (30));
    card.removeFromTop (2);
    hint.setBounds (card);
}

juce::Rectangle<int> LicenseOverlay::cardBounds() const
{
    return getLocalBounds().withSizeKeepingCentre (560, 292);
}

void LicenseOverlay::importLicenseFile (const juce::File& f)
{
    juce::String err;
    if (goa::License::activateFile (f, err))
    {
        error.setText ("", juce::dontSendNotification);
        if (onActivated != nullptr)
            onActivated();
        return;
    }

    // Show why it failed; when the file at least looks like a GoaSynth
    // license, also surface its note/issued lines for context.
    fileNote.setText ({}, juce::dontSendNotification);

    if (f.existsAsFile())
    {
        juce::StringArray kept;
        for (const auto& line : juce::StringArray::fromLines (f.loadFileAsString()))
        {
            const juce::String t = line.trim();
            if (t.startsWithIgnoreCase ("note:") || t.startsWithIgnoreCase ("issued:"))
                kept.add (t);
        }
        if (! kept.isEmpty())
            fileNote.setText (kept.joinIntoString ("   "), juce::dontSendNotification);
    }

    error.setText (err.toUpperCase(), juce::dontSendNotification);
}

void LicenseOverlay::retint()
{
    head.setColour (juce::Label::textColourId, accent);
    machineLabel.setColour (juce::Label::textColourId, textDim);
    machineId.setColour (juce::Label::textColourId, textBright);
    serial.setColour (juce::TextEditor::backgroundColourId, bgInset);
    serial.setColour (juce::TextEditor::outlineColourId, border);
    serial.setColour (juce::TextEditor::focusedOutlineColourId, accent);
    serial.setColour (juce::TextEditor::textColourId, textBright);
    serial.setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.35f));
    activateBtn.setColour (juce::TextButton::buttonColourId, bgPanelLo);
    activateBtn.setColour (juce::TextButton::textColourOffId, accent);
    chooseFileBtn.setColour (juce::TextButton::buttonColourId, bgPanelLo);
    chooseFileBtn.setColour (juce::TextButton::textColourOffId, textDim);
    fileNote.setColour (juce::Label::textColourId, textDim);
    closeBtn.setColour (juce::TextButton::buttonColourId, bgPanelLo);
    closeBtn.setColour (juce::TextButton::textColourOffId, textDim);
    copyBtn.setColour (juce::TextButton::buttonColourId, bgPanelLo);
    copyBtn.setColour (juce::TextButton::textColourOffId, textDim);
    error.setColour (juce::Label::textColourId, juce::Colour (0xffe05a6a));
    hint.setColour (juce::Label::textColourId, textDim);
}

//==============================================================================
SavePresetOverlay::SavePresetOverlay()
{
    setInterceptsMouseClicks (true, true);
    setAlwaysOnTop (true);

    head.setText ("SAVE USER PRESET", juce::dontSendNotification);
    head.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    head.setColour (juce::Label::textColourId, accentA);
    head.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (head);

    name.setTextToShowWhenEmpty ("preset name... e.g. NIGHT ACID REWORK", textDim);
    name.setColour (juce::TextEditor::backgroundColourId, bgInset);
    name.setColour (juce::TextEditor::outlineColourId, border);
    name.setColour (juce::TextEditor::focusedOutlineColourId, accentA);
    name.setColour (juce::TextEditor::textColourId, textBright);
    name.setColour (juce::TextEditor::highlightColourId, accentA.withAlpha (0.35f));
    name.setFont (juce::Font (juce::FontOptions (13.0f)));
    name.setIndents (8, 6);
    name.onReturnKey = [this] { saveBtn.triggerClick(); };
    addAndMakeVisible (name);

    tags.setTextToShowWhenEmpty ("tags... e.g. acid bass night (space separated)", textDim);
    tags.setColour (juce::TextEditor::backgroundColourId, bgInset);
    tags.setColour (juce::TextEditor::outlineColourId, border);
    tags.setColour (juce::TextEditor::focusedOutlineColourId, accentA);
    tags.setColour (juce::TextEditor::textColourId, textBright);
    tags.setColour (juce::TextEditor::highlightColourId, accentA.withAlpha (0.35f));
    tags.setFont (juce::Font (juce::FontOptions (11.0f)));
    tags.setIndents (8, 6);
    tags.onReturnKey = [this] { saveBtn.triggerClick(); };
    addAndMakeVisible (tags);

    sharedBtn.setButtonText ("SHARED (all users on this PC)");
    sharedBtn.setColour (juce::ToggleButton::textColourId, textDim);
    sharedBtn.onClick = [this]
    {
        sharedBtn.setColour (juce::ToggleButton::textColourId,
                             sharedBtn.getToggleState() ? accentA : textDim);
    };
    addAndMakeVisible (sharedBtn);

    saveBtn.setColour (juce::TextButton::buttonColourId, bgPanelLo);
    saveBtn.setColour (juce::TextButton::textColourOffId, accentA);
    saveBtn.onClick = [this]
    {
        if (renaming)
        {
            if (onRename != nullptr)
                onRename (name.getText(), collectTags());
        }
        else if (onSave != nullptr)
        {
            onSave (name.getText(), collectTags(), sharedBtn.getToggleState());
        }
    };
    addAndMakeVisible (saveBtn);

    cancelBtn.setColour (juce::TextButton::buttonColourId, bgPanelLo);
    cancelBtn.setColour (juce::TextButton::textColourOffId, textDim);
    cancelBtn.onClick = [this] { setVisible (false); };
    addAndMakeVisible (cancelBtn);
    retint();
}

// Duplicate: a normal save, pre-filled with the source name/tags.
void SavePresetOverlay::beginSaveAs (const juce::String& presetName,
                                     const juce::StringArray& presetTags)
{
    renaming = false;
    head.setText ("SAVE USER PRESET", juce::dontSendNotification);
    head.setColour (juce::Label::textColourId, accentA);
    saveBtn.setButtonText ("SAVE");
    saveBtn.setTooltip ("Save preset");
    sharedBtn.setVisible (true);
    name.setText (presetName, juce::dontSendNotification);
    tags.setText (presetTags.joinIntoString (" "), juce::dontSendNotification);
    name.selectAll();
}

// Rename: same fields, but SAVE rewrites the existing file under a new name
// instead of creating a second patch. The shared toggle is hidden because a
// rename never moves a preset between banks.
void SavePresetOverlay::beginRename (const juce::String& presetName,
                                     const juce::StringArray& presetTags)
{
    renaming = true;
    head.setText ("RENAME PRESET", juce::dontSendNotification);
    head.setColour (juce::Label::textColourId, accent);
    saveBtn.setButtonText ("RENAME");
    saveBtn.setTooltip ("Rename this preset");
    sharedBtn.setVisible (false);
    name.setText (presetName, juce::dontSendNotification);
    tags.setText (presetTags.joinIntoString (" "), juce::dontSendNotification);
    name.selectAll();
}

void SavePresetOverlay::retint()
{
    head.setColour (juce::Label::textColourId, renaming ? accent : accentA);
    name.setColour (juce::TextEditor::backgroundColourId, bgInset);
    name.setColour (juce::TextEditor::outlineColourId, border);
    name.setColour (juce::TextEditor::focusedOutlineColourId, accentA);
    name.setColour (juce::TextEditor::textColourId, textBright);
    name.setColour (juce::TextEditor::highlightColourId, accentA.withAlpha (0.35f));
    tags.setColour (juce::TextEditor::backgroundColourId, bgInset);
    tags.setColour (juce::TextEditor::outlineColourId, border);
    tags.setColour (juce::TextEditor::focusedOutlineColourId, accentA);
    tags.setColour (juce::TextEditor::textColourId, textBright);
    tags.setColour (juce::TextEditor::highlightColourId, accentA.withAlpha (0.35f));
    sharedBtn.setColour (juce::ToggleButton::textColourId, textDim);
    saveBtn.setColour (juce::TextButton::buttonColourId, bgPanelLo);
    saveBtn.setColour (juce::TextButton::textColourOffId, accentA);
    cancelBtn.setColour (juce::TextButton::buttonColourId, bgPanelLo);
    cancelBtn.setColour (juce::TextButton::textColourOffId, textDim);
    repaint();
}

void SavePresetOverlay::paint (juce::Graphics& g)
{
    g.setColour (bgDark.withAlpha (0.90f));
    g.fillAll();

    auto card = cardBounds().toFloat();
    g.setColour (bgPanel);
    g.fillRoundedRectangle (card, 6.0f);
    g.setColour (border);
    g.drawRoundedRectangle (card.reduced (0.5f), 6.0f, 1.0f);
    g.setGradientFill (uvGradient ({ card.getX() + 10.0f, card.getY(),
                                     card.getWidth() - 20.0f, 2.0f }));
    g.fillRect (card.getX() + 10.0f, card.getY(), card.getWidth() - 20.0f, 2.0f);
}

void SavePresetOverlay::resized()
{
    auto card = cardBounds();
    auto body = card.reduced (16, 10);
    head.setBounds (body.removeFromTop (28));
    body.removeFromTop (6);

    auto row = body.removeFromTop (32);
    saveBtn.setBounds (row.removeFromRight (86).reduced (0, 3));
    cancelBtn.setBounds (row.removeFromRight (86).reduced (0, 3));
    row.removeFromRight (8);
    name.setBounds (row);

    body.removeFromTop (5);
    tags.setBounds (body.removeFromTop (26));
    body.removeFromTop (5);
    sharedBtn.setBounds (body.removeFromTop (20));
}

juce::Rectangle<int> SavePresetOverlay::cardBounds() const
{
    return getLocalBounds().withSizeKeepingCentre (620, 165);
}

juce::StringArray SavePresetOverlay::collectTags() const
{
    juce::StringArray out;
    out.addTokens (tags.getText().toLowerCase(), " ,;", "");
    out.removeEmptyStrings();
    return out;
}

void SavePresetOverlay::mouseDown (const juce::MouseEvent& e)
{
    if (! cardBounds().contains (e.getPosition()))
        setVisible (false);
}

//==============================================================================
// Serum-style preset browser overlay.
//==============================================================================
PresetBrowserOverlay::PresetBrowserOverlay()
{
    setInterceptsMouseClicks (true, true);
    setAlwaysOnTop (true);

    head.setText ("PRESET BROWSER", juce::dontSendNotification);
    head.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    head.setColour (juce::Label::textColourId, accent);
    head.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (head);

    closeBtn.setColour (juce::TextButton::buttonColourId, bgPanelLo);
    closeBtn.setColour (juce::TextButton::textColourOffId, textDim);
    closeBtn.onClick = [this] { setVisible (false); };
    addAndMakeVisible (closeBtn);

    search.setTextToShowWhenEmpty ("search presets...", textDim);
    search.setColour (juce::TextEditor::backgroundColourId, bgInset);
    search.setColour (juce::TextEditor::outlineColourId, border);
    search.setColour (juce::TextEditor::focusedOutlineColourId, accent);
    search.setColour (juce::TextEditor::textColourId, textBright);
    search.setFont (juce::Font (juce::FontOptions (12.0f)));
    search.setIndents (8, 5);
    search.onTextChange = [this] { if (onChanged != nullptr) onChanged(); };
    // TextEditor consumes ESC and Return by default (consumeEscAndReturnKeys),
    // so they arrive as these callbacks rather than bubbling to keyPressed().
    // ESC closes without rolling back - whatever is loaded stays loaded.
    search.onEscapeKey = [this] { setVisible (false); };
    search.onReturnKey = [this] { acceptRow (list.getSelectedRow()); };
    addAndMakeVisible (search);

    tagCombo.setColour (juce::ComboBox::backgroundColourId, bgPanel);
    tagCombo.setColour (juce::ComboBox::textColourId, accentA);
    tagCombo.setColour (juce::ComboBox::arrowColourId, accentA);
    tagCombo.setColour (juce::ComboBox::outlineColourId, border);
    tagCombo.setTextWhenNothingSelected ("TAG: ALL");
    tagCombo.onChange = [this] { if (onChanged != nullptr) onChanged(); };
    addAndMakeVisible (tagCombo);

    // Sort. The user bank had no defined order at all before this (it came out
    // of findChildFiles, i.e. filesystem order), so A-Z is the default and
    // "BANK ORDER" is there for anyone who wants the old grouping back.
    sortCombo.addItem ("SORT: A-Z", 1);
    sortCombo.addItem ("SORT: Z-A", 2);
    sortCombo.addItem ("SORT: NEWEST", 3);
    sortCombo.addItem ("SORT: BANK ORDER", 4);
    sortCombo.setSelectedItemIndex (0, juce::dontSendNotification);
    sortCombo.setTooltip ("Sort the list");
    sortCombo.onChange = [this]
    {
        sortIdx = juce::jmax (0, sortCombo.getSelectedItemIndex());
        if (onChanged != nullptr)
            onChanged();
    };
    addAndMakeVisible (sortCombo);

    for (auto* tab : { &allTab, &factoryTab, &userTab, &favTab })
    {
        tab->setColour (juce::TextButton::buttonColourId, bgPanel);
        tab->setColour (juce::TextButton::textColourOffId, textDim);
        addAndMakeVisible (tab);
    }
    allTab.onClick     = [this] { setFolder (0); };
    factoryTab.onClick = [this] { setFolder (1); };
    userTab.onClick    = [this] { setFolder (2); };
    favTab.onClick     = [this] { setFolder (3); };

    for (auto* btn : { &exportBtn, &importBtn })
    {
        btn->setColour (juce::TextButton::buttonColourId, bgPanelLo);
        btn->setColour (juce::TextButton::textColourOffId, accentB);
        addAndMakeVisible (*btn);
        btn->onClick = [this, btn]
        {
            if (btn == &exportBtn)  { if (onExport != nullptr) onExport(); }
            else                    { if (onImport != nullptr) onImport(); }
        };
    }

    status.setFont (juce::Font (juce::FontOptions (10.0f)));
    status.setJustificationType (juce::Justification::centredLeft);
    status.setColour (juce::Label::textColourId, textDim);
    addAndMakeVisible (status);

    // Preset count. A separate label from `status` so a pack import/export
    // message never hides how many patches the current filter matched.
    count.setFont (juce::Font (juce::FontOptions (10.0f)));
    count.setJustificationType (juce::Justification::centredRight);
    count.setColour (juce::Label::textColourId, textDim);
    addAndMakeVisible (count);

    list.setColour (juce::ListBox::backgroundColourId, bgInset);
    list.setColour (juce::ListBox::outlineColourId, border);
    list.setModel (this);
    list.setRowHeight (22);
    list.setMultipleSelectionEnabled (false);
    // Arrow keys must reach the list (and the overlay) rather than being eaten
    // by the search box: TextEditor does not consume up/down, so they bubble.
    list.setWantsKeyboardFocus (true);
    addAndMakeVisible (list);

    retint();
    syncTabs();
}

void PresetBrowserOverlay::retint()
{
    head.setColour (juce::Label::textColourId, accent);
    closeBtn.setColour (juce::TextButton::buttonColourId, bgPanelLo);
    closeBtn.setColour (juce::TextButton::textColourOffId, textDim);
    search.setColour (juce::TextEditor::backgroundColourId, bgInset);
    search.setColour (juce::TextEditor::outlineColourId, border);
    search.setColour (juce::TextEditor::focusedOutlineColourId, accent);
    search.setColour (juce::TextEditor::textColourId, textBright);
    for (auto* cb : { &tagCombo, &sortCombo })
    {
        cb->setColour (juce::ComboBox::backgroundColourId, bgPanel);
        cb->setColour (juce::ComboBox::textColourId, accentA);
        cb->setColour (juce::ComboBox::arrowColourId, accentA);
        cb->setColour (juce::ComboBox::outlineColourId, border);
    }
    for (auto* tab : { &allTab, &factoryTab, &userTab, &favTab })
    {
        tab->setColour (juce::TextButton::buttonColourId, bgPanel);
        tab->setColour (juce::TextButton::textColourOffId, textDim);
    }
    for (auto* btn : { &exportBtn, &importBtn })
    {
        btn->setColour (juce::TextButton::buttonColourId, bgPanelLo);
        btn->setColour (juce::TextButton::textColourOffId, accentB);
    }
    status.setColour (juce::Label::textColourId, textDim);
    count.setColour (juce::Label::textColourId, textDim);
    list.setColour (juce::ListBox::backgroundColourId, bgInset);
    list.setColour (juce::ListBox::outlineColourId, border);
    syncTabs();
    list.repaint();
    repaint();
}

void PresetBrowserOverlay::syncTabs()
{
    const int active = folderIdx;
    allTab.setColour     (juce::TextButton::textColourOffId, active == 0 ? accent : textDim);
    factoryTab.setColour (juce::TextButton::textColourOffId, active == 1 ? accent : textDim);
    userTab.setColour    (juce::TextButton::textColourOffId, active == 2 ? accent : textDim);
    // The star tab is gold when active: it is the one tab that filters on a
    // user-owned attribute rather than where the patch came from.
    favTab.setColour     (juce::TextButton::textColourOffId,
                          active == 3 ? juce::Colour (0xffe8c25a) : textDim);
}

void PresetBrowserOverlay::setFolder (int f)
{
    folderIdx = juce::jlimit (0, 3, f);
    syncTabs();
    if (onChanged != nullptr)
        onChanged();
}

void PresetBrowserOverlay::setSort (int mode)
{
    sortIdx = juce::jlimit (0, 3, mode);
    sortCombo.setSelectedItemIndex (sortIdx, juce::dontSendNotification);
}

void PresetBrowserOverlay::setSearchText (const juce::String& s)
{
    search.setText (s, juce::dontSendNotification);
}

void PresetBrowserOverlay::setTagText (const juce::String& s)
{
    if (s.isEmpty())
    {
        tagCombo.setSelectedItemIndex (0, juce::dontSendNotification);
        return;
    }
    for (int i = 0; i < tagCombo.getNumItems(); ++i)
        if (tagCombo.getItemText (i).equalsIgnoreCase (s))
        {
            tagCombo.setSelectedItemIndex (i, juce::dontSendNotification);
            return;
        }
    tagCombo.setSelectedItemIndex (0, juce::dontSendNotification);
}

void PresetBrowserOverlay::focusSearch()
{
    search.grabKeyboardFocus();
    search.selectAll();
}

// Restore a persisted filter in one go, without firing onChanged per field (the
// caller does a single rebuildPresetList afterwards).
void PresetBrowserOverlay::applyState (int f, int sort, const juce::String& tag,
                                       const juce::String& searchText)
{
    folderIdx = juce::jlimit (0, 3, f);
    setSort (sort);
    setTagText (tag);
    setSearchText (searchText);
    syncTabs();
}

void PresetBrowserOverlay::setCount (int shown, int total)
{
    if (total <= 0)
        count.setText ({}, juce::dontSendNotification);
    else if (shown == total)
        count.setText (juce::String (total) + (total == 1 ? " preset" : " presets"),
                       juce::dontSendNotification);
    else
        count.setText (juce::String (shown) + " of " + juce::String (total), 
                       juce::dontSendNotification);
    count.setColour (juce::Label::textColourId, shown == 0 ? accentB : textDim);
}

juce::Rectangle<int> PresetBrowserOverlay::cardBounds() const
{
    return getLocalBounds().withSizeKeepingCentre (720, 560);
}

int PresetBrowserOverlay::getNumRows() { return (int) rows.size(); }

void PresetBrowserOverlay::paint (juce::Graphics& g)
{
    g.setColour (bgDark.withAlpha (0.90f));
    g.fillAll();

    auto card = cardBounds().toFloat();
    g.setColour (bgPanel);
    g.fillRoundedRectangle (card, 6.0f);
    g.setColour (border);
    g.drawRoundedRectangle (card.reduced (0.5f), 6.0f, 1.0f);
    g.setGradientFill (uvGradient ({ card.getX() + 10.0f, card.getY(),
                                     card.getWidth() - 20.0f, 2.0f }));
    g.fillRect (card.getX() + 10.0f, card.getY(), card.getWidth() - 20.0f, 2.0f);

    // Empty state. A ListBox with no rows paints nothing at all, so a filter
    // that matches nothing looked identical to a browser that had failed to
    // load. Say so instead, in the middle of the list area.
    if (rows.empty())
    {
        auto empty = list.getBounds().toFloat();
        g.setColour (textDim.withAlpha (0.85f));
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText (search.getText().trim().isEmpty()
                        ? "No presets here yet."
                        : "Nothing matches \u201c" + search.getText().trim() + "\u201d",
                    empty, juce::Justification::centred);
        g.setColour (textDim.withAlpha (0.6f));
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawText ("Clear the search, or pick another tag / folder.",
                    empty.translated (0.0f, 18.0f), juce::Justification::centred);
    }
}

void PresetBrowserOverlay::resized()
{
    auto card = cardBounds();
    auto body = card.reduced (14, 10);

    auto top = body.removeFromTop (30);
    head.setBounds (top.removeFromLeft (180));
    closeBtn.setBounds (top.removeFromRight (30).reduced (4, 2));

    // search | TAG | SORT | count
    auto filters = body.removeFromTop (28);
    count.setBounds (filters.removeFromRight (104).reduced (0, 3));
    filters.removeFromRight (6);
    sortCombo.setBounds (filters.removeFromRight (118).reduced (0, 3));
    filters.removeFromRight (6);
    tagCombo.setBounds (filters.removeFromRight (110).reduced (0, 3));
    filters.removeFromRight (6);
    search.setBounds (filters.reduced (0, 3));

    // tabs ALL | FACTORY | USER | star, then IMPORT / EXPORT
    auto tabs = body.removeFromTop (26);
    exportBtn.setBounds (tabs.removeFromRight (112).reduced (0, 2));
    tabs.removeFromRight (6);
    importBtn.setBounds (tabs.removeFromRight (122).reduced (0, 2));
    tabs.removeFromRight (10);
    favTab.setBounds (tabs.removeFromRight (40).reduced (0, 2));
    tabs.removeFromRight (6);
    const int tw = (tabs.getWidth() - 12) / 3;
    allTab.setBounds (tabs.removeFromLeft (tw).reduced (0, 2));
    tabs.removeFromLeft (6);
    factoryTab.setBounds (tabs.removeFromLeft (tw).reduced (0, 2));
    tabs.removeFromLeft (6);
    userTab.setBounds (tabs.reduced (0, 2));

    auto bottom = body.removeFromBottom (18);
    status.setBounds (bottom);

    body.removeFromTop (6);
    list.setBounds (body);
}

void PresetBrowserOverlay::mouseDown (const juce::MouseEvent& e)
{
    if (! cardBounds().contains (e.getPosition()))
        setVisible (false);
}

void PresetBrowserOverlay::setRows (const std::vector<Row>& r, int sel)
{
    // updateContent() moves the ListBox selection around, which would otherwise
    // arrive in selectedRowsChanged() as "the user arrowed onto a row" and load
    // a preset on every refresh (including on each keystroke in the search box).
    const juce::ScopedValueSetter<bool> guard (internalUpdate, true);
    rows = r;
    selectedRow = sel;
    list.updateContent();
    if (juce::isPositiveAndBelow (selectedRow, getNumRows()))
        list.scrollToEnsureRowIsOnscreen (selectedRow);
    else
        list.deselectAllRows();
}

// Audition: load it, but stay open so patches can be flipped through.
void PresetBrowserOverlay::previewRow (int row)
{
    if (! juce::isPositiveAndBelow (row, (int) rows.size()) || rows[(size_t) row].header)
        return;
    selectedRow = row;
    list.repaint();
    if (onSelect != nullptr)
        onSelect (rows[(size_t) row].visibleIndex);
}

// Commit: load it and get out of the way.
void PresetBrowserOverlay::acceptRow (int row)
{
    if (! juce::isPositiveAndBelow (row, (int) rows.size()) || rows[(size_t) row].header)
        return;
    const int visIdx = rows[(size_t) row].visibleIndex;
    if (onAccept != nullptr)
        onAccept (visIdx);
    else if (onSelect != nullptr)
        onSelect (visIdx);
    setVisible (false);
}

void PresetBrowserOverlay::setStatus (const juce::String& text, bool ok)
{
    status.setText (text, juce::dontSendNotification);
    status.setColour (juce::Label::textColourId, ok ? accentA : accentB);
}

void PresetBrowserOverlay::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel)
{
    if (! juce::isPositiveAndBelow (row, (int) rows.size()))
        return;
    const auto& r = rows[(size_t) row];

    if (r.header)
    {
        g.setColour (bgPanel);
        g.fillRect (0, 0, w, h);
        g.setColour (border.withAlpha (0.6f));
        g.drawHorizontalLine (h - 1, 0.0f, (float) w);
        g.setColour (r.isUser ? accentA : accent);
        g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        g.drawText (r.label, 8, 0, w - 16, h, juce::Justification::centredLeft);
        return;
    }

    // `sel` is the list cursor (mouse or arrow keys); r.selected is the patch
    // actually loaded. They differ as soon as you arrow away from what you are
    // hearing, so both are drawn - otherwise auditioning loses your place.
    if (sel)
    {
        g.setColour ((r.isUser ? accentA : accent).withAlpha (0.22f));
        g.fillRect (0, 0, w, h);
    }
    else if (row % 2 == 1)
    {
        g.setColour (bgPanel.withAlpha (0.45f));
        g.fillRect (0, 0, w, h);
    }
    else
    {
        g.setColour (bgInset);
        g.fillRect (0, 0, w, h);
    }

    if (r.selected)   // loaded marker: accent bar down the left edge
    {
        g.setColour (r.isUser ? accentA : accent);
        g.fillRect (0, 0, 3, h);
    }

    // Star column. Clicking it toggles the favourite (see listBoxItemClicked).
    g.setColour (r.fav ? juce::Colour (0xffe8c25a) : textDim.withAlpha (0.5f));
    g.setFont (juce::Font (juce::FontOptions (13.0f)));
    g.drawText (starGlyph (r.fav), juce::Rectangle<int> (6, 0, 20, h),
                juce::Justification::centred);

    g.setColour (r.selected ? textBright : (r.isUser ? accentA : textBright).withAlpha (0.92f));
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    const int nameX = 32;
    g.drawText (r.label, nameX, 0, juce::jmax (40, w - nameX - 196), h,
                juce::Justification::centredLeft);

    if (r.shared)
    {
        // Machine-wide shared bank: teal pill badge so users can tell which
        // patches come from C:\Users\Public\Documents\GoaSynth.
        auto b = juce::Rectangle<int> (w - 190, (h - 13) / 2, 52, 13);
        g.setColour (accentA.withAlpha (0.14f));
        g.fillRoundedRectangle (b.toFloat(), 5.0f);
        g.setColour (accentA.withAlpha (0.55f));
        g.drawRoundedRectangle (b.toFloat(), 5.0f, 1.0f);
        g.setColour (accentA);
        g.setFont (juce::Font (juce::FontOptions (8.0f, juce::Font::bold)));
        g.drawText ("SHARED", b, juce::Justification::centred);
    }

    // Tags: as many as fit, then "+N" for the remainder. This used to draw the
    // first three into a fixed 100px box, so a fourth tag was silently invisible
    // and there was no way to discover it at all.
    if (! r.tags.isEmpty())
    {
        const juce::Font tf (juce::FontOptions (9.0f));
        const int tagW = tagColumnWidth();
        juce::String text;
        for (int n = r.tags.size(); n >= 1; --n)
        {
            juce::StringArray head;
            for (int k = 0; k < n; ++k)
                head.add (r.tags[k].toUpperCase());
            juce::String s = head.joinIntoString (" ");
            const int hidden = r.tags.size() - n;
            if (hidden > 0)
                s << " +" << hidden;
            if (tf.getStringWidth (s) <= (float) tagW)
            {
                text = s;
                break;
            }
        }
        if (text.isNotEmpty())
        {
            g.setColour (textDim.withAlpha (0.75f));
            g.setFont (tf);
            g.drawText (text, w - tagW - 10, 0, tagW, h, juce::Justification::centredRight);
        }
    }
}

void PresetBrowserOverlay::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (! juce::isPositiveAndBelow (row, (int) rows.size()) || rows[(size_t) row].header)
        return;

    const int visIdx = rows[(size_t) row].visibleIndex;

    // Right-click: the editor owns preset policy (rename / delete / reveal), so
    // it builds the menu; the overlay just reports the gesture and where.
    if (e.mods.isPopupMenu())
    {
        if (onRowMenu != nullptr)
            onRowMenu (visIdx, e.getScreenPosition());
        return;
    }

    if (e.x < 28)   // star column
    {
        if (onFavourite != nullptr)
            onFavourite (visIdx);
        return;
    }

    if (e.x >= list.getWidth() - tagColumnWidth() - 12 && ! rows[(size_t) row].tags.isEmpty())
    {
        if (onTagClick != nullptr)
            onTagClick (visIdx);
        return;
    }

    previewRow (row);   // audition; the browser stays open
}

void PresetBrowserOverlay::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    acceptRow (row);
}

void PresetBrowserOverlay::returnKeyPressed (int row)
{
    acceptRow (row);
}

void PresetBrowserOverlay::selectedRowsChanged (int row)
{
    // Ignore the churn setRows() causes, or every refresh would load a preset.
    if (internalUpdate)
        return;
    if (row >= 0)
        previewRow (row);   // arrowing onto a row auditions it
}

juce::String PresetBrowserOverlay::getTooltipForRow (int row)
{
    if (! juce::isPositiveAndBelow (row, (int) rows.size()))
        return {};
    const auto& r = rows[(size_t) row];
    if (r.header)
        return {};

    juce::String t = r.label;
    if (! r.tags.isEmpty())
        t << "\ntags: " << r.tags.joinIntoString (", ");
    t << (r.isUser ? "\nuser preset" : "\nfactory preset");
    if (r.shared)
        t << " (shared bank)";
    if (r.fav)
        t << "\nfavourite";
    t << "\nclick to audition \u00b7 double-click to load and close"
         "\nright-click for rename / delete / export";
    return t;
}

bool PresetBrowserOverlay::keyPressed (const juce::KeyPress& key)
{
    if (key.isKeyCode (juce::KeyPress::escapeKey))
    {
        // Close without rolling back: whatever is loaded stays loaded, so ESC
        // can never lose the patch you just picked.
        setVisible (false);
        return true;
    }

    if (key.isKeyCode (juce::KeyPress::returnKey))
    {
        acceptRow (list.getSelectedRow());
        return true;
    }

    // Up/Down are NOT consumed by the search TextEditor (it maps neither), so
    // they bubble up to here while the search box still has focus - which is
    // what makes type-then-arrow browsing work.
    const bool down = key.isKeyCode (juce::KeyPress::downKey);
    if (down || key.isKeyCode (juce::KeyPress::upKey))
    {
        const int n = getNumRows();
        if (n <= 0)
            return true;

        int r = list.getSelectedRow();
        r = r < 0 ? (down ? 0 : n - 1) : juce::jlimit (0, n - 1, r + (down ? 1 : -1));
        while (juce::isPositiveAndBelow (r, n) && rows[(size_t) r].header)
            r += down ? 1 : -1;
        if (juce::isPositiveAndBelow (r, n))
            list.selectRow (r, false, true);   // auditions via selectedRowsChanged
        return true;
    }

    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'F')
    {
        focusSearch();
        return true;
    }

    return false;
}

} // namespace goaui

//==============================================================================
GoaSynthAudioProcessorEditor::GoaSynthAudioProcessorEditor (GoaSynthAudioProcessor& processor)
    : AudioProcessorEditor (&processor),
      proc (processor),
      waveA (proc, 0, param::osc1Wave, param::osc1WtPos, goaui::roleA),
      waveB (proc, 1, param::osc2Wave, param::osc2WtPos, goaui::roleB),
      filtCurve (proc.apvts, goaui::roleAccent),
      ampGraph (proc.apvts, param::ampA, param::ampD, param::ampS, param::ampR, goaui::roleA),
      filGraph (proc.apvts, param::filtA, param::filtD, param::filtS, param::filtR, goaui::roleAccent),
      lfo1Graph (proc.apvts, param::lfo1Rate, param::lfo1Wave, goaui::roleAccent),
      lfo2Graph (proc.apvts, param::lfo2Rate, param::lfo2Wave, goaui::roleB),
      keyboard (processor)
{
    setLookAndFeel (&laf);
    setOpaque (true);
    auto& apvts = proc.apvts;

    // Sessions restore the skin they closed with (setStateInformation sets
    // themeForNextEditor before the host creates the editor).
    goaui::setTheme (goaui::themeForNextEditor, nullptr);

    // Hover tips for the big interactive displays (knobs/toggles get theirs
    // from their parameter names inside Ctl/ToggleCtl).
    waveA.setTooltip ("OSC A wavetable editor \u2014 left-drag draws, right-drag erases,"
                      " double-click resets; click the strip to pick a frame");
    waveB.setTooltip ("OSC B wavetable editor \u2014 left-drag draws, right-drag erases,"
                      " double-click resets; click the strip to pick a frame");
    keyboard.setTooltip ("Click or drag to play \u2014 notes feed the synth directly");

    // ---- header ----
    addAndMakeVisible (logoGoa);   // UV-gradient logo (draws itself)

    logoSynth.setText ("SYNTH", juce::dontSendNotification);
    logoSynth.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
    logoSynth.setColour (juce::Label::textColourId, goaui::textBright);
    logoSynth.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (logoSynth);

    tagline.setText ("GOA TRANCE SYNTHESIZER", juce::dontSendNotification);
    tagline.setFont (juce::Font (juce::FontOptions (8.0f)));
    tagline.setColour (juce::Label::textColourId, goaui::textDim);
    tagline.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (tagline);

    // Current-patch name display; the browser lives in an overlay.
    presetName.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    presetName.setColour (juce::Label::textColourId, goaui::accent);
    presetName.setJustificationType (juce::Justification::centred);
    presetName.setText ("Init Patch", juce::dontSendNotification);
    addAndMakeVisible (presetName);

    browseBtn.setColour (juce::TextButton::buttonColourId, goaui::bgPanel);
    browseBtn.setColour (juce::TextButton::textColourOffId, goaui::accent);
    browseBtn.setTooltip ("Open the preset browser (search, tags, folders)");
    addAndMakeVisible (browseBtn);
    browseBtn.onClick = [this]
    {
        if (presetBrowser == nullptr)
            return;
        if (! presetBrowser->isVisible())
        {
            presetBrowser->setBounds (getLocalBounds());
            rebuildPresetList();
            presetBrowser->setStatus ("", true);
            presetBrowser->setVisible (true);
            presetBrowser->focusSearch();   // type-to-filter without reaching for the mouse
        }
        else
        {
            persistBrowserState();          // remember the filter for next time
            presetBrowser->setVisible (false);
        }
    };

    presetBrowser = std::make_unique<goaui::PresetBrowserOverlay>();
    presetBrowser->setVisible (false);
    // Audition: load and stay open. The overlay closes itself on accept.
    presetBrowser->onSelect = [this] (int visIdx) { applyPreset (visIdx); };
    presetBrowser->onAccept = [this] (int visIdx) { applyPreset (visIdx); };
    presetBrowser->onChanged = [this]
    {
        rebuildPresetList();
        persistBrowserState();
    };
    presetBrowser->onExport = [this] { exportPresetPack(); };
    presetBrowser->onImport = [this] { importPresetPack(); };
    presetBrowser->onFavourite = [this] (int visIdx) { toggleFavourite (visIdx); };
    presetBrowser->onTagClick = [this] (int visIdx)
    {
        if (presetBrowser == nullptr
            || ! juce::isPositiveAndBelow (visIdx, (int) visiblePresets.size()))
            return;
        const auto& tags = visiblePresets[(size_t) visIdx].tags;
        if (tags.isEmpty())
            return;
        presetBrowser->setTagText (tags[0]);   // the leftmost tag drawn on the row
        rebuildPresetList();
        persistBrowserState();
    };
    presetBrowser->onRowMenu = [this] (int visIdx, juce::Point<int> pos)
    {
        showRowMenu (visIdx, pos);
    };
    addChildComponent (*presetBrowser);

    // Favourites + last filter, restored before the first build so the browser
    // opens exactly where it was left.
    loadBrowserState();
    refreshUserPresets();
    updatePresetTint();

    for (auto* btn : { &prevBtn, &nextBtn })
    {
        btn->setColour (juce::TextButton::buttonColourId, goaui::bgPanel);
        btn->setColour (juce::TextButton::textColourOffId, goaui::textDim);
        addAndMakeVisible (*btn);
    }
    prevBtn.onClick = [this] { cyclePreset (-1); };
    nextBtn.onClick = [this] { cyclePreset (1); };

    panicBtn.setColour (juce::TextButton::buttonColourId, goaui::bgPanel);
    panicBtn.setColour (juce::TextButton::textColourOffId, goaui::accentB);
    addAndMakeVisible (panicBtn);
    panicBtn.onClick = [this] { proc.panic(); };

    aiBtn.setColour (juce::TextButton::buttonColourId, goaui::bgPanelLo);
    aiBtn.setColour (juce::TextButton::textColourOffId, goaui::accentA);
    aiBtn.setTooltip ("AI patch designer \u2014 describe a Goa sound and generate it");
    addAndMakeVisible (aiBtn);
    aiBtn.onClick = [this]
    {
        if (aiOverlay == nullptr)
            return;
        const bool show = ! aiOverlay->isVisible();
        if (show)
            aiOverlay->setBounds (getLocalBounds());
        aiOverlay->setVisible (show);
    };

    modBtn.setColour (juce::TextButton::buttonColourId, goaui::bgPanel);
    modBtn.setColour (juce::TextButton::textColourOffId, goaui::accentA);
    modBtn.setTooltip ("Open the modulation matrix (8 routable slots)");
    addAndMakeVisible (modBtn);
    modBtn.onClick = [this]
    {
        if (modOverlay == nullptr)
            return;
        const bool show = ! modOverlay->isVisible();
        if (show)
        {
            modOverlay->setBounds (getLocalBounds());
            modOverlay->cancelPick();   // never reopen mid-pick
        }
        modOverlay->setVisible (show);
        if (show)
            goaui::repaintCtlPaints (this);   // refresh knob dots (presets may have changed)
    };

    modOverlay = std::make_unique<goaui::ModOverlay> (proc);
    modOverlay->setVisible (false);
    addChildComponent (*modOverlay);   // without this the MOD button shows nothing

    themeBtn.setColour (juce::TextButton::buttonColourId, goaui::bgPanel);
    themeBtn.setColour (juce::TextButton::textColourOffId, goaui::accentB);
    themeBtn.setTooltip ("Pick a theme: UV Goa / steel / warm analog / neon / paper / OLED");
    addAndMakeVisible (themeBtn);
    themeBtn.onClick = [this]
    {
        // Dropdown, not cycling: all themes visible at once, each with a small
        // swatch of its background + three accents so the palette can be
        // previewed before picking. A tick marks the active theme; a switch
        // applies immediately (applyTheme retints the whole chrome, the
        // badge/zoom buttons and the backdrop).
        static const char* const names[] = { "UV GOA", "STUDIO STEEL", "WARM ANALOG",
                                             "NEON", "PAPER", "OLED" };

        auto swatchFor = [] (int i) -> juce::Image
        {
            const auto& p = goaui::paletteForIndex (i);
            constexpr int w = 30, h = 14;

            juce::Image img (juce::Image::ARGB, w, h, true);
            juce::Graphics g (img);

            const juce::Colour chips[] = { p.bgDark, p.accent, p.accentA, p.accentB };
            const float cw = (float) w / 4.0f;
            for (int c = 0; c < 4; ++c)
            {
                g.setColour (chips[c]);
                g.fillRect (juce::Rectangle<float> (c * cw, 0.0f, cw + 0.5f, (float) h));
            }

            // Hairline frame so light chips (PAPER) read on light menus and
            // black chips (OLED) read on dark ones.
            g.setColour (juce::Colours::black.withAlpha (0.4f));
            g.drawRect (0.0f, 0.0f, (float) w, (float) h);
            return img;
        };

        juce::PopupMenu m;
        for (int i = 0; i < (int) goaui::themeCount; ++i)
        {
            auto di = std::make_unique<juce::DrawableImage>();
            di->setImage (swatchFor (i));

            juce::PopupMenu::Item item;
            item.itemID    = i + 1;                 // 0 would make it untriggerable
            item.text      = names[i];
            item.isEnabled = true;
            item.isTicked  = (int) goaui::activeTheme == i;
            item.image     = std::move (di);
            item.action    = [this, i]
            {
                goaui::setTheme (goaui::themeFromIndex (i), this);
                applyTheme();
                saveThemePref();   // the pick survives editor reopens
            };
            m.addItem (item);
        }

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (themeBtn));
    };

    menuBtn.setColour (juce::TextButton::buttonColourId, goaui::bgPanel);
    menuBtn.setColour (juce::TextButton::textColourOffId, goaui::accentB);
    addAndMakeVisible (menuBtn);
    menuBtn.onClick = [this]
    {
        juce::PopupMenu m;

        m.addSectionHeader (juce::String ("GOASYNTH ") + goaVersionString());
        m.addSeparator();
        {
            juce::PopupMenu::Item about;
            about.itemID   = 1;
            about.text     = "ABOUT";
            about.isEnabled = true;
            about.action   = [this]
            {
                if (aboutOverlay == nullptr)
                    return;
                // Rebuilt on every open so a mid-session activation is reflected.
            // (AboutOverlay is rebuilt-with-state here rather than cached.)
                aboutOverlay->stateLine.setText (
                    proc.licensedFlag.load() ? juce::String ("LICENSE  ACTIVATED")
                        : (proc.trial ? juce::String ("LICENSE  TRIAL \u2014 ")
                                          + goa::License::trialTimeLeft()
                                      : juce::String ("LICENSE  ACTIVATION REQUIRED")),
                    juce::dontSendNotification);
                aboutOverlay->setBounds (getLocalBounds());
                aboutOverlay->toFront (true);
                aboutOverlay->setVisible (true);
            };
            m.addItem (about);

            juce::PopupMenu::Item upd;
            upd.itemID   = 2;
            upd.text     = "CHECK FOR UPDATES";
            upd.isEnabled = true;
            upd.action   = [this] { runUpdateCheck(); };
            m.addItem (upd);

            juce::PopupMenu::Item log;
            log.itemID   = 3;
            log.text     = "CHANGE LOG";
            log.isEnabled = true;
            log.action   = [this]
            {
                // The site's release-notes section. No embedded browser in the
                // plugin: hand the URL to the system's default browser.
                juce::URL ("https://y4m4.github.io/GoaSynth/#whats-new")
                    .launchInDefaultBrowser();
            };
            m.addItem (log);
        }

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (menuBtn));
    };

    aboutOverlay = std::make_unique<goaui::AboutOverlay>();
    addChildComponent (*aboutOverlay);

    updateResultOverlay = std::make_unique<goaui::UpdateResultOverlay>();
    addChildComponent (*updateResultOverlay);

    zoomBtn.setColour (juce::TextButton::buttonColourId, goaui::bgPanelLo);
    zoomBtn.setColour (juce::TextButton::textColourOffId, goaui::accentA);
    zoomBtn.setTooltip ("Interface zoom: 100 / 125 / 150 / 175 / 200 %");
    addAndMakeVisible (zoomBtn);
    zoomBtn.onClick = [this]
    {
        static constexpr float steps[] = { 1.0f, 1.25f, 1.5f, 1.75f, 2.0f };
        int idx = 0;
        for (int i = 0; i < 5; ++i)
            if (std::fabs (steps[i] - uiZoom) < 0.01f)
                idx = i;        applyZoom (steps[(idx + 1) % 5]);
    };

    aiOverlay = std::make_unique<goaui::AiOverlay>();
    aiOverlay->setVisible (false);
    aiOverlay->onGenerate = [this] (const juce::String& brief, int varIdx,
                                    int engine, int model, const juce::String& cred)
    {
        applyAiPatch (brief, varIdx, engine, cred, model);
    };
    addChildComponent (*aiOverlay);

    saveBtn.setColour (juce::TextButton::buttonColourId, goaui::bgPanelLo);
    saveBtn.setColour (juce::TextButton::textColourOffId, goaui::accentA);
    saveBtn.setTooltip ("Save current patch as a user preset");
    addAndMakeVisible (saveBtn);
    saveBtn.onClick = [this]
    {
        if (saveOverlay == nullptr)
            return;
        // beginSaveAs (not prefill) so the dialog is definitely in SAVE mode: it
        // is also the rename prompt, and prefill alone would leave it renaming.
        renameSource = juce::File();
        saveOverlay->beginSaveAs ("MY GOA PATCH", {});
        saveOverlay->setBounds (getLocalBounds());
        saveOverlay->setVisible (true);
    };


    addAndMakeVisible (levelMeter);

    // Patch-workflow buttons: randomise / undo / redo / A-B / lock. They live
    // in the bottom bar beside the octave keys (the header has no width left).
    {
        auto wire = [this] (juce::TextButton& b)
        {
            b.setColour (juce::TextButton::buttonColourId, goaui::bgPanelLo);
            b.setColour (juce::TextButton::textColourOffId, goaui::textBright);
            addAndMakeVisible (b);
        };
        wire (randBtn); wire (undoBtn); wire (redoBtn); wire (abBtn); wire (lockBtn);

        randBtn.onClick = [this] { randomisePatch(); };
        undoBtn.onClick = [this] { undo(); };
        redoBtn.onClick = [this] { redo(); };
        abBtn.onClick   = [this] { swapAb(); };
        lockBtn.setClickingTogglesState (true);
        lockBtn.onClick = [this] { setLockMode (lockBtn.getToggleState()); };
        updateHistoryButtons();
        setLockMode (false);
    }

    saveOverlay = std::make_unique<goaui::SavePresetOverlay>();
    saveOverlay->setVisible (false);
    saveOverlay->onSave = [this] (const juce::String& n, const juce::StringArray& t, bool shared)
    {
        saveUserPreset (n, t, shared);
    };
    saveOverlay->onRename = [this] (const juce::String& n, const juce::StringArray& t)
    {
        renameUserPreset (renameSource, n, t);
    };
    addChildComponent (*saveOverlay);

    // Activation screen: covers everything until a valid serial lands.
    addAndMakeVisible (trialBadge);
    trialBadge.setTooltip ("Time left in your trial \u2014 click to activate");
    trialBadge.onClick = [this] { showLicenseOverlay (true); };

    licenseOverlay = std::make_unique<goaui::LicenseOverlay>();
    licenseOverlay->setVisible (false);
    licenseOverlay->onActivated = [this]
    {
        proc.licensed = true;
        proc.licensedFlag.store (true, std::memory_order_relaxed);
        proc.sessionSerial = goa::License::storedSerial();
        refreshLicenseUi();
    };
    addChildComponent (*licenseOverlay);

    masterCtl = std::make_unique<goaui::Ctl> (apvts, param::masterGain, "MASTER");
    masterCtl->liveEngine = &proc.synth;
    addAndMakeVisible (*masterCtl);

    buildContent (apvts);

    aiOverlay->toFront (false);

    // ---- bottom bar ----
    octDown.setButtonText ("-");
    octUp.setButtonText ("+");
    octDown.setColour (juce::TextButton::buttonColourId, goaui::bgPanel);
    octDown.setColour (juce::TextButton::textColourOffId, goaui::textDim);
    octUp.setColour (juce::TextButton::buttonColourId, goaui::bgPanel);
    octUp.setColour (juce::TextButton::textColourOffId, goaui::textDim);
    octDown.setTooltip ("Keyboard octave down");
    octUp.setTooltip    ("Keyboard octave up");
    addAndMakeVisible (octDown);
    addAndMakeVisible (octUp);
    octDown.onClick = [this] { keyboard.setOctave (keyboard.octave - 1); };
    octUp.onClick   = [this] { keyboard.setOctave (keyboard.octave + 1); };

    addAndMakeVisible (gateStrip);
    addAndMakeVisible (arpStrip);
    addAndMakeVisible (keyboard);

    voicingCtl = std::make_unique<goaui::Ctl> (apvts, param::voicing, "VOICING", true);
    polyCtl    = std::make_unique<goaui::Ctl> (apvts, param::polyMax, "POLY");
    portaCtl   = std::make_unique<goaui::Ctl> (apvts, param::glide, "PORTA");
    bendCtl    = std::make_unique<goaui::Ctl> (apvts, param::bendRange, "BEND");
    addAndMakeVisible (*voicingCtl);
    addAndMakeVisible (*polyCtl);
    addAndMakeVisible (*portaCtl);
    addAndMakeVisible (*bendCtl);

    // Scale quantizer + microtuning block (bottom-right, under the voicing row)
    scaleCtl = std::make_unique<goaui::Ctl> (apvts, param::arpScale, "SCALE", true);
    rootCtl  = std::make_unique<goaui::Ctl> (apvts, param::arpRoot,  "ROOT",  true);
    lockCtl  = std::make_unique<goaui::ToggleCtl> (apvts, param::scaleLock, "LOCK");
    tuneCtl  = std::make_unique<goaui::Ctl> (apvts, param::tuningFine, "FINE\u00a2");
    addAndMakeVisible (*scaleCtl);
    addAndMakeVisible (*rootCtl);
    addAndMakeVisible (*lockCtl);
    addAndMakeVisible (*tuneCtl);
    sclBtn.setColour (juce::TextButton::buttonColourId, goaui::bgPanelLo);
    sclBtn.setColour (juce::TextButton::textColourOffId, goaui::accentA);
    sclBtn.onClick = [this]
    {
        if (proc.scalaLoaded())
        {
            proc.clearScala();
            sclBtn.setButtonText ("SCL");
            sclBtn.setTooltip ("Load a Scala .scl microtuning file (replaces 12-TET)");
            return;
        }
        sclChooser = std::make_shared<juce::FileChooser> ("Load Scala tuning",
            juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.scl");
        sclChooser->launchAsync (juce::FileBrowserComponent::openMode
                                   | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                const auto f = fc.getResult();
                if (! f.existsAsFile())
                    return;
                if (proc.loadScalaFile (f))
                {
                    sclBtn.setButtonText (proc.scalaName().substring (0, 10).toUpperCase());
                    sclBtn.setTooltip (proc.scalaName()
                        + " \u2014 " + juce::String (proc.scalaDegrees())
                        + " degrees. Click to return to 12-TET.");
                }
            });
    };
    if (proc.scalaLoaded())
    {
        sclBtn.setButtonText (proc.scalaName().substring (0, 10).toUpperCase());
        sclBtn.setTooltip (proc.scalaName()
            + " \u2014 " + juce::String (proc.scalaDegrees())
            + " degrees. Click to return to 12-TET.");
    }
    addAndMakeVisible (sclBtn);

    // Default size stays the classic 1120 x 780; setResizeLimits makes the
    // window user-resizable and loadSizePref() restores the last chosen size
    // for hosts that forget their bounds between sessions.
    //
    // The minimum height is what the layout can actually honour: header 56 +
    // padding 4 + row 250 + gap + row 170 + gap + row 114 + padding 4 +
    // bottom bar 110 = 720. The old 640 minimum was below the point where the
    // rows fit, which is what let the FX row overlap the bottom bar.
    setResizeLimits (960, 720, 2048, 1440);
    loadSizePref();   // restores the last chosen size, else the 1120 x 780 default
    loadThemePref();  // restore the saved skin BEFORE the first paint/retint pass
    applyTheme();
    rebuildBackdrop();
    refreshLicenseUi();
    startTimerHz (30);

    // Interface zoom: apply the saved preference (editor is fully built here).
    loadZoomPref();
    applyZoom (uiZoom);
}

// Show the activation screen instead of the synth while unlicensed; on
// activation (or a licensed session load) reveal the full UI.
// Show the activation screen. Forced (expired trial / unlicensed) it is the
// whole UI; as a trial "peek" a × lets the user go back without activating.
void GoaSynthAudioProcessorEditor::showLicenseOverlay (bool allowClose)
{
    licenseOverlay->dismissible = allowClose;
    licenseOverlay->closeBtn.setVisible (allowClose);
    licenseOverlay->closeBtn.setEnabled (allowClose);

    // Say why the screen is up: the 24 h trial ran out vs never activated.
    licenseOverlay->setHeadline (! proc.licensedFlag.load() && ! proc.trial
                                     && goa::License::trialTimeLeft() == "expired");

    if (licenseOverlay->getBounds() != getLocalBounds())   // e.g. DAW resized while a peek was up
        licenseOverlay->setBounds (getLocalBounds());
    licenseOverlay->setVisible (true);
    licenseOverlay->serial.grabKeyboardFocus();
}

void GoaSynthAudioProcessorEditor::refreshLicenseUi()
{
    const bool licensed = proc.licensedFlag.load();

    if (licensed)
    {
        licenseOverlay->setVisible (false);
        return;
    }

    if (proc.trial)
        return;                              // trial running: the synth stays usable

    showLicenseOverlay (false);              // unlicensed + no trial: forced screen
}

// 24 h trial readout (TRIAL 4 h 12 m / TRIAL EXPIRED / nothing when
// licensed). Cheap enough for the 30 Hz timer; only rewrites the label when
// 24 h trial readout in the header. Hidden when licensed or expired; a
// click opens the activation screen as a dismissible "peek".
void GoaSynthAudioProcessorEditor::updateTrialBadge()
{
    const bool trialRunning = proc.trial && proc.licensedFlag.load();

    trialBadge.setVisible (trialRunning);     // bounds are assigned in resized() either way
    if (trialRunning)
        trialBadge.setButtonText ("TRIAL " + goa::License::trialTimeLeft().toUpperCase());

    // After mid-session expiry the trial flags are false but the overlay may
    // still be down (host without processBlock calls, e.g. rendering before
    // transport starts): keep the activation screen offered here too.
    if (! proc.licensedFlag.load() && ! proc.trial
        && licenseOverlay != nullptr && ! licenseOverlay->isVisible())
    {
        showLicenseOverlay (false);
    }
}

// A modal overlay covers the synth (the activation screen doesn't count:
// dropped .goalicense files are exactly what it is for).
bool GoaSynthAudioProcessorEditor::anyOverlayUp() const
{
    return (aiOverlay != nullptr && aiOverlay->isVisible())
        || (modOverlay != nullptr && modOverlay->isVisible())
        || (saveOverlay != nullptr && saveOverlay->isVisible())
        || (presetBrowser != nullptr && presetBrowser->isVisible());
}

// Double-click license import: Windows opens .goalicense files with GoaSynth
// and delivers the path here as a file drop (EditorHost also routes the DAW's
// own drags through this).
bool GoaSynthAudioProcessorEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    if (anyOverlayUp())
        return false;

    for (const auto& f : files)
    {
        const juce::File file (f);
        // A .goalicense is only useful while unlicensed; an audio file is a
        // wavetable import and is welcome in any licensing state.
        if (file.hasFileExtension (goa::License::fileExtension))
            return ! proc.licensedFlag.load();
        if (file.hasFileExtension ("wav;aif;aiff;flac"))
            return true;
    }

    return false;
}

void GoaSynthAudioProcessorEditor::filesDropped (const juce::StringArray& files, int x, int y)
{
    if (anyOverlayUp())
        return;

    // Dropping onto the OSC B panel imports into B; anywhere else goes to A.
    // Deterministic, and discoverable: the panel under the cursor is the one
    // that changes.
    const int oscIndex = oscBFrame.getBounds().contains (x, y) ? 1 : 0;

    for (const auto& path : files)
    {
        const juce::File f (path);

        if (f.hasFileExtension ("wav;aif;aiff;flac"))
        {
            importWavFile (f, oscIndex);
            return;
        }

        if (! proc.licensedFlag.load()
            && f.hasFileExtension (goa::License::fileExtension))
        {
            // During a trial the overlay is hidden - bring it up as a peek so
            // success/failure feedback isn't written to an invisible component.
            if (! licenseOverlay->isVisible())
                showLicenseOverlay (true);

            licenseOverlay->importLicenseFile (f);   // onActivated -> refreshLicenseUi()
            return;
        }
    }
}

// Push the active theme into every component. Called once at construction
// (after all children exist) and from the THEME button.
// ---- interface zoom ----------------------------------------------------------
// The editor's layout is written for exactly 1120x780 logical pixels; a
// component transform scales the whole thing to any size without touching
// that code. The VST3 wrapper's childBoundsChanged picks the new bounds up
// and resizes the host window, so the plug-in grows in the DAW too.

// Window size and UI zoom are remembered in the same per-user folder as the
// preset bank. Tests sweep the editor through its whole resize range and flip
// the zoom, and saveSizePref() also fires from the destructor - so without an
// override a single test run would silently overwrite the real user's saved
// window size. Same convention as GOASYNTH_PRESET_DIR / GOASYNTH_LICENSE_FILE;
// tests assert the override is honoured before touching anything.
static juce::File envOrAppDataFile (const char* envVar, const char* fileName)
{
    if (const char* over = std::getenv (envVar))
        if (*over != 0)
            return juce::File (juce::String::fromUTF8 (over));

    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("GoaSynth").getChildFile (fileName);
}

juce::File GoaSynthAudioProcessorEditor::zoomPrefFile()
{
    return envOrAppDataFile ("GOASYNTH_ZOOM_FILE", "zoom.txt");
}

juce::File GoaSynthAudioProcessorEditor::sizePrefFile()
{
    return envOrAppDataFile ("GOASYNTH_SIZE_FILE", "size.txt");
}

void GoaSynthAudioProcessorEditor::loadSizePref()
{
    const juce::StringArray parts =
        juce::StringArray::fromTokens (sizePrefFile().loadFileAsString(), " ", "");
    // Restore the last chosen size, or fall back to the classic default when
    // there is no usable preference (first run, corrupt file, or a sandboxed
    // test that never wrote one). This used to be dead code: the constructor
    // called setSize (defaultWidth, defaultHeight) unconditionally straight
    // after, so size.txt was written on every exit but never actually read.
    if (parts.size() == 2)
        setSize (juce::jlimit (960, 2048, parts[0].getIntValue()),
                 juce::jlimit (720, 1440, parts[1].getIntValue()));
    else
        setSize (defaultWidth, defaultHeight);
}

void GoaSynthAudioProcessorEditor::saveSizePref()
{
    sizePrefFile().getParentDirectory().createDirectory();
    sizePrefFile().replaceWithText (juce::String (getWidth()) + " "
                                    + juce::String (getHeight()));
}

void GoaSynthAudioProcessorEditor::loadZoomPref()
{
    const float z = zoomPrefFile().loadFileAsString().getFloatValue();
    if (z >= 0.99f && z <= 2.01f)
        uiZoom = juce::jlimit (1.0f, 2.0f, z);
}

void GoaSynthAudioProcessorEditor::saveZoomPref()
{
    zoomPrefFile().getParentDirectory().createDirectory();
    zoomPrefFile().replaceWithText (juce::String (uiZoom, 2));
}

juce::File GoaSynthAudioProcessorEditor::themePrefFile()
{
    return envOrAppDataFile ("GOASYNTH_THEME_FILE", "theme.txt");
}

void GoaSynthAudioProcessorEditor::loadThemePref()
{
    // Out-of-range (first run, corrupt file, sandbox that never wrote one)
    // keeps the house look; themeFromIndex clamps anyway, but keep it explicit.
    const int idx = themePrefFile().loadFileAsString().trim().getIntValue();
    if (juce::isPositiveAndBelow (idx, (int) goaui::themeCount))
        goaui::setTheme (goaui::themeFromIndex (idx), nullptr);
}

void GoaSynthAudioProcessorEditor::saveThemePref()
{
    themePrefFile().getParentDirectory().createDirectory();
    themePrefFile().replaceWithText (juce::String ((int) goaui::activeTheme));
}

bool GoaSynthAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    static constexpr float steps[] = { 1.0f, 1.25f, 1.5f, 1.75f, 2.0f };
    if (key.getModifiers().isCtrlDown())
    {
        if (key.getTextCharacter() == '+')
        {
            int idx = 0;
            for (int i = 0; i < 5; ++i)
                if (std::fabs (steps[i] - uiZoom) < 0.01f)
                    idx = i;
            applyZoom (steps[juce::jmin (4, idx + 1)]);
            return true;
        }
        if (key.isKeyCode ('-'))
        {
            int idx = 0;
            for (int i = 0; i < 5; ++i)
                if (std::fabs (steps[i] - uiZoom) < 0.01f)
                    idx = i;
            applyZoom (steps[juce::jmax (0, idx - 1)]);
            return true;
        }
    }
    return false;
}

void GoaSynthAudioProcessorEditor::mouseWheelMove (const juce::MouseEvent&,
                                                   const juce::MouseWheelDetails& wheel)
{
    if (! juce::ModifierKeys::getCurrentModifiersRealtime().isCtrlDown())
        return;

    static constexpr float steps[] = { 1.0f, 1.25f, 1.5f, 1.75f, 2.0f };
    int idx = 0;
    for (int i = 0; i < 5; ++i)
        if (std::fabs (steps[i] - uiZoom) < 0.01f)
            idx = i;

    if (wheel.deltaY > 0.05f && idx < 4)
        applyZoom (steps[idx + 1]);
    else if (wheel.deltaY < -0.05f && idx > 0)
        applyZoom (steps[idx - 1]);
}

void GoaSynthAudioProcessorEditor::applyZoom (float z)
{
    uiZoom = juce::jlimit (1.0f, 2.0f, z);
    setTransform (juce::AffineTransform::scale (uiZoom));
    zoomBtn.setButtonText (juce::String (juce::roundToInt (uiZoom * 100.0f)) + "%");
    saveZoomPref();
    repaint();
}

void GoaSynthAudioProcessorEditor::applyTheme()
{
    goaui::setTheme (goaui::activeTheme, this);

    trialBadge.setColour (juce::TextButton::buttonColourId, goaui::bgPanel);
    trialBadge.setColour (juce::TextButton::textColourOffId, goaui::accentB);
    themeBtn.setColour (juce::TextButton::buttonColourId, goaui::bgPanel);
    themeBtn.setColour (juce::TextButton::textColourOffId, goaui::accentB);
    menuBtn.setColour (juce::TextButton::buttonColourId, goaui::bgPanel);
    menuBtn.setColour (juce::TextButton::textColourOffId, goaui::accentB);
    if (aboutOverlay != nullptr)
        aboutOverlay->retint();
    if (updateResultOverlay != nullptr)
        updateResultOverlay->retint();
    juce::String themeLabel;
    switch (goaui::activeTheme)
    {
        case goaui::themeUv:     themeLabel = "THEME: UV";    break;
        case goaui::themeSteel:  themeLabel = "THEME: STEEL"; break;
        case goaui::themeAnalog: themeLabel = "THEME: WARM";  break;
        case goaui::themeNeon:   themeLabel = "THEME: NEON";  break;
        case goaui::themePaper:  themeLabel = "THEME: PAPER"; break;
        case goaui::themeOled:   themeLabel = "THEME: OLED";  break;
        default:                 themeLabel = "THEME";        break;
    }
    themeBtn.setButtonText (themeLabel);
    zoomBtn.setColour (juce::TextButton::buttonColourId, goaui::bgPanelLo);
    zoomBtn.setColour (juce::TextButton::textColourOffId, goaui::accentA);
    if (presetBrowser != nullptr && presetBrowser->isVisible())
        presetBrowser->retint();
    rebuildBackdrop();
    repaint();
}

GoaSynthAudioProcessorEditor::~GoaSynthAudioProcessorEditor()
{
    // An in-flight update-check thread must be shut down before its callbacks
    // can touch dead members; stopThread joins it (safe to call when it never
    // ran).
    if (updateThread != nullptr)
        updateThread->stopThread (4000);

    saveSizePref();   // remember the window size for the next session
    stopTimer();

    // Closing the window mid-note would otherwise leave the voice ringing
    // forever: the keyboard's mouse-up note-off dies with the editor.
    proc.releaseAllUiNotes();

    setLookAndFeel (nullptr);
}

// Pre-render the two spiral layers at half resolution: each layer is a two-arm
// logarithmic spiral (Arm A teal, Arm B magenta) over a violet haze, blurred by
// drawing to a downscaled image. Off-center vortex centres so the layers create
// an evolving two-lobed pattern when counter-rotated on top of each other.
void GoaSynthAudioProcessorEditor::rebuildBackdrop()
{
    const int size = juce::jlimit (64, 1024,
        juce::jmax (getWidth(), getHeight()) / 2);
    if (backdropSize == (double) size && swirl1.isValid())
        return;
    backdropSize = size;

    swirl1 = juce::Image (juce::Image::ARGB, size, size, true);
    swirl2 = juce::Image (juce::Image::ARGB, size, size, true);

    struct Spiral
    {
        juce::Image& img;
        juce::Point<float> centre;
        float scale;
        float phase;
        juce::Colour arm1, arm2;
    };
    const Spiral layers[2] =
    {
        { swirl1, { size * 0.38f, size * 0.44f }, 1.00f, 0.0f,
          goaui::accentA.withAlpha (0.20f), goaui::accentB.withAlpha (0.13f) },
        { swirl2, { size * 0.62f, size * 0.58f }, 0.72f, 0.9f,
          goaui::accent.withAlpha (0.16f), goaui::accentA.withAlpha (0.10f) }
    };

    for (const auto& L : layers)
    {
        juce::Graphics gl (L.img);
        gl.addTransform (juce::AffineTransform::translation (-L.centre.x, -L.centre.y)
                             .scaled (L.scale, L.scale)
                             .rotated (L.phase));
        // Wide violet haze under the arms.
        juce::ColourGradient haze (goaui::accent.withAlpha (0.10f), 0, 0,
                                   goaui::accent.withAlpha (0.0f), size * 0.75f, size * 0.75f, true);
        gl.setGradientFill (haze);
        gl.fillEllipse (-size * 0.75f, -size * 0.75f,
                        size * 1.5f, size * 1.5f);

        // Two log-spiral arms, painted as chained thick line segments.
        for (int arm = 0; arm < 2; ++arm)
        {
            gl.setColour (arm == 0 ? L.arm1 : L.arm2);
            const float dir = arm == 0 ? 1.0f : -1.0f;
            const float armPhase = arm * juce::MathConstants<float>::pi;
            const float b = 0.16f;                       // log-spiral tightness
            const float armScale = size * 0.02f;
            juce::Path p;
            bool started = false;
            for (float a = 0.0f; a < 4.6f; a += 0.05f)   // ~0.73 turns per arm
            {
                const float r = armScale * std::exp (b * a);
                const float ang = armPhase + dir * a;
                const auto pt = juce::Point<float> (
                    r * std::cos (ang), r * std::sin (ang));
                if (! started) { p.startNewSubPath (pt); started = true; }
                else           { p.lineTo (pt); }
            }
            juce::PathStrokeType stroke (size * 0.022f,
                                         juce::PathStrokeType::curved,
                                         juce::PathStrokeType::rounded);
            gl.strokePath (p, stroke);
        }
    }
}

// 30 fps animation: counter-rotate the two spiral layers and scale-pulse the
// composite with the smoothed audio level; then cache the composite so paint()
// only stretches one image.
void GoaSynthAudioProcessorEditor::timerCallback()
{
    animClock += 1.0 / 30.0;
    updateTrialBadge();

    // Breathing logo glow: slow sine 0..1, ~4 second period.
    logoGoa.breathe = 0.5f + 0.5f * std::sin ((float) animClock * 1.6f);
    logoGoa.repaint();

    // Stuck-note rescue: if the processor is holding a UI note the keyboard
    // no longer knows about (a swallowed mouse-up), silence it. release() is
    // idempotent, so this is safe to call redundantly.
    if (keyboard.held < 0 && proc.uiHeldCount() > 0)
        proc.releaseAllUiNotes();
    // Alt-Tab / stolen mouse capture mid-press: the keyboard still thinks it's
    // held but no mouse button is down anywhere, so the mouse-up never comes.
    if (keyboard.held >= 0 && ! juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
        keyboard.release();

    // Trial expiring mid-session must lock the running UI: without this the
    // synth window would stay up with silent audio until the next reopen.
    // processBlock refreshes proc.trial/licensedFlag from the License store
    // (that check also runs headless), so the flags now track expiry live and
    // the pendingPoke debounce means the overlay appears within one tick of
    // the first silent block.
    if (proc.pendingPoke.exchange (false, std::memory_order_relaxed))
        refreshLicenseUi();

    const float level = proc.uiLevel.load (std::memory_order_relaxed);
    if (backdropSize > 0.0 && swirl1.isValid() && swirl2.isValid())
    {
        const int s = (int) backdropSize;
        if (! backdrop.isValid() || backdrop.getWidth() != s)
            backdrop = juce::Image (juce::Image::ARGB, s, s, true);

        juce::Graphics gb (backdrop);
        gb.fillAll (juce::Colours::transparentBlack);
        const float pulse = 1.0f + level * 0.10f;
        const float rot1 = (float) (0.10 * animClock) + (float) std::sin (animClock * 0.23) * 0.35f;
        const float rot2 = (float) (-0.14 * animClock) + (float) std::sin (animClock * 0.31) * 0.45f;
        gb.drawImageTransformed (swirl1,
            juce::AffineTransform::rotation (rot1, s * 0.5f, s * 0.5f)
                .scaled (pulse, pulse, s * 0.5f, s * 0.5f));
        gb.drawImageTransformed (swirl2,
            juce::AffineTransform::rotation (rot2, s * 0.5f, s * 0.5f)
                .scaled (pulse, pulse, s * 0.5f, s * 0.5f));
    }

    repaint();
}

void GoaSynthAudioProcessorEditor::buildContent (juce::AudioProcessorValueTreeState& apvts)
{
    auto knob = [this, &apvts] (const char* id, const char* name, bool combo = false)
    {
        auto c = std::make_unique<goaui::Ctl> (apvts, id, name, combo);
        c->liveEngine = &proc.synth;   // animated mod dots read live sources
        addAndMakeVisible (*c);

        // History + lock hooks. A knob drag pushes ONE snapshot (at gesture
        // start, not per sample), so UNDO after a tweak restores the value the
        // knob had before the drag.
        auto* raw = c.get();
        raw->onLockClick = [this, raw]
        {
            const juce::String pid = raw->paramId;
            if (isLocked (pid))
                lockedParams.erase (pid);
            else
                lockedParams.insert (pid);
            raw->locked = isLocked (pid);
            raw->repaint();
        };
        raw->slider.onDragStart = [this] { pushUndo(); };
        raw->combo.onChange = [this] { pushUndo(); };
        allCtls.push_back (raw);
        return c;
    };
    auto toggle = [this, &apvts] (const char* id, const char* name)
    {
        auto c = std::make_unique<goaui::ToggleCtl> (apvts, id, name);
        c->liveEngine = &proc.synth;
        // Snapshot before a switch flips, so UNDO covers toggles too.
        c->btn.onClick = [this] { pushUndo(); };
        addAndMakeVisible (*c);
        return c;
    };

    // OSC row
    addAndMakeVisible (oscAFrame);
    addAndMakeVisible (oscBFrame);
    addAndMakeVisible (filtFrame);
    addAndMakeVisible (filt2Frame);
    addAndMakeVisible (subFrame);
    addAndMakeVisible (waveA);
    addAndMakeVisible (waveB);
    addAndMakeVisible (filtCurve);

    // The wave displays' 8th tool (WAV) hands off to the editor, which owns the
    // file chooser and the slice-into-frames import.
    waveA.onLoadWav = [this] { loadWavIntoWavetable (0); };
    waveB.onLoadWav = [this] { loadWavIntoWavetable (1); };

    wave1Ctl  = knob (param::osc1Wave,   "WAVE", true);
    oct1Ctl   = knob (param::osc1Oct,    "OCT");
    fin1Ctl   = knob (param::osc1Fine,   "FIN");
    uni1Ctl   = knob (param::uniVoices,  "UNI");
    det1Ctl   = knob (param::uniDetune,  "DETUNE");
    wid1Ctl   = knob (param::uniSpread,  "WIDTH");
    pan1Ctl   = knob (param::osc1Pan,    "PAN");
    wt1Ctl    = knob (param::osc1WtPos,  "WT POS");
    lvl1Ctl   = knob (param::osc1Level,  "LEVEL");
    ph1Ctl    = knob (param::osc1Phase,  "PHASE");
    prand1Ctl = toggle (param::osc1PRand, "P.RAND");

    wave2Ctl  = knob (param::osc2Wave,   "WAVE", true);
    oct2Ctl   = knob (param::osc2Oct,    "OCT");
    fin2Ctl   = knob (param::osc2Fine,   "FIN");
    uni2Ctl   = knob (param::uniVoices,  "UNI");
    det2Ctl   = knob (param::uniDetune,  "DETUNE");
    wid2Ctl   = knob (param::uniSpread,  "WIDTH");
    pan2Ctl   = knob (param::osc2Pan,    "PAN");
    wt2Ctl    = knob (param::osc2WtPos,  "WT POS");
    lvl2Ctl   = knob (param::osc2Level,  "LEVEL");
    ph2Ctl    = knob (param::osc2Phase,  "PHASE");
    prand2Ctl = toggle (param::osc2PRand, "P.RAND");
    fmCtl     = knob (param::fmAmount,   "FM > A");

    // Oscillator inter-modulation: PWM duty (shared by both oscillators' PWM
    // wave), hard sync (A reset by B) and ring mod (A x B).
    pwCtl     = knob (param::pulseWidth, "PULSE W");
    ringCtl   = knob (param::ringMod,    "RING");
    syncCtl   = toggle (param::oscSync,  "SYNC");

    ftypeCtl  = knob (param::filterType, "TYPE", true);
    cutoffCtl = knob (param::cutoff,     "CUTOFF");
    resoCtl   = knob (param::reso,       "RESO");
    driveCtl  = knob (param::drive,      "DRIVE");
    keyCtl    = knob (param::keytrack,   "KEY");
    ftype2Ctl = knob (param::filter2Type, "TYPE B", true);
    cutoff2Ctl= knob (param::cutoff2,    "CUT B");
    reso2Ctl  = knob (param::reso2,      "RES B");
    routeCtl  = knob (param::filterRoute, "ROUTE", true);
    vOnCtl    = toggle (param::vowelOn,    "VOWEL");
    hqCtl     = toggle (param::masterHQ,   "QUALITY");
    vMorphCtl = knob (param::vowelMorph, "MORPH");
    vResCtl   = knob (param::vowelRes,   "V-RES");
    vMixCtl   = knob (param::vowelMix,   "V-MIX");
    fdCtl     = knob (param::fDrive,     "F-DRIVE");
    fbCtl     = knob (param::fFeedback,  "F-FB");

    pumpSyncCtl  = knob (param::pumpSync,  "PUMP", true);
    pumpDepthCtl = knob (param::pumpDepth, "DEPTH");
    analogCtl    = knob (param::analogAmt, "ANALOG");

    // Supersaw character: shapes the unison detune/pan field (CLASSIC / PHASED
    // / HYPER) and chord memory (OFF / 5TH / MINOR / MAJOR / OCT).
    charCtl  = knob (param::uniMode,   "CHAR", true);
    chordCtl = knob (param::chordMode, "CHORD", true);

    // Master EQ (post MASTER, pre limiter). 0 dB on every band is a true bypass.
    eqLowCtl     = knob (param::eqLow,     "EQ LOW");
    eqMidCtl     = knob (param::eqMid,     "EQ MID");
    eqMidFreqCtl = knob (param::eqMidFreq, "MID F");
    eqHighCtl    = knob (param::eqHigh,    "EQ HIGH");

    // FX row panels are added BEFORE any of their controls. A Panel paints its
    // own opaque background, so a control created earlier sits underneath it
    // and never renders. SHIM, DUCK, NOISE LEVEL and MACRO A/B were invisible
    // for exactly this reason - they were created above, before these frames
    // were added (found by rendering the row rather than measuring it).
    addAndMakeVisible (chorusFrame);
    addAndMakeVisible (phaserFrame);
    addAndMakeVisible (delayFrame);
    addAndMakeVisible (reverbFrame);
    addAndMakeVisible (moveFrame);
    addAndMakeVisible (macroFrame);
    addAndMakeVisible (ottFrame);
    // NOISE is a single knob: no panel frame, just the control + a small label.

    // FX-duck + reverb-shimmer send.
    shimCtl = knob (param::revShimmer, "SHIM");
    duckCtl = knob (param::duckAmt,    "DUCK");

    subWaveCtl = knob (param::subWave,   "WAVE", true);
    subOctCtl  = knob (param::subOct,    "OCT");
    subCtl     = knob (param::subLevel,  "LEVEL");
    noiseCtl   = knob (param::noiseLevel, "NOISE");
    noiseTypeCtl = knob (param::noiseType, "TYPE", true);   // WHITE / PINK

    // Performance macros (matrix SOURCES; also MIDI CC 14 / CC 15).
    macroACtl = knob (param::macroA, "A");
    macroBCtl = knob (param::macroB, "B");

    // ENV / LFO row
    addAndMakeVisible (ampEnvFrame);
    addAndMakeVisible (filEnvFrame);
    addAndMakeVisible (lfo1Frame);
    addAndMakeVisible (lfo2Frame);
    addAndMakeVisible (ampGraph);
    addAndMakeVisible (filGraph);
    addAndMakeVisible (lfo1Graph);
    addAndMakeVisible (lfo2Graph);

    aACtl = knob (param::ampA, "ATK");
    aDCtl = knob (param::ampD, "DEC");
    aSCtl = knob (param::ampS, "SUS");
    aRCtl = knob (param::ampR, "REL");
    fACtl = knob (param::filtA, "ATK");
    fDCtl = knob (param::filtD, "DEC");
    fSCtl = knob (param::filtS, "SUS");
    fRCtl = knob (param::filtR, "REL");
    envAmtCtl   = knob (param::envAmt,   "ENV AMT");
    modDepthCtl = knob (param::modDepth, "MODWHL");   // "MODWHEEL" overran its cell

    l1RateCtl  = knob (param::lfo1Rate,   "RATE");
    l1WaveCtl  = knob (param::lfo1Wave,   "WAVE", true);
    l1TgtCtl   = knob (param::lfo1Target, "TARGET", true);
    l1DepthCtl = knob (param::lfo1Depth,  "DEPTH");
    l1UnitCtl  = knob (param::lfo1Unit,   "UNIT", true);
    l1DivCtl   = knob (param::lfo1Div,    "SYNC", true);
    l2RateCtl  = knob (param::lfo2Rate,   "RATE");
    l2WaveCtl  = knob (param::lfo2Wave,   "WAVE", true);
    l2TgtCtl   = knob (param::lfo2Target, "TARGET", true);
    l2DepthCtl = knob (param::lfo2Depth,  "DEPTH");
    l2UnitCtl  = knob (param::lfo2Unit,   "UNIT", true);
    l2DivCtl   = knob (param::lfo2Div,    "SYNC", true);

    // FX row controls. The frames themselves are added above, before the first
    // of these, so none of them can be painted over.
    chRateCtl  = knob (param::chorusRate,  "RATE");
    chDepthCtl = knob (param::chorusDepth, "DEP");   // "DEPTH" ellipsized in a 3-knob panel
    chMixCtl   = knob (param::chorusMix,   "MIX");
    phRateCtl  = knob (param::phRate,      "RATE");
    phDepthCtl = knob (param::phDepth,     "DEP");   // "DEPTH" ellipsized in a 3-knob panel
    phMixCtl   = knob (param::phMix,       "MIX");
    dSyncCtl   = knob (param::delaySync,   "SYNC", true);
    dTimeCtl   = knob (param::delayTime,   "TIME");
    dFbCtl     = knob (param::delayFb,     "FB");     // "FEEDBACK" overran its cell
    dMixCtl    = knob (param::delayMix,    "MIX");
    rSizeCtl   = knob (param::revSize,     "SIZE");
    rDampCtl   = knob (param::revDamp,     "DAMP");
    rMixCtl    = knob (param::revMix,      "MIX");
    oDepthCtl  = knob (param::ottDepth,    "DEPTH");
    oLowCtl    = knob (param::ottLow,      "LOW");
    oMidCtl    = knob (param::ottMid,      "MID");
    oHighCtl   = knob (param::ottHigh,     "HIGH");
    oOutCtl    = knob (param::ottOut,      "OUT");
    driftCtl     = knob (param::drift,     "DRIFT");
    uniDetCtl    = knob (param::uniDetune, "DET");    // "DETUNE" overran its cell
    uniSpreadCtl = knob (param::uniSpread, "WIDE");   // "WIDTH" exactly filled its cell
}

void GoaSynthAudioProcessorEditor::applyPreset (int index)
{
    // Patch-level change: snapshot first so UNDO can come back to what was
    // playing before the load.
    pushUndo();

    auto& apvts = proc.apvts;
    for (auto* par : proc.getParameters())
        par->setValueNotifyingHost (par->getDefaultValue());

    if (juce::isPositiveAndBelow (index, (int) visiblePresets.size()))
    {
        const auto& entry = visiblePresets[(size_t) index];
        if (entry.factoryIndex >= 0)
        {
            const auto& pr = getPresets()[(size_t) entry.factoryIndex];
            for (const auto& [id, v] : pr.values)
                if (auto* par = apvts.getParameter (id))
                    par->setValueNotifyingHost (par->convertTo0to1 (v));
        }
        else if (entry.file.existsAsFile())
        {
            if (auto xml = juce::parseXML (entry.file))
                proc.applyStateXml (*xml);
        }
        selectedPreset = index;
        presetName.setText (entry.name, juce::dontSendNotification);
        // Keep the host's program display in step with the browser: program 0
        // is Init, so factory index i is program i + 1. A user patch has no
        // program slot, so the index is left where it was.
        if (entry.factoryIndex >= 0)
            proc.noteUiProgram (entry.factoryIndex + 1);
    }
    updatePresetTint();
    goaui::repaintCtlPaints (this);   // preset changed the matrix -> refresh knob dots
}

void GoaSynthAudioProcessorEditor::cyclePreset (int direction)
{
    const int n = (int) visiblePresets.size();
    if (n <= 0)
        return;
    applyPreset (((selectedPreset + direction) % n + n) % n);
}

//==============================================================================
// Patch history, A/B compare, randomise and parameter lock.
//
// Scope of undo: patch-level operations — preset loads, randomise, A/B swaps,
// and knob/selector gestures (one snapshot per gesture, taken at drag start).
// It deliberately does NOT try to undo host automation ramps: those are the
// host's business, and pushing on every parameter change would fill the stack
// within seconds and make UNDO useless.
GoaSynthAudioProcessorEditor::Snapshot GoaSynthAudioProcessorEditor::capturePatch() const
{
    Snapshot s;
    for (auto* par : proc.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (par))
            s.emplace_back (rp->paramID, rp->getValue());
    return s;
}

void GoaSynthAudioProcessorEditor::applySnapshot (const Snapshot& s)
{
    for (const auto& [id, v] : s)
        if (auto* p = proc.apvts.getParameter (id))
            p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, v));
}

void GoaSynthAudioProcessorEditor::pushUndo()
{
    undoStack.push_back (capturePatch());
    if (undoStack.size() > maxUndo)
        undoStack.erase (undoStack.begin());
    redoStack.clear();
    updateHistoryButtons();
}

void GoaSynthAudioProcessorEditor::undo()
{
    if (undoStack.empty())
        return;
    redoStack.push_back (capturePatch());
    applySnapshot (undoStack.back());
    undoStack.pop_back();
    updateHistoryButtons();
    goaui::repaintCtlPaints (this);
}

void GoaSynthAudioProcessorEditor::redo()
{
    if (redoStack.empty())
        return;
    undoStack.push_back (capturePatch());
    applySnapshot (redoStack.back());
    redoStack.pop_back();
    updateHistoryButtons();
    goaui::repaintCtlPaints (this);
}

void GoaSynthAudioProcessorEditor::updateHistoryButtons()
{
    undoBtn.setEnabled (! undoStack.empty());
    redoBtn.setEnabled (! redoStack.empty());
}

void GoaSynthAudioProcessorEditor::swapAb()
{
    if (! abValid)
    {
        // First press: both slots start as the current patch, so nothing
        // changes audibly — the user is now "in B" and free to edit, with A
        // still holding the version they started from.
        abA = capturePatch();
        abB = abA;
        abValid = true;
        abSlot = 1;
    }
    else if (abSlot == 0)
    {
        abA = capturePatch();
        applySnapshot (abB);
        abSlot = 1;
    }
    else
    {
        abB = capturePatch();
        applySnapshot (abA);
        abSlot = 0;
    }
    abBtn.setButtonText (abSlot == 0 ? "A" : "B");
    goaui::repaintCtlPaints (this);
}

void GoaSynthAudioProcessorEditor::setLockMode (bool on)
{
    lockMode = on;
    for (auto* c : allCtls)
        c->setLockMode (on);
    lockBtn.setToggleState (on, juce::dontSendNotification);
    lockBtn.setColour (juce::TextButton::buttonColourId,
                       on ? goaui::accent : goaui::bgPanelLo);
    lockBtn.setColour (juce::TextButton::textColourOffId,
                       on ? goaui::bgDark : goaui::textBright);
}

bool GoaSynthAudioProcessorEditor::isStepParamId (const juce::String& id,
                                                  const juce::String& prefix)
{
    if (! id.startsWith (prefix))
        return false;
    const juce::String rest = id.substring (prefix.length());
    return rest.isNotEmpty() && rest.containsOnly ("0123456789");
}

void GoaSynthAudioProcessorEditor::randomisePatch()
{
    pushUndo();
    auto& rng = juce::Random::getSystemRandom();

    for (auto* par : proc.getParameters())
    {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*> (par);
        if (rp == nullptr)
            continue;
        const juce::String id = rp->paramID;

        if (isLocked (id))
            continue;

        // Structural, routing and pattern parameters: randomising these makes
        // a scrambled setup rather than a playable patch, so they are left
        // exactly as the user set them.
        if (id.startsWith ("mod"))                               continue;
        if (isStepParamId (id, "gate"))                          continue;
        if (isStepParamId (id, "arp"))                           continue;
        if (isStepParamId (id, "arpVel"))                        continue;
        if (isStepParamId (id, "arpGate"))                       continue;
        if (id == param::masterGain || id == param::tuningFine)   continue;
        if (id == param::voicing    || id == param::polyMax)      continue;
        if (id == param::bendRange  || id == param::masterHQ)     continue;
        if (id == param::modBank    || id == param::scaleLock)    continue;
        if (id == param::osc1Phase  || id == param::osc2Phase)    continue;
        if (id == param::osc1PRand  || id == param::osc2PRand)    continue;
        if (id == param::pumpSync)                               continue;

        // Booleans get a coin flip: a uniform draw in 0..1 always lands on the
        // "on" side of the 0.5 threshold, which would switch every switch on.
        if (dynamic_cast<juce::AudioParameterBool*> (rp) != nullptr)
        {
            rp->setValueNotifyingHost (rng.nextBool() ? 1.0f : 0.0f);
            continue;
        }

        // Musically biased draws. A uniform value in every cell produces a
        // patch that is mostly unusable: 5-second attacks, full-wet reverb,
        // maximum resonance. Times favour the short end, wet amounts stay
        // moderate, and drive/resonance stay clear of the extremes.
        const float u = rng.nextFloat();
        float n = u;
        if (id == param::filtA || id == param::filtD || id == param::ampA
            || id == param::ampD || id == param::filtR || id == param::ampR)
            n = juce::jmin (u, rng.nextFloat()) * 0.6f;
        else if (id == param::delayMix || id == param::revMix || id == param::chorusMix
                 || id == param::phMix || id == param::ottDepth || id == param::duckAmt
                 || id == param::vowelMix || id == param::revShimmer)
            n = u * 0.5f;
        else if (id == param::drive || id == param::reso || id == param::reso2
                 || id == param::fDrive || id == param::fFeedback)
            n = u * 0.6f;
        else if (id == param::cutoff || id == param::cutoff2)
            n = 0.15f + 0.75f * std::sqrt (u);   // mid-forward, never fully shut

        rp->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, n));
    }

    goaui::repaintCtlPaints (this);
    updateHistoryButtons();
}

void GoaSynthAudioProcessorEditor::rebuildPresetList()
{
    if (presetBrowser == nullptr)
        return;

    const juce::String q = presetBrowser->search.getText().trim();
    const int tagIdx = presetBrowser->tagCombo.getSelectedItemIndex();
    const juce::String wantTag = tagIdx > 0
        ? presetBrowser->tagCombo.getItemText (tagIdx).toLowerCase() : juce::String();
    const int folder = presetBrowser->folder();   // 0 ALL 1 FACTORY 2 USER 3 FAVS
    const int sortMode = presetBrowser->sortMode();

    // Every whitespace-separated term must match the NAME OR THE TAGS, so
    // "acid bass" narrows rather than widens. Search used to look at the name
    // only, which made a patch unfindable by the very tag you gave it.
    juce::StringArray tokens;
    tokens.addTokens (q.toLowerCase(), " ", "");
    tokens.removeEmptyStrings();

    visiblePresets.clear();
    for (const auto& e : allPresets)
    {
        bool ok = true;
        if (folder == 1)
            ok = e.factoryIndex >= 0 || e.name == "Init Patch";   // FACTORY tab
        else if (folder == 2)
            ok = e.isUser;                                        // USER tab
        else if (folder == 3)
            ok = isFavourite (e);                                 // star tab

        if (ok && wantTag.isNotEmpty())
            ok = e.tags.contains (wantTag, true);   // exact tag, not a substring

        if (ok && ! tokens.isEmpty())
        {
            const juce::String hay =
                (e.name + " " + e.tags.joinIntoString (" ")).toLowerCase();
            for (const auto& tok : tokens)
                if (! hay.contains (tok))
                {
                    ok = false;
                    break;
                }
        }

        if (ok)
            visiblePresets.push_back (e);
    }

    // Ordering. The user bank previously had NO order at all: it came straight
    // out of findChildFiles(), i.e. whatever the filesystem returned, which
    // differed between machines and between runs. Banks are kept contiguous so
    // the FACTORY / USER PATCHES headers stay meaningful.
    auto byName = [] (const PresetEntry& a, const PresetEntry& b, bool ascending)
    {
        if (a.isUser != b.isUser)
            return a.isUser < b.isUser;   // factory first, user second
        const int c = a.name.compareIgnoreCase (b.name);
        return ascending ? c < 0 : c > 0;
    };

    switch (sortMode)
    {
        case 1:   // Z-A
            std::stable_sort (visiblePresets.begin(), visiblePresets.end(),
                              [&byName] (const PresetEntry& a, const PresetEntry& b)
                              { return byName (a, b, false); });
            break;
        case 2:   // newest first. Factory patches have no mtime (0), so the user
                  // bank floats to the top - which is where a patch you just
                  // saved belongs - and the two banks stay contiguous.
            std::stable_sort (visiblePresets.begin(), visiblePresets.end(),
                              [] (const PresetEntry& a, const PresetEntry& b)
                              {
                                  if (a.modified != b.modified)
                                      return a.modified > b.modified;
                                  return a.name.compareIgnoreCase (b.name) < 0;
                              });
            break;
        case 3:   // bank order: Init, factory table, then the user bank
            std::stable_sort (visiblePresets.begin(), visiblePresets.end(),
                              [] (const PresetEntry& a, const PresetEntry& b)
                              { return a.bankOrder < b.bankOrder; });
            break;
        case 0:
        default:  // A-Z
            std::stable_sort (visiblePresets.begin(), visiblePresets.end(),
                              [&byName] (const PresetEntry& a, const PresetEntry& b)
                              { return byName (a, b, true); });
            break;
    }

    // Keep the selection valid; stay on the same patch when possible.
    if (selectedPreset >= (int) visiblePresets.size())
        selectedPreset = (int) visiblePresets.size() - 1;

    presetBrowser->setCount ((int) visiblePresets.size(), (int) allPresets.size());

    std::vector<goaui::PresetBrowserOverlay::Row> rows;
    auto addHeader = [&rows] (const juce::String& label, bool user)
    {
        goaui::PresetBrowserOverlay::Row r;
        r.header = true;
        r.label = label;
        r.isUser = user;
        rows.push_back (r);
    };

    int lastGroup = -1;   // -1 none, 0 factory/init, 1 user
    for (size_t i = 0; i < visiblePresets.size(); ++i)
    {
        const auto& e = visiblePresets[i];
        const int group = e.isUser ? 1 : 0;
        if (group != lastGroup)
        {
            addHeader (group == 0 ? "FACTORY" : "USER PATCHES", group == 1);
            lastGroup = group;
        }
        goaui::PresetBrowserOverlay::Row r;
        r.label = e.name;
        r.tags = e.tags;
        r.isUser = e.isUser;
        r.shared = e.shared;
        r.fav = isFavourite (e);
        r.selected = (int) i == selectedPreset;
        r.visibleIndex = (int) i;
        rows.push_back (r);
    }

    // Pass the loaded row's index, not -1: setRows only scrolls when the index
    // is real, so the list used to reopen at the top even with the loaded patch
    // highlighted somewhere far below.
    int selRow = -1;
    for (size_t i = 0; i < rows.size(); ++i)
        if (! rows[i].header && rows[i].selected)
        {
            selRow = (int) i;
            break;
        }
    presetBrowser->setRows (rows, selRow);
}

// Async file chooser for pack import/export. Only one dialog can be alive at a
// time. JUCE requires the FileChooser to outlive its async operation, so the
// callback keeps it alive via a shared_ptr: if the plugin window closes while
// the native dialog is up, the chooser still exists when the dialog returns.
// A SafePointer guards every editor touch so a closed editor can't be touched.
//==============================================================================
// WAV -> wavetable. The file is cut into `numFrames` equal slices and each
// slice is linearly resampled to the table's 256 points, then peak-normalised
// (a raw recording is almost never at a useful level for a wavetable). The
// oscillator is switched to its User wave so the import is audible at once.
void GoaSynthAudioProcessorEditor::loadWavIntoWavetable (int oscIndex)
{
    const juce::String osc = oscIndex == 0 ? "A" : "B";
    const bool opened = asyncFileChooser ("Load a WAV into OSC " + osc + " wavetable",
        "*.wav;*.aif;*.aiff;*.flac",
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, oscIndex] (const juce::File& f)
        {
            if (f != juce::File())
                importWavFile (f, oscIndex);
        });

    if (! opened)
        reportPackStatus ("Close the open file dialog first.", false);
}

void GoaSynthAudioProcessorEditor::importWavFile (const juce::File& f, int oscIndex)
{
    if (! f.existsAsFile())
        return;

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (f));
    if (reader == nullptr || reader->lengthInSamples <= 0)
    {
        reportPackStatus ("Not a readable audio file: " + f.getFileName(), false);
        return;
    }

    auto* w = proc.wavetable (oscIndex);
    if (w == nullptr)
        return;

    // Cap the read at 16 M samples: a 20-minute file would otherwise allocate
    // a large buffer just to throw 99% of it away.
    const int n = (int) juce::jmin (reader->lengthInSamples, (juce::int64) (1 << 24));
    juce::AudioBuffer<float> src (1, n);
    reader->read (&src, 0, n, 0, true, false);
    const float* x = src.getReadPointer (0);

    constexpr int frames = goa::UserWave::numFrames;
    constexpr int pts    = goa::UserWave::size;
    const int per = juce::jmax (1, n / frames);

    w->beginEdit();
    std::array<float, (size_t) pts> buf {};
    for (int fr = 0; fr < frames; ++fr)
    {
        const int start = juce::jlimit (0, juce::jmax (0, n - 1), fr * per);
        const int len   = juce::jlimit (1, n - start, per);
        for (int i = 0; i < pts; ++i)
        {
            const float pos = (float) i / (float) pts * (float) len;
            const int   i0  = juce::jlimit (0, len - 1, (int) pos);
            const int   i1  = juce::jmin (len - 1, i0 + 1);
            const float fr2 = pos - (float) i0;
            buf[(size_t) i] = x[start + i0] + (x[start + i1] - x[start + i0]) * fr2;
        }
        w->setFrame (fr, buf.data(), pts);
        w->normalizeFrame (fr);
    }
    proc.commitWaves();   // publishes the edit surface + rebuilds mips

    // Wave choice 5 is "User"; without this the import would be invisible (and
    // silent) until the user found the combo.
    if (auto* p = proc.apvts.getParameter (oscIndex == 0 ? param::osc1Wave : param::osc2Wave))
        p->setValueNotifyingHost (p->convertTo0to1 (5.0f));
    if (auto* p = proc.apvts.getParameter (oscIndex == 0 ? param::osc1WtPos : param::osc2WtPos))
        p->setValueNotifyingHost (0.5f);
    if (auto* p = proc.apvts.getParameter (oscIndex == 0 ? param::osc1Level : param::osc2Level))
        if (p->getValue() < 0.05f)
            p->setValueNotifyingHost (p->convertTo0to1 (0.8f));

    reportPackStatus ("Loaded " + f.getFileName() + " into OSC "
                          + (oscIndex == 0 ? "A" : "B"), true);
}

bool GoaSynthAudioProcessorEditor::asyncFileChooser (const juce::String& title,                                                     const juce::String& wildcards,
                                                     int browserFlags,
                                                     std::function<void (const juce::File&)> onChosen)
{
    if (packChooser != nullptr)
        return false; // a dialog is already open

    auto chooser = std::make_shared<juce::FileChooser> (title,
                                                        userpresets::presetsDir(), wildcards);
    packChooser = chooser;

    juce::Component::SafePointer<GoaSynthAudioProcessorEditor> safeThis { this };
    chooser->launchAsync (browserFlags,
        [safeThis, chooser, onChosen] (const juce::FileChooser& fc)
    {
        const auto result = fc.getResult();
        if (safeThis == nullptr)
            return;                     // editor closed - nothing to update
        safeThis->packChooser = nullptr; // allow the next dialog
        onChosen (result);
    });
    return true;
}

void GoaSynthAudioProcessorEditor::exportPresetPack()
{
    auto doExport = [this] (const juce::File& chosen)
    {
        auto files = userpresets::scanPresets();
        if (files.isEmpty())
        {
            reportPackStatus ("Nothing to export - save some presets first.", false);
            return;
        }
        reportPackStatus ("Exporting " + juce::String (files.size()) + " presets...", true);
        const bool ok = userpresets::exportPack (chosen, files);
        reportPackStatus (ok ? "Exported " + juce::String (files.size())
                                   + (files.size() == 1 ? " preset" : " presets")
                                   + " to " + chosen.getFileName()
                             : "Export failed - could not write the pack.", ok);
    };

    const bool opened = asyncFileChooser ("Export preset pack (.goapack)",
        "*." + juce::String (userpresets::packExtension),
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this, doExport] (const juce::File& raw)
    {
        if (raw == juce::File())
            return; // user cancelled
        const auto chosen = userpresets::withPackExtension (raw);
        if (! chosen.existsAsFile())
        {
            doExport (chosen);
            return;
        }
        // Overwrite confirmation, native and async.
        if (! juce::NativeMessageBox::showOkCancelBox (
                juce::MessageBoxIconType::QuestionIcon, "Overwrite pack?",
                "\"" + chosen.getFileName() + "\" already exists.\nReplace it?",
                nullptr, juce::ModalCallbackFunction::create ([doExport, chosen] (int r)
                { if (r != 0) doExport (chosen); })))
        {
            doExport (chosen); // native box unavailable - proceed
        }
    });

    if (opened)
        reportPackStatus ("", true); // clear stale status while the dialog is up
    else
        reportPackStatus ("A file dialog is already open.", false);
}

void GoaSynthAudioProcessorEditor::importPresetPack()
{
    const bool opened = asyncFileChooser ("Import preset pack (.goapack)",
        "*." + juce::String (userpresets::packExtension),
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::File& chosen)
    {
        if (! chosen.existsAsFile())
            return; // user cancelled
        reportPackStatus ("Importing pack...", true);

        auto r = userpresets::importPack (chosen, userpresets::presetsDir(), false);
        if (r.error.isNotEmpty())
        {
            reportPackStatus (r.error, false);
            return;
        }
        refreshUserPresets();

        juce::String msg = "Imported " + juce::String (r.imported)
                         + (r.imported == 1 ? " preset" : " presets");
        if (r.skipped > 0)
            msg << " (" << r.skipped << " already in bank - skipped)";
        reportPackStatus (msg, r.imported > 0);
    });

    if (opened)
        reportPackStatus ("", true);
    else
        reportPackStatus ("A file dialog is already open.", false);
}

void GoaSynthAudioProcessorEditor::reportPackStatus (const juce::String& text, bool ok)
{
    if (presetBrowser != nullptr)
        presetBrowser->setStatus (text, ok);
}

void GoaSynthAudioProcessorEditor::refreshUserPresets()
{
    allPresets.clear();

    PresetEntry init;
    init.name = "Init Patch";
    allPresets.push_back (init);

    const auto& fTags = getFactoryTags();
    for (size_t i = 0; i < getPresets().size(); ++i)
    {
        PresetEntry e;
        e.name = getPresets()[i].name;
        e.factoryIndex = (int) i;
        if (auto it = fTags.find (e.name); it != fTags.end())
            e.tags = it->second;
        allPresets.push_back (std::move (e));
    }

    for (const auto& f : userpresets::scanPresets())
    {
        PresetEntry e;
        e.name = userpresets::displayName (f);
        e.isUser = true;
        e.shared = userpresets::isSharedPreset (f);
        e.file = f;
        e.tags = userpresets::readTags (f);
        e.modified = f.getLastModificationTime().toMilliseconds();   // for SORT: NEWEST
        allPresets.push_back (std::move (e));
    }

    // Stable position, so "BANK ORDER" is a real order rather than a hope.
    for (size_t i = 0; i < allPresets.size(); ++i)
        allPresets[i].bankOrder = (int) i;

    // Rebuild the tag dropdown from every tag in the bank (factory + user).
    juce::StringArray allTags;
    for (const auto& e : allPresets)
        allTags.addArray (e.tags);
    allTags.removeDuplicates (true);
    allTags.sort (true);
    if (presetBrowser != nullptr)
    {
        auto& tagCombo = presetBrowser->tagCombo;
        const int prevTag = tagCombo.getSelectedItemIndex();
        tagCombo.clear (juce::dontSendNotification);
        tagCombo.addItem ("TAG: ALL", 1);
        for (int i = 0; i < allTags.size(); ++i)
            tagCombo.addItem (allTags[i].toUpperCase(), i + 2);
        tagCombo.setSelectedItemIndex (juce::jlimit (0, tagCombo.getNumItems() - 1,
                                                     prevTag < 0 ? 0 : prevTag),
                                       juce::dontSendNotification);
    }

    rebuildPresetList();
}

// ---- browser state: favourites + last filter -------------------------------

void GoaSynthAudioProcessorEditor::loadBrowserState()
{
    browserState = userpresets::loadBrowserState();
    if (presetBrowser != nullptr)
        presetBrowser->applyState (browserState.folder, browserState.sortMode,
                                   browserState.tag, browserState.search);
}

void GoaSynthAudioProcessorEditor::persistBrowserState()
{
    if (presetBrowser != nullptr)
    {
        browserState.folder = presetBrowser->folder();
        browserState.sortMode = presetBrowser->sortMode();
        browserState.search = presetBrowser->search.getText().trim();
        const int tagIdx = presetBrowser->tagCombo.getSelectedItemIndex();
        browserState.tag = tagIdx > 0
            ? presetBrowser->tagCombo.getItemText (tagIdx) : juce::String();
    }
    userpresets::saveBrowserState (browserState);
}

bool GoaSynthAudioProcessorEditor::isFavourite (const PresetEntry& e) const
{
    return browserState.favourites.contains (
        userpresets::favouriteKey (e.isUser, e.name, e.file));
}

void GoaSynthAudioProcessorEditor::toggleFavourite (int visIdx)
{
    if (! juce::isPositiveAndBelow (visIdx, (int) visiblePresets.size()))
        return;
    const auto& e = visiblePresets[(size_t) visIdx];
    const auto key = userpresets::favouriteKey (e.isUser, e.name, e.file);

    if (browserState.favourites.contains (key))
        browserState.favourites.removeString (key);
    else
        browserState.favourites.add (key);

    persistBrowserState();
    rebuildPresetList();
}

// Rename / duplicate / delete / reveal / export for one row. The overlay reports
// the gesture; the policy lives here.
void GoaSynthAudioProcessorEditor::showRowMenu (int visIdx, juce::Point<int> screenPos)
{
    if (! juce::isPositiveAndBelow (visIdx, (int) visiblePresets.size()))
        return;

    // Copy what the menu needs: visiblePresets can be rebuilt while the menu is
    // open (a favourite toggle or an import would do it), which would dangle
    // any reference into the vector.
    const bool isUser = visiblePresets[(size_t) visIdx].isUser;
    const bool hasFile = visiblePresets[(size_t) visIdx].file.existsAsFile();
    const juce::String name = visiblePresets[(size_t) visIdx].name;
    const juce::StringArray tags = visiblePresets[(size_t) visIdx].tags;
    const juce::File file = visiblePresets[(size_t) visIdx].file;

    juce::PopupMenu m;
    m.addItem (1, "Load");
    m.addSeparator();
    m.addItem (2, "Rename...", isUser && hasFile);
    m.addItem (3, "Duplicate...");
    m.addItem (4, "Delete", isUser && hasFile);
    m.addSeparator();
    m.addItem (5, "Reveal in Explorer", isUser && hasFile);
    m.addItem (6, "Export this preset...");

    if (! tags.isEmpty())
    {
        juce::PopupMenu tagMenu;
        for (int i = 0; i < tags.size(); ++i)
            tagMenu.addItem (100 + i, tags[i].toUpperCase());
        m.addSeparator();
        m.addSubMenu ("Filter by tag", tagMenu);
    }

    juce::Component::SafePointer<GoaSynthAudioProcessorEditor> safeThis { this };
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (
                         juce::Rectangle<int> (screenPos.x, screenPos.y, 1, 1)),
        [safeThis, visIdx, isUser, name, tags, file] (int result)
        {
            if (safeThis == nullptr || result == 0)
                return;

            if (result == 1)
            {
                safeThis->applyPreset (visIdx);
                if (safeThis->presetBrowser != nullptr)
                    safeThis->presetBrowser->setVisible (false);
            }
            else if (result == 2)   // rename
            {
                if (safeThis->saveOverlay == nullptr)
                    return;
                safeThis->renameSource = file;
                safeThis->saveOverlay->beginRename (name, tags);
                safeThis->saveOverlay->setBounds (safeThis->getLocalBounds());
                safeThis->saveOverlay->setVisible (true);
            }
            else if (result == 3)   // duplicate
            {
                if (safeThis->saveOverlay == nullptr)
                    return;
                safeThis->renameSource = juce::File();   // a copy, not a rename
                safeThis->saveOverlay->beginSaveAs (name + " copy", tags);
                safeThis->saveOverlay->setBounds (safeThis->getLocalBounds());
                safeThis->saveOverlay->setVisible (true);
            }
            else if (result == 4)   // delete, confirmed
            {
                if (! juce::NativeMessageBox::showOkCancelBox (
                        juce::MessageBoxIconType::QuestionIcon, "Delete preset?",
                        "\"" + name + "\" will be deleted from your preset bank.\n"
                        "This cannot be undone.", nullptr, juce::ModalCallbackFunction::create (
                            [safeThis, file] (int r)
                            {
                                if (r == 0 || safeThis == nullptr)
                                    return;
                                userpresets::deletePreset (file);
                                safeThis->refreshUserPresets();
                                safeThis->updatePresetTint();
                            })))
                {
                    // Native box unavailable: fall through and delete.
                    userpresets::deletePreset (file);
                    safeThis->refreshUserPresets();
                    safeThis->updatePresetTint();
                }
            }
            else if (result == 5)   // reveal
            {
                file.revealToUser();
            }
            else if (result == 6)   // export one preset
            {
                safeThis->exportSinglePreset (visIdx);
            }
            else if (result >= 100)
            {
                // Filter by the tag that was clicked.
                const int ti = result - 100;
                if (juce::isPositiveAndBelow (ti, tags.size())
                    && safeThis->presetBrowser != nullptr)
                {
                    safeThis->presetBrowser->setTagText (tags[ti]);
                    safeThis->rebuildPresetList();
                    safeThis->persistBrowserState();
                }
            }
        });
}

void GoaSynthAudioProcessorEditor::renameUserPreset (const juce::File& from,
                                                     const juce::String& newNameIn,
                                                     const juce::StringArray& tags)
{
    if (saveOverlay != nullptr)
        saveOverlay->setVisible (false);

    const juce::String newName = newNameIn.trim();
    if (! from.existsAsFile() || newName.isEmpty())
        return;

    const auto to = userpresets::fileForName (newName);
    auto xml = juce::parseXML (from);
    if (xml == nullptr)
        return;

    // savePresetTo strips any old PRESETINFO and writes the new tags, so this is
    // also the retag path when the name is unchanged.
    if (! userpresets::savePresetTo (to, *xml, tags))
        return;
    if (to != from)
        from.deleteFile();

    // A user patch's identity is its file name, so a rename moves its star
    // across - otherwise renaming would silently un-favourite it.
    const auto oldKey = juce::String ("u:") + from.getFileName();
    const auto newKey = juce::String ("u:") + to.getFileName();
    if (oldKey != newKey && browserState.favourites.contains (oldKey))
    {
        browserState.favourites.removeString (oldKey);
        browserState.favourites.addIfNotAlreadyThere (newKey);
        persistBrowserState();
    }

    refreshUserPresets();

    // Land on the renamed patch so the user sees what just happened.
    const auto wanted = to.getFileNameWithoutExtension();
    for (size_t i = 0; i < visiblePresets.size(); ++i)
        if (visiblePresets[i].isUser
            && visiblePresets[i].file.getFileNameWithoutExtension() == wanted)
        {
            selectedPreset = (int) i;
            presetName.setText (visiblePresets[i].name, juce::dontSendNotification);
            break;
        }
    updatePresetTint();
}

void GoaSynthAudioProcessorEditor::exportSinglePreset (int visIdx)
{
    if (! juce::isPositiveAndBelow (visIdx, (int) visiblePresets.size()))
        return;

    const juce::String suggested = userpresets::safeFileName (visiblePresets[(size_t) visIdx].name);
    const juce::File source = visiblePresets[(size_t) visIdx].file;
    const bool isUser = visiblePresets[(size_t) visIdx].isUser;

    const bool opened = asyncFileChooser ("Export preset (.goapreset)",
        "*." + juce::String (userpresets::presetExtension),
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this, visIdx, source, isUser] (const juce::File& raw)
    {
        if (raw == juce::File())
            return;   // cancelled
        const auto dest = raw.hasFileExtension (userpresets::presetExtension)
                              ? raw : raw.withFileExtension (userpresets::presetExtension);

        if (isUser && source.existsAsFile())
        {
            // User patch: copy the file verbatim, so the tags travel with it.
            if (source.copyFileTo (dest))
                reportPackStatus ("Exported " + dest.getFileName(), true);
            else
                reportPackStatus ("Could not write " + dest.getFileName(), false);
            return;
        }

        // Factory patch: it has no file, so materialise the sound. This loads the
        // patch (that is what makes it serialisable) and then writes the state.
        applyPreset (visIdx);
        auto xml = proc.stateToXml();
        if (xml == nullptr || ! dest.replaceWithText (xml->toString()))
            reportPackStatus ("Could not write " + dest.getFileName(), false);
        else
            reportPackStatus ("Exported " + dest.getFileName(), true);
    });

    if (! opened)
        reportPackStatus ("A file dialog is already open.", false);
}

void GoaSynthAudioProcessorEditor::saveUserPreset (const juce::String& nameIn,
                                                   const juce::StringArray& tagsIn,
                                                   bool shared)
{
    if (saveOverlay != nullptr)
        saveOverlay->setVisible (false);

    const juce::String name = nameIn.trim();
    if (name.isEmpty())
        return;

    const auto xml = proc.stateToXml();
    const bool ok = shared ? userpresets::saveSharedPreset (name, *xml, tagsIn)
                           : userpresets::savePreset (name, *xml, tagsIn);
    if (xml == nullptr || ! ok)
        return;

    refreshUserPresets();

    const juce::String wanted = userpresets::safeFileName (name);
    for (size_t i = 0; i < visiblePresets.size(); ++i)
        if (visiblePresets[i].isUser
            && visiblePresets[i].file.getFileNameWithoutExtension() == wanted)
        {
            selectedPreset = (int) i;
            presetName.setText (visiblePresets[i].name, juce::dontSendNotification);
            break;
        }
    updatePresetTint();
}


void GoaSynthAudioProcessorEditor::updatePresetTint()
{
    const bool isUser = juce::isPositiveAndBelow (selectedPreset, (int) visiblePresets.size())
                        && visiblePresets[(size_t) selectedPreset].isUser;
    presetName.setColour (juce::Label::textColourId, isUser ? goaui::accentA : goaui::accent);
    updateArrows();
}

void GoaSynthAudioProcessorEditor::updateArrows()
{
    // Deletion lives in the preset browser's context menu now (right-click a
    // user preset there); the header has no DEL button any more.
}

void GoaSynthAudioProcessorEditor::applyAiPatch (const juce::String& brief, int variation,
                                                 int engine, const juce::String& credential,
                                                 int modelIdx)
{
    if (brief.trim().isEmpty() || aiOverlay == nullptr)
        return;

    // Local engine: the offline designer is instant — run it inline.
    if (engine <= 0)
    {
        const auto local = AiPatchGen::generate (brief,
            variation <= 0 ? AiPatchGen::Var::Subtle
          : variation >= 2 ? AiPatchGen::Var::Wild
                           : AiPatchGen::Var::Normal);
        AiCloudGen::Response res;
        res.ok = true;
        res.title = local.title;
        res.description = local.description;
        res.patch = local.patch;

        // Learned-memory augmentation: blend the offline result with the
        // best-matching archived cloud successes so LOCAL keeps improving.
        const auto matches = learned::bestMatch (brief, 2);
        if (! matches.empty())
        {
            float blend = 0.45f;
            for (const auto& m : matches)
            {
                for (const auto& [id, v] : m.patch)
                    res.patch[id] = res.patch.count (id) == 0
                        ? v : (res.patch[id] + v * blend) / (1.0f + blend);
                blend *= 0.5f;
            }
            if (res.description.isNotEmpty())
                res.description << "  +";
        }
        applyAiPatchResult (res);
        return;
    }

    // Cloud engines run on a background thread so the UI never blocks on the
    // network; the result hops back to the message thread via a lambda.
    using E = AiCloudGen::Engine;
    const auto eng = engine == 1 ? E::Gemini : engine == 2 ? E::OpenAI : E::Custom;

    if (! AiCloudGen::credentialLooksValid (eng, credential))
    {
        aiOverlay->showError ("paste a valid API key first, then press GENERATE");
        return;
    }

    const auto models = AiCloudGen::modelNames (eng);
    const juce::String model = models.isEmpty() ? juce::String()
                               : models[juce::jlimit (0, models.size() - 1, modelIdx)];

    aiOverlay->setBusy (true);
    juce::Component::SafePointer<GoaSynthAudioProcessorEditor> safe (this);
    juce::Component::SafePointer<goaui::AiOverlay> safeOverlay (aiOverlay.get());

    juce::Thread::launch ([safe, safeOverlay, brief, variation, eng, credential, model]()
    {
        auto res = AiCloudGen::generate (brief, eng, credential, variation, model);

        // Successes are archived for the offline designer's learned memory
        // (best-effort, on this background thread — never blocks the UI).
        if (res.ok)
            learned::record (brief, res.title, res.patch,
                             eng == AiCloudGen::Engine::Gemini ? "GEMINI"
                           : eng == AiCloudGen::Engine::OpenAI ? "OPENAI"
                                                               : "CUSTOM",
                             model);

        juce::MessageManager::callAsync ([safe, safeOverlay, res]()
        {
            if (safe == nullptr || safeOverlay == nullptr)
                return;
            safeOverlay->setBusy (false);
            if (! res.ok)
            {
                safeOverlay->showError ("AI: " + res.error);
                return;
            }
            safe->applyAiPatchResult (res);
        });
    });
}

// Applies a finished AI result (local or cloud) to the synth: resets all
// parameters to defaults, layers the patch values on top, refreshes the UI.
void GoaSynthAudioProcessorEditor::applyAiPatchResult (const AiCloudGen::Response& res)
{
    auto& apvts = proc.apvts;
    for (auto* par : proc.getParameters())
        par->setValueNotifyingHost (par->getDefaultValue());
    for (const auto& [id, v] : res.patch)
        if (auto* par = apvts.getParameter (id))
            par->setValueNotifyingHost (par->convertTo0to1 (v));

    selectedPreset = -1;
    presetName.setText ("AI PATCH", juce::dontSendNotification);
    updatePresetTint();

    aiOverlay->showResult (res.title, res.description);
}

void GoaSynthAudioProcessorEditor::paint (juce::Graphics& g)
{
    // The output level drives the *backdrop* (built by the timer), not the paint
    // pass, so nothing here reads it any more.

    // Cached psychedelic swirl backdrop (half resolution, composited once per
    // animation frame by the Timer), drawn stretched over the whole window.
    if (backdrop.isValid())
        g.drawImageTransformed (backdrop,
            juce::AffineTransform::scale (
                (float) getWidth()  / backdrop.getWidth(),
                (float) getHeight() / backdrop.getHeight()));

    // Base gradient + theme glows ride on top, keeping the mood even when idle.
    juce::ColourGradient bg (goaui::bgTop, 0.0f, 0.0f,
                             goaui::bgBottom, 0.0f, (float) getHeight(), false);
    g.setGradientFill (bg);
    g.fillAll();

    // UV party glows: teal top-right, violet left, magenta near the bottom.
    auto glow = [&g] (juce::Point<float> c, float radius, juce::Colour col)
    {
        juce::ColourGradient grad (col.withAlpha (0.11f), c.x, c.y,
                                   col.withAlpha (0.0f), radius, radius, true);
        g.setGradientFill (grad);
        g.fillEllipse (c.x - radius, c.y - radius, radius * 2.0f, radius * 2.0f);
    };
    const float t = (float) animClock;
    const float sway = std::sin (t * 0.35f) * 18.0f;
    glow ({ (float) getWidth() * 0.88f,  40.0f },                    230.0f, goaui::accentA);
    glow ({ 60.0f + sway,               (float) getHeight() * 0.42f }, 260.0f, goaui::accent);
    glow ({ (float) getWidth() * 0.62f, (float) getHeight() - 60.0f }, 200.0f, goaui::accentB);

    // Grain texture: deterministic pseudo-random dots at 2 % density, very
    // low alpha so it adds texture without becoming noise. The hash makes it
    // stable frame-to-frame with no state needed.
    {
        g.setColour (goaui::textBright.withAlpha (0.018f));
        const int gw = getWidth() / 4;
        const int gh = getHeight() / 4;
        for (int gy = 0; gy < gh; ++gy)
        {
            for (int gx = 0; gx < gw; ++gx)
            {
                const uint32_t h = (uint32_t) (gx * 73856093u ^ gy * 19349663u);
                if ((h & 0x1F) == 0)   // 1 in 32 chance
                {
                    const int x = gx * 4 + (int) ((h >> 5) & 3);
                    const int y = gy * 4 + (int) ((h >> 7) & 3);
                    g.fillRect (x, y, 1, 1);
                }
            }
        }
    }

    g.setColour (goaui::bgHeader);
    g.fillRect (0, 0, getWidth(), 56);
    g.fillRect (0, getHeight() - 110, getWidth(), 110);

    // Goa accent seams on the header / bottom bars.
    g.setGradientFill (goaui::uvGradient (
        { 0.0f, 51.0f, (float) getWidth(), 2.0f }));
    g.fillRect (0, 54, getWidth(), 2);
    g.setColour (goaui::accentB.withAlpha (0.45f));
    g.fillRect (0, getHeight() - 112, getWidth(), 2);

    g.setColour (goaui::border);
    g.drawHorizontalLine (56, 0.0f, (float) getWidth());
    g.drawHorizontalLine (getHeight() - 110, 0.0f, (float) getWidth());
}

void GoaSynthAudioProcessorEditor::resized()
{
    auto b = getLocalBounds();
    // The bottom bar grew from 110 to 140 to hold the master EQ as a fourth
    // right-hand row. The upper rows absorb it: at the default 780px window
    // they land at 262 / 186 / 116, all still above their floors.
    auto bottom = b.removeFromBottom (140);
    auto header = b.removeFromTop (56);
    auto content = b.reduced (6, 4);
    const int gap = 6;

    // ---- header ----
    logoGoa.setBounds (12, 6, 56, 28);
    logoSynth.setBounds (70, 6, 90, 28);
    tagline.setBounds (12, 34, 148, 12);

    // Full-surface overlays must track a live window resize: the license
    // screen does this inside showLicenseOverlay() itself, and the others
    // only get bounds at open time. Fixed-size days made this moot.
    if (aiOverlay != nullptr && aiOverlay->isVisible())
        aiOverlay->setBounds (getLocalBounds());
    if (modOverlay != nullptr && modOverlay->isVisible())
        modOverlay->setBounds (getLocalBounds());
    if (saveOverlay != nullptr && saveOverlay->isVisible())
        saveOverlay->setBounds (getLocalBounds());
    if (aboutOverlay != nullptr && aboutOverlay->isVisible())
        aboutOverlay->setBounds (getLocalBounds());
    if (updateResultOverlay != nullptr && updateResultOverlay->isVisible())
        updateResultOverlay->setBounds (getLocalBounds());
    if (presetBrowser != nullptr && presetBrowser->isVisible())
        presetBrowser->setBounds (getLocalBounds());

    // Reserve must fit all buttons (panic 56 + theme 62 + menu 62 + ai 46 +
    // mod 42 + save 46 + 5x2 spacers = 324) plus the trial badge (100), MASTER
    // (72) and the level meter (84 + 8 gap). MASTER is the rightmost control;
    // the meter sits directly beside it where a level readout belongs.
    auto right = header.removeFromRight (588);
    // MASTER at the absolute right edge.
    masterCtl->setBounds (right.removeFromRight (72).reduced (6, 2));
    // Level meter beside MASTER (anchored from the right so it never slides
    // under the buttons or the preset name on narrow windows).
    {
        const int meterW = 84;
        levelMeter.setBounds (right.removeFromRight (meterW + 8).removeFromLeft (meterW)
                                  .reduced (0, 9).withTrimmedBottom (0));
    }
    right.removeFromRight (2);
    trialBadge.setBounds (right.removeFromRight (100).reduced (2, 12));
    panicBtn.setBounds (right.removeFromRight (56).reduced (4, 16));
    right.removeFromRight (2);
    themeBtn.setBounds (right.removeFromRight (62).reduced (2, 16));
    right.removeFromRight (2);
    menuBtn.setBounds (right.removeFromRight (62).reduced (2, 16));
    right.removeFromRight (2);
    aiBtn.setBounds (right.removeFromRight (46).reduced (2, 16));
    right.removeFromRight (2);
    modBtn.setBounds (right.removeFromRight (42).reduced (2, 16));
    right.removeFromRight (2);
    saveBtn.setBounds (right.removeFromRight (46).reduced (2, 16));
    right.removeFromRight (2);

    // The old 104px right trim reserved the gap before the meter; the meter
    // now lives inside the button block, so only a small gap is needed.
    auto presetZone = header.withTrimmedLeft (280).withTrimmedRight (8).reduced (0, 13);
    nextBtn.setBounds (presetZone.removeFromRight (30).reduced (2, 0));
    prevBtn.setBounds (presetZone.removeFromLeft (30).reduced (2, 0));
    browseBtn.setBounds (presetZone.removeFromRight (64).reduced (4, 0));
    presetName.setBounds (presetZone.reduced (4, 0));


    // ---- rows ----
    // Vertical budget. The header, the bottom bar and the padding are fixed,
    // and the three panel rows share what is left. The design heights are
    // 276 / 196 / 122 at the default 780px window; below that the two upper
    // rows scale down towards a floor and the FX row keeps a floor of its own.
    //
    // The rows used to be fixed at 276 / 196 with row 3 taking "the rest",
    // which drove row 3 NEGATIVE below 658px: the FX frames got a negative
    // height, and NOISE's LEVEL knob (a hardcoded 66px tall, centred on the
    // degenerate rect) was pushed up over the bottom bar, landing on top of
    // POLY / PORTA / BEND. Found by the layout sweep in OverlayTest.
    constexpr int design1 = 276, design2 = 196, design3 = 122;
    constexpr int floor1  = 250, floor2  = 170, floor3  = 114;

    const int avail = content.getHeight() - 2 * gap;
    int row1H = juce::jlimit (floor1, design1,
                              juce::roundToInt (avail * (float) design1
                                                / (float) (design1 + design2 + design3)));
    int row2H = juce::jlimit (floor2, design2,
                              juce::roundToInt (avail * (float) design2
                                                / (float) (design1 + design2 + design3)));
    int row3H = avail - row1H - row2H;

    // Row 3 is the one that must never be squeezed: give it its floor back by
    // shaving the upper rows (which cannot go below their own floors).
    if (row3H < floor3)
    {
        int shortfall = floor3 - row3H;
        const int take1 = juce::jmin (shortfall, row1H - floor1);
        row1H -= take1;
        shortfall -= take1;
        row2H -= juce::jmin (shortfall, row2H - floor2);
        row3H = avail - row1H - row2H;
    }

    auto row1 = content.removeFromTop (row1H);
    content.removeFromTop (gap);
    auto row2 = content.removeFromTop (row2H);
    content.removeFromTop (gap);
    auto row3 = content.removeFromTop (row3H);

    // ROW 1 : OSC A | OSC B | FILTER | SUB
    auto sideCol = row1.removeFromRight (92);
    subFrame.setBounds (sideCol);
    {
        auto inner = subFrame.getBounds().reduced (7, 24);
        subWaveCtl->setBounds (inner.removeFromTop (20));
        inner.removeFromTop (2);
        // SYNC (hard sync: OSC A reset by OSC B) rides the bottom of the panel
        // as a full-width switch strip. A toggle needs no readout width, and
        // this narrow panel has no room for a fifth knob cell.
        syncCtl->setBounds (inner.removeFromBottom (30));
        inner.removeFromBottom (2);
        // 2x2 knob grid: output (LEVEL, OCT) on top, character (CHAR, CHORD)
        // below — the combos need no readout width, so the narrow panel holds
        // four cells comfortably.
        auto band = inner.withSizeKeepingCentre (inner.getWidth(), juce::jmin (110, inner.getHeight()));
        const int kh = band.getHeight() / 2;
        const int kw = band.getWidth() / 2;
        auto kr1 = band.removeFromTop (kh);
        subCtl->setBounds    (kr1.removeFromLeft (kw).reduced (1));
        subOctCtl->setBounds (kr1.reduced (1));
        charCtl->setBounds   (band.removeFromLeft (kw).reduced (1));
        chordCtl->setBounds  (band.reduced (1));
    }

    // Row-1 seams are all the same explicit gap, removed between panels:
    // OSC A | gap | OSC B | gap | FILTER B | gap | FILTER | gap | SUB.
    row1.removeFromRight (gap);
    filtFrame.setBounds (row1.removeFromRight (294));
    {
        auto inner = filtFrame.getBounds().reduced (9, 24);
        ftypeCtl->setBounds (inner.removeFromTop (20));
        inner.removeFromTop (3);
        filtCurve.setBounds (inner.removeFromTop (94));
        inner.removeFromTop (3);
        // Six knobs as a 3x2 grid: two rows of three, exactly like the OSC
        // panels' two-rows-of-cells rhythm — same cell size, so the same big
        // knob diameter instead of the old 3-row squeeze (18px rotaries).
        // Row order = signal/character order: CUTOFF RESO KEY on top, DRIVE
        // F-DRIVE F-FB below. F-DRIVE / F-FB: documented filter-character
        // knobs, but they were never given bounds here, so they rendered at
        // width zero (invisible) — found by the OverlayTest extremes sweep.
        auto knobs = inner;
        constexpr int kn = 3;
        const int kw = knobs.getWidth() / kn;
        const int kh = knobs.getHeight() / 2;   // two knob rows
        auto kr1 = knobs.removeFromTop (kh);
        auto kr2 = knobs;
        cutoffCtl->setBounds (kr1.removeFromLeft (kw).reduced (1));
        resoCtl->setBounds   (kr1.removeFromLeft (kw).reduced (1));
        keyCtl->setBounds    (kr1.reduced (1));
        driveCtl->setBounds  (kr2.removeFromLeft (kw).reduced (1));
        fdCtl->setBounds     (kr2.removeFromLeft (kw).reduced (1));
        fbCtl->setBounds     (kr2.reduced (1));
    }

    row1.removeFromRight (gap);
    filt2Frame.setBounds (row1.removeFromRight (148));
    {
        auto inner = filt2Frame.getBounds().reduced (9, 24);
        ftype2Ctl->setBounds (inner.removeFromTop (20));
        inner.removeFromTop (3);
        routeCtl->setBounds (inner.removeFromTop (20));
        inner.removeFromTop (3);

        auto band = inner.removeFromTop (96);
        const int kw = band.getWidth() / 2;
        cutoff2Ctl->setBounds (band.removeFromLeft (kw).reduced (1));
        reso2Ctl->setBounds (band);
        inner.removeFromTop (2);

        // Vowel/formant filter (post FILTER B): on toggle + morph/res/mix.
        vOnCtl->setBounds (inner.removeFromTop (16).withSizeKeepingCentre (84, 15));
        inner.removeFromTop (2);
        const int vw = inner.getWidth() / 3;
        vMorphCtl->setBounds (inner.removeFromLeft (vw).reduced (1));
        vResCtl->setBounds   (inner.removeFromLeft (vw).reduced (1));
        vMixCtl->setBounds   (inner.reduced (1));
    }

    // Same explicit gap as everywhere else in the row, reserved from the right
    // BEFORE the OSC pair is sized, so OSC B ends one gap short of FILTER B
    // (it used to run flush against it — an invisible 0px seam).
    row1.removeFromRight (gap);
    const int oscW = (row1.getWidth() - gap) / 2;
    oscAFrame.setBounds (row1.removeFromLeft (oscW));
    row1.removeFromLeft (gap);
    oscBFrame.setBounds (row1);

    auto layoutOsc = [] (goaui::Panel& frame, goaui::WaveDisplay& disp, juce::Component* waveCombo,
                         const std::vector<juce::Component*>& row1Cells,
                         const std::vector<juce::Component*>& row2Cells)
    {
        auto inner = frame.getBounds().reduced (9, 24);
        waveCombo->setBounds (inner.removeFromTop (20));
        inner.removeFromTop (3);
        disp.setBounds (inner.removeFromTop (94));
        inner.removeFromTop (3);
        auto knobs = inner;
        const int kh = knobs.getHeight() / 2;
        auto kr1 = knobs.removeFromTop (kh);
        auto kr2 = knobs;
        // Rows are split by cell COUNT so every knob in a row gets an equal
        // share. Row 1 is the same six cells for both oscillators (pitch pair
        // OCT+FIN, unison trio UNI/DET/WID, stereo PAN). Row 2 anchors output
        // (LEVEL, PHASE) and then each panel's own tail: OSC A carries the PWM
        // duty, OSC B carries FM > A and RING. Equal-per-row beats a fixed-6
        // grid — no empty trailing slots and no cell on a reduced sliver.
        auto rowCells = [] (juce::Rectangle<int> r, const std::vector<juce::Component*>& cs)
        {
            const int n = (int) cs.size();
            if (n == 0)
                return;
            const int w = r.getWidth() / n;
            for (auto* c : cs)
                c->setBounds (r.removeFromLeft (w).reduced (1));
        };
        rowCells (kr1, row1Cells);
        rowCells (kr2, row2Cells);
    };

    layoutOsc (oscAFrame, waveA, wave1Ctl.get(),
               { oct1Ctl.get(), fin1Ctl.get(), uni1Ctl.get(), det1Ctl.get(), wid1Ctl.get(), pan1Ctl.get() },
               { lvl1Ctl.get(), ph1Ctl.get(), pwCtl.get(), wt1Ctl.get(), prand1Ctl.get() });
    layoutOsc (oscBFrame, waveB, wave2Ctl.get(),
               { oct2Ctl.get(), fin2Ctl.get(), uni2Ctl.get(), det2Ctl.get(), wid2Ctl.get(), pan2Ctl.get() },
               { lvl2Ctl.get(), ph2Ctl.get(), fmCtl.get(), ringCtl.get(), wt2Ctl.get(), prand2Ctl.get() });

    // ROW 2 : ENV A | ENV F | LFO 1 | LFO 2
    const int w2 = (row2.getWidth() - 3 * gap) / 4;
    ampEnvFrame.setBounds (row2.removeFromLeft (w2));
    row2.removeFromLeft (gap);
    filEnvFrame.setBounds (row2.removeFromLeft (w2));
    row2.removeFromLeft (gap);
    lfo1Frame.setBounds (row2.removeFromLeft (w2));
    row2.removeFromLeft (gap);
    lfo2Frame.setBounds (row2);

    {
        auto inner = ampEnvFrame.getBounds().reduced (9, 24);
        ampGraph.setBounds (inner.removeFromTop (inner.getHeight() - 58));
        inner.removeFromTop (2);
        auto knobs = inner;
        const int kw = knobs.getWidth() / 4;
        aACtl->setBounds (knobs.removeFromLeft (kw).reduced (1));
        aDCtl->setBounds (knobs.removeFromLeft (kw).reduced (1));
        aSCtl->setBounds (knobs.removeFromLeft (kw).reduced (1));
        aRCtl->setBounds (knobs.reduced (1));
    }
    {
        auto inner = filEnvFrame.getBounds().reduced (9, 24);
        filGraph.setBounds (inner.removeFromTop (inner.getHeight() - 58));
        inner.removeFromTop (2);
        auto knobs = inner;
        const int kw = knobs.getWidth() / 6;
        fACtl->setBounds (knobs.removeFromLeft (kw).reduced (1));
        fDCtl->setBounds (knobs.removeFromLeft (kw).reduced (1));
        fSCtl->setBounds (knobs.removeFromLeft (kw).reduced (1));
        fRCtl->setBounds (knobs.removeFromLeft (kw).reduced (1));
        envAmtCtl->setBounds (knobs.removeFromLeft (kw).reduced (1));
        modDepthCtl->setBounds (knobs.reduced (1));
    }

    auto layoutLfo = [] (goaui::Panel& frame, goaui::LfoGraph& graph, juce::Component* wave,
                         juce::Component* tgt, juce::Component* unit, juce::Component* div,
                         juce::Component* rate, juce::Component* depth)
    {
        auto inner = frame.getBounds().reduced (9, 24);
        graph.setBounds (inner.removeFromTop (inner.getHeight() - 82));
        inner.removeFromTop (2);
        auto comboRow = inner.removeFromTop (20);
        const int cw = comboRow.getWidth() / 4;
        wave->setBounds (comboRow.removeFromLeft (cw).reduced (1, 1));
        tgt->setBounds (comboRow.removeFromLeft (cw).reduced (1, 1));
        unit->setBounds (comboRow.removeFromLeft (cw).reduced (1, 1));
        div->setBounds (comboRow.reduced (1, 1));
        inner.removeFromTop (2);
        auto knobs = inner;
        const int kw = knobs.getWidth() / 2;
        rate->setBounds (knobs.removeFromLeft (kw).reduced (1));
        depth->setBounds (knobs.reduced (1));
    };

    layoutLfo (lfo1Frame, lfo1Graph, l1WaveCtl.get(), l1TgtCtl.get(), l1UnitCtl.get(),
               l1DivCtl.get(), l1RateCtl.get(), l1DepthCtl.get());
    layoutLfo (lfo2Frame, lfo2Graph, l2WaveCtl.get(), l2TgtCtl.get(), l2UnitCtl.get(),
               l2DivCtl.get(), l2RateCtl.get(), l2DepthCtl.get());

    // ROW 3 : CHORUS | PHASER | DELAY | REVERB | OTT | MOVEMENT | MACRO | NOISE.
    // Panel widths are weighted by KNOB COUNT so every FX cell lands at the
    // same width: 3 : 3 : 4 : 4 : 5 : 4 for CHORUS : PHASER : DELAY : REVERB :
    // OTT : MOVE (23 units). REVERB and MOVEMENT were both weighted 3 while
    // holding four knobs each (SIZE/DAMP/MIX/SHIM and DUCK/DRIFT/DETUNE/WIDTH),
    // which squeezed their cells to ~21px - narrow enough that each knob's
    // label and its live readout were painted on top of each other.
    // NOISE has only one knob: no panel frame, just the control in the tail.
    // (An earlier draft split 24/18 of the width across the six frames and
    // silently squeezed NOISE to a sliver — found by the extremes sweep.)
    constexpr int row3Units = 3 + 3 + 4 + 4 + 5 + 4;
    const int noiseW = juce::jmin (120, juce::jmax (56, row3.getWidth() / 9));
    // MACRO takes a fixed slice too; seven framed panels mean six gaps,
    // then one more gap before the bare NOISE knob.
    const int shared = juce::jmax (360, row3.getWidth() - noiseW - 110 - 7 * gap);
    chorusFrame.setBounds (row3.removeFromLeft (shared * 3 / row3Units));
    row3.removeFromLeft (gap);
    phaserFrame.setBounds (row3.removeFromLeft (shared * 3 / row3Units));
    row3.removeFromLeft (gap);
    delayFrame.setBounds (row3.removeFromLeft (shared * 4 / row3Units));
    row3.removeFromLeft (gap);
    reverbFrame.setBounds (row3.removeFromLeft (shared * 4 / row3Units));
    row3.removeFromLeft (gap);
    ottFrame.setBounds (row3.removeFromLeft (shared * 5 / row3Units));
    row3.removeFromLeft (gap);
    moveFrame.setBounds (row3.removeFromLeft (shared * 4 / row3Units));
    row3.removeFromLeft (gap);
    // MACRO: fixed narrow panel for the two performance knobs.
    macroFrame.setBounds (row3.removeFromLeft (110));
    row3.removeFromLeft (gap);
    // NOISE: bare knob + TYPE combo (WHITE/PINK), no frame, in the tail.
    {
        auto tail = row3.reduced (2, 4);
        noiseTypeCtl->setBounds (tail.removeFromTop (20));
        noiseCtl->setBounds (tail);
    }

    auto row = [] (goaui::Panel& frame, const juce::Array<juce::Component*>& cells)
    {
        auto inner = frame.getBounds().reduced (8, 24);
        // Clamp the band to the panel's inner height: on a short window a
        // fixed 66 would centre the cells on a degenerate rect and spill them
        // outside the panel.
        auto band = inner.withSizeKeepingCentre (inner.getWidth(),
                                                 juce::jmax (24, juce::jmin (66, inner.getHeight())));
        const int n = cells.size();
        const int kw = band.getWidth() / n;
        for (int i = 0; i < n; ++i)
            cells[i]->setBounds (band.removeFromLeft (kw).reduced (2));
    };

    row (chorusFrame, { chRateCtl.get(), chDepthCtl.get(), chMixCtl.get() });
    row (phaserFrame, { phRateCtl.get(), phDepthCtl.get(), phMixCtl.get() });
    row (delayFrame,  { dSyncCtl.get(), dTimeCtl.get(), dFbCtl.get(), dMixCtl.get() });
    row (reverbFrame, { rSizeCtl.get(), rDampCtl.get(), rMixCtl.get(),
                        shimCtl.get() });
    row (moveFrame,   { duckCtl.get(),
                        driftCtl.get(), uniDetCtl.get(), uniSpreadCtl.get() });
    row (ottFrame,    { oDepthCtl.get(), oLowCtl.get(), oMidCtl.get(), oHighCtl.get(), oOutCtl.get() });
    // NOISE knob bounds are set directly in the row layout above (no frame).
    // MACRO: a taller band so A / B stack comfortably in the narrow panel.
    {
        auto inner = macroFrame.getBounds().reduced (7, 24);
        const int kh = juce::jmin (70, inner.getHeight() / 2);
        macroACtl->setBounds (inner.removeFromTop (kh).reduced (1));
        macroBCtl->setBounds (inner.reduced (1));
    }
    // ---- bottom bar ----
    auto bb = bottom.reduced (6, 5);
    auto rightCol = bb.removeFromRight (296);
    {
        auto r1 = rightCol.removeFromTop (46);
        voicingCtl->setBounds (r1.removeFromLeft (128).reduced (2, 2));
        polyCtl->setBounds (r1.removeFromLeft (56).reduced (2, 2));
        portaCtl->setBounds (r1.removeFromLeft (56).reduced (2, 2));
        bendCtl->setBounds (r1.reduced (2, 2));
    }
    rightCol.removeFromTop (6);

    // The three remaining right-hand rows share what is left evenly.
    const int colRow = juce::jmax (18, rightCol.getHeight() / 3);

    // Second right-hand row: scale quantizer + microtuning.
    {
        auto r2 = rightCol.removeFromTop (colRow);
        scaleCtl->setBounds (r2.removeFromLeft (106).reduced (2, 2));
        rootCtl->setBounds  (r2.removeFromLeft (52).reduced (2, 2));
        lockCtl->setBounds  (r2.removeFromLeft (44).reduced (2, 2));
        tuneCtl->setBounds  (r2.removeFromLeft (46).reduced (2, 2));
        sclBtn.setBounds    (r2.reduced (1, 2));
    }

    // Third right-hand row: sidechain pump + analog character + zoom.
    {
        auto r3 = rightCol.removeFromTop (colRow);
        pumpSyncCtl->setBounds  (r3.removeFromLeft (56).reduced (2, 2));
        pumpDepthCtl->setBounds (r3.removeFromLeft (50).reduced (2, 2));
        analogCtl->setBounds    (r3.removeFromLeft (46).reduced (2, 2));
        hqCtl->setBounds        (r3.removeFromLeft (52).reduced (2, 2));
        zoomBtn.setBounds       (r3.reduced (1, 2));
    }

    // Fourth right-hand row: master 3-band EQ, sitting with the other global
    // controls rather than in the FX row (whose cells have no width to spare —
    // adding a panel there pushed the CHORUS rate readout past its cell).
    {
        auto r4 = rightCol;
        eqLowCtl->setBounds     (r4.removeFromLeft (74).reduced (2, 2));
        eqMidCtl->setBounds     (r4.removeFromLeft (74).reduced (2, 2));
        eqMidFreqCtl->setBounds (r4.removeFromLeft (74).reduced (2, 2));
        eqHighCtl->setBounds    (r4.reduced (2, 2));
    }

    auto stripRow = bb.removeFromTop (30);
    gateStrip.setBounds (stripRow.removeFromLeft ((stripRow.getWidth() - 6) / 2));
    stripRow.removeFromLeft (6);
    arpStrip.setBounds (stripRow);
    bb.removeFromTop (4);

    octDown.setBounds (bb.removeFromLeft (34).reduced (5, 8));
    octUp.setBounds (bb.removeFromLeft (34).reduced (5, 8));
    bb.removeFromLeft (4);
    // Utility buttons, then whatever is left is the keyboard. Fixed slices so
    // the keys absorb a narrow window rather than the buttons collapsing.
    // (56px, not 50: RAND is four wide glyphs and 50 clipped it to "RA...".)
    randBtn.setBounds (bb.removeFromLeft (56).reduced (2, 9));
    undoBtn.setBounds (bb.removeFromLeft (56).reduced (2, 9));
    redoBtn.setBounds (bb.removeFromLeft (56).reduced (2, 9));
    abBtn.setBounds   (bb.removeFromLeft (40).reduced (2, 9));
    lockBtn.setBounds (bb.removeFromLeft (56).reduced (2, 9));
    keyboard.setBounds (bb.reduced (2, 2));
}



//==============================================================================
//  About card (MENU → ABOUT): version, license state, machine ID and product
//  page. Pure display — no interaction beyond COPY and the close button.
//==============================================================================
goaui::AboutOverlay::AboutOverlay()
{
    // The tinted backdrop is the click target (see mouseDown); the card and its
    // children still receive their own clicks.
    setInterceptsMouseClicks (true, true);
    setAlwaysOnTop (true);

    head.setText ("GOASYNTH", juce::dontSendNotification);
    head.setFont (juce::Font (juce::FontOptions (17.0f, juce::Font::bold)));
    head.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (head);

    verLine.setText ("VERSION " + goaVersionString(), juce::dontSendNotification);
    verLine.setFont (juce::Font (juce::FontOptions (13.0f)));
    verLine.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (verLine);

    stateLine.setFont (juce::Font (juce::FontOptions (12.5f, juce::Font::bold)));
    stateLine.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (stateLine);

    idLabel.setText ("MACHINE ID", juce::dontSendNotification);
    idLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    idLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (idLabel);

    idValue.setText (goa::License::machineId(), juce::dontSendNotification);
    idValue.setFont (juce::Font (juce::FontOptions (13.5f, juce::Font::bold)));
    idValue.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (idValue);

    siteLine.setText ("goasynth \u2014 y4m4.github.io/GoaSynth", juce::dontSendNotification);
    siteLine.setFont (juce::Font (juce::FontOptions (11.5f)));
    siteLine.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (siteLine);

    copyBtn.setButtonText ("COPY");
    copyBtn.setTooltip ("Copy the machine id to the clipboard");
    copyBtn.onClick = [this]
    {
        juce::SystemClipboard::copyTextToClipboard (goa::License::machineId());
        copyBtn.setButtonText ("COPIED");
        copyBtn.setButtonText ("COPY");
    };
    addAndMakeVisible (copyBtn);

    closeBtn.onClick = [this] { setVisible (false); };
    addAndMakeVisible (closeBtn);

    // Clicking the darkened backdrop (anywhere outside the card) also closes:
    // the standard overlay dismissal, and the X itself sits on the card so it
    // is impossible to miss (a corner-of-window X once read as "no close
    // button at all" because the eye searches the card, not the screen).
    mouseDownCallback = [this] (const juce::MouseEvent& e)
    {
        if (! cardBounds().contains (e.position.toInt()))
            setVisible (false);
    };

    retint();
}

void goaui::AboutOverlay::mouseDown (const juce::MouseEvent& e)
{
    if (mouseDownCallback != nullptr)
        mouseDownCallback (e);
}

juce::Rectangle<int> goaui::AboutOverlay::cardBounds() const noexcept
{
    return getLocalBounds().withSizeKeepingCentre (440, 268);
}

void goaui::AboutOverlay::paint (juce::Graphics& g)
{
    g.setColour (bgDark.withAlpha (0.96f));
    g.fillAll();

    const auto card = cardBounds().toFloat();
    g.setColour (bgPanel);
    g.fillRoundedRectangle (card, 6.0f);
    g.setColour (border);
    g.drawRoundedRectangle (card.reduced (0.5f), 6.0f, 1.0f);
    g.setGradientFill (uvGradient ({ card.getX() + 10.0f, card.getY(),
                                     card.getWidth() - 20.0f, 2.0f }));
    g.fillRect (card.getX() + 10.0f, card.getY(), card.getWidth() - 20.0f, 2.0f);
}

void goaui::AboutOverlay::resized()
{
    // The X sits ON the card's top-right corner where the eye actually looks
    // for it, mirroring every other overlay's card furniture.
    closeBtn.setBounds (cardBounds().removeFromTop (26)
                                  .removeFromRight (28).reduced (5));

    auto card = cardBounds();
    card.reduce (18, 12);
    head.setBounds (card.removeFromTop (26));
    verLine.setBounds (card.removeFromTop (20));
    stateLine.setBounds (card.removeFromTop (22));
    card.removeFromTop (8);
    idLabel.setBounds (card.removeFromTop (15));
    idValue.setBounds (card.removeFromTop (22));
    copyBtn.setBounds (card.removeFromTop (26).withSizeKeepingCentre (86, 24));
    card.removeFromTop (6);
    siteLine.setBounds (card);
}

void goaui::AboutOverlay::retint()
{
    head.setColour (juce::Label::textColourId, accent);
    verLine.setColour (juce::Label::textColourId, textBright);
    stateLine.setColour (juce::Label::textColourId, accentB);
    idLabel.setColour (juce::Label::textColourId, textDim);
    idValue.setColour (juce::Label::textColourId, textBright);
    siteLine.setColour (juce::Label::textColourId, textDim);
    copyBtn.setColour (juce::TextButton::buttonColourId, bgPanelLo);
    copyBtn.setColour (juce::TextButton::textColourOffId, textDim);
    closeBtn.setColour (juce::TextButton::buttonColourId, bgPanelLo);
    closeBtn.setColour (juce::TextButton::textColourOffId, textDim);
}

//==============================================================================
//  Update result dialog (MENU → CHECK FOR UPDATES → outcome). An overlay on
//  purpose: an OS message box cannot host a clickable link, and the two
//  outcomes that name the download URL are exactly the ones where clicking it
//  is the point. As the editor's child it also inherits the guarantee the
//  dialog always needed — the host has no window to bury it behind.
goaui::UpdateResultOverlay::UpdateResultOverlay()
{
    setInterceptsMouseClicks (true, true);
    setAlwaysOnTop (true);

    titleLabel.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    titleLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (titleLabel);

    messageLabel.setFont (juce::Font (juce::FontOptions (12.5f)));
    messageLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (messageLabel);

    // juce's own hyperlink control: underlined, hand cursor over it, hover
    // feedback, and a click hands the URL to the system's default browser —
    // the same launchInDefaultBrowser() the Change Log menu item uses. The
    // fixed font (resizeToMatchComponentHeight = false) keeps the underline
    // the same size whatever row height layout picks.
    link.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::underlined)), false,
                  juce::Justification::centred);
    addAndMakeVisible (link);

    okBtn.onClick = [this] { setVisible (false); };
    addAndMakeVisible (okBtn);

    closeBtn.onClick = [this] { setVisible (false); };
    addAndMakeVisible (closeBtn);

    // The backdrop is the click target (see mouseDown); the card and its
    // children still receive their own clicks — the link sits ON the card, so
    // opening the browser can never be mistaken for a dismissal.
    mouseDownCallback = [this] (const juce::MouseEvent& e)
    {
        if (! cardBounds().contains (e.position.toInt()))
            setVisible (false);
    };

    retint();
}

void goaui::UpdateResultOverlay::configure (const juce::String& title,
                                            const juce::String& message,
                                            const juce::String& linkUrl)
{
    titleLabel.setText (title, juce::dontSendNotification);
    messageLabel.setText (message, juce::dontSendNotification);

    // "Up to date" has nothing to open, so the link row is hidden rather than
    // shown as dead furniture — the message then takes the whole body.
    if (linkUrl.isNotEmpty())
    {
        link.setButtonText (linkUrl);
        link.setURL (juce::URL (linkUrl));   // also refreshes the tooltip
        link.setVisible (true);
    }
    else
    {
        link.setVisible (false);
    }
}

void goaui::UpdateResultOverlay::mouseDown (const juce::MouseEvent& e)
{
    if (mouseDownCallback != nullptr)
        mouseDownCallback (e);
}

juce::Rectangle<int> goaui::UpdateResultOverlay::cardBounds() const noexcept
{
    return getLocalBounds().withSizeKeepingCentre (440, 210);
}

void goaui::UpdateResultOverlay::paint (juce::Graphics& g)
{
    g.setColour (bgDark.withAlpha (0.96f));
    g.fillAll();

    const auto card = cardBounds().toFloat();
    g.setColour (bgPanel);
    g.fillRoundedRectangle (card, 6.0f);
    g.setColour (border);
    g.drawRoundedRectangle (card.reduced (0.5f), 6.0f, 1.0f);
    g.setGradientFill (uvGradient ({ card.getX() + 10.0f, card.getY(),
                                     card.getWidth() - 20.0f, 2.0f }));
    g.fillRect (card.getX() + 10.0f, card.getY(), card.getWidth() - 20.0f, 2.0f);
}

void goaui::UpdateResultOverlay::resized()
{
    // The X sits ON the card's top-right corner, as everywhere else.
    closeBtn.setBounds (cardBounds().removeFromTop (26)
                                    .removeFromRight (28).reduced (5));

    auto card = cardBounds();
    card.reduce (18, 14);
    titleLabel.setBounds (card.removeFromTop (24));
    card.removeFromTop (4);
    okBtn.setBounds (card.removeFromBottom (26).withSizeKeepingCentre (86, 24));
    card.removeFromBottom (6);

    if (link.isVisible())
    {
        // The link is the row right above OK: the message reads down into it
        // — "download from:" → the address → the button.
        link.setBounds (card.removeFromBottom (24));
        card.removeFromBottom (4);
    }

    messageLabel.setBounds (card);
}

void goaui::UpdateResultOverlay::retint()
{
    titleLabel.setColour (juce::Label::textColourId, accent);
    messageLabel.setColour (juce::Label::textColourId, textBright);
    link.setColour (juce::HyperlinkButton::textColourId, accent);
    okBtn.setColour (juce::TextButton::buttonColourId, bgPanelLo);
    okBtn.setColour (juce::TextButton::textColourOffId, textDim);
    closeBtn.setColour (juce::TextButton::buttonColourId, bgPanelLo);
    closeBtn.setColour (juce::TextButton::textColourOffId, textDim);
}

//==============================================================================
//  Update check (MENU → CHECK FOR UPDATES): fetches docs/version.json from the
//  product site on a background thread, compares it to the running version and
//  always reports back in the message thread — success, newer feed, or the
//  reason nothing could be checked. The plugin never phones home beyond this
//  one anonymous GET.
//==============================================================================
namespace
{
    // The product site, and the feed it publishes: same origin, one path
    // apart. The result dialogs link here; the fetch reads the feed below.
    constexpr const char* siteUrl = "https://y4m4.github.io/GoaSynth/";
    constexpr const char* updateFeedUrl =
        "https://y4m4.github.io/GoaSynth/version.json";

    class UpdateCheckThread : public juce::Thread
    {
    public:
        explicit UpdateCheckThread (GoaSynthAudioProcessorEditor& owner_)
            : juce::Thread ("goasynth update check"), owner (owner_) {}

        void run() override
        {
            // Every path through here — success or failure — must end in the
            // message-thread callback below: the original version returned
            // silently on any fetch failure, which is exactly the "Check for
            // updates does nothing" report. A quiet failure is
            // indistinguishable from a dead menu item.
            // A SafePointer keeps the callback harmless if the editor died
            // while the request ran.
            juce::Component::SafePointer<GoaSynthAudioProcessorEditor> safeOwner (&owner);

            int statusCode = 0;
            // A plain GET: this is a read of a static file, and GitHub Pages
            // answers a bodyless POST with 405. (The original inPostData form
            // turned every request into a POST, so even a live feed failed.)
            // inAddress keeps the (empty) parameter list in the URL, i.e. a
            // bodyless GET — the only other choice, inPostData, sends a body.
            const auto stream = juce::URL (updateFeedUrl)
                                    .createInputStream (juce::URL::InputStreamOptions (
                                                            juce::URL::ParameterHandling::inAddress)
                                                            .withConnectionTimeoutMs (8000)
                                                            .withStatusCode (&statusCode));

            if (stream != nullptr && statusCode >= 200 && statusCode < 300)
            {
                latest = juce::JSON::parse (stream->readEntireStreamAsString());

                // Only a purely numeric "latest" counts as a real feed; anything
                // else (error page, CDN challenge, truncated file) is treated as
                // "cannot check" rather than "up to date".
                const juce::String latestVer = latest.getProperty ("latest", {}).toString();
                owner.updateReachable.store (latestVer.isNotEmpty()
                                                 && latestVer.containsOnly ("0123456789."));

                // Set every result flag BEFORE waking the message thread, so
                // updateCheckDone() can never read half-published state.
                owner.updateThreadVersion = latestVer;
                owner.updateOlder.store (owner.updateReachable.load()
                                             && goaui::compareVersions (goaVersionString(),
                                                                        latestVer) < 0);
            }
            else
            {
                owner.updateReachable.store (false);
            }

            // Report — always, success or failure.
            juce::MessageManager::callAsync ([safeOwner]
            {
                if (safeOwner != nullptr)
                    safeOwner->updateCheckDone();
            });
        }

        GoaSynthAudioProcessorEditor& owner;
        juce::var latest;
    };
}

void GoaSynthAudioProcessorEditor::runUpdateCheck()
{
    if (updateThread != nullptr && updateThread->isThreadRunning())
        return;

    // A result dialog must not try to own the keyboard while the overlay is up
    // (the activation screen grabs it for the serial field).
    if (licenseOverlay != nullptr && licenseOverlay->isVisible())
    {
        juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
            "GOASYNTH",
            "Close the activation screen first, then check for updates.", this);
        return;
    }

    updateThread = std::make_unique<UpdateCheckThread> (*this);
    updateThread->startThread();
}

void GoaSynthAudioProcessorEditor::updateCheckDone()
{
    // No thread teardown here: the worker may still be finishing its last
    // lines; the destructor (or the next check replacing the unique_ptr)
    // joins it safely.
    //
    // Every outcome shows in the result overlay — never a native box, never
    // silence. The overlay is this editor's own child, so the host has no
    // window to bury it behind (an unparented native box was free to land
    // behind the arrange view — another "nothing happens" symptom), and the
    // two outcomes that name the download carry the address as a link that
    // opens the browser when clicked. Native boxes cannot host a link at all:
    // that is the reason this dialog is an overlay.
    if (! updateReachable.load())
    {
        showUpdateResult ("GOASYNTH",
                          "Could not reach the update feed.\n\n"
                          "Latest version and downloads:",
                          siteUrl);
        return;
    }

    if (updateOlder.load())
    {
        showUpdateResult ("UPDATE AVAILABLE",
                          "GoaSynth " + updateThreadVersion + " is available (you are running "
                              + goaVersionString() + ").\n\n"
                          "Download the new installer from:",
                          siteUrl);
        return;
    }

    showUpdateResult ("GOASYNTH",
                      "You are running the latest version (" + goaVersionString() + ").",
                      {});
}

void GoaSynthAudioProcessorEditor::showUpdateResult (const juce::String& title,
                                                     const juce::String& message,
                                                     const juce::String& linkUrl)
{
    // One open path for all three outcomes, mirroring the About card's:
    // configure, fit to the current window, front, show.
    if (updateResultOverlay == nullptr)
        return;

    updateResultOverlay->configure (title, message, linkUrl);
    updateResultOverlay->setBounds (getLocalBounds());
    updateResultOverlay->toFront (true);
    updateResultOverlay->setVisible (true);
}
