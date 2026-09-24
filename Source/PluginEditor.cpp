#include "PluginEditor.h"
#include "AiCloudGen.h"
#include "AiPatchGen.h"
#include "LearnedPatches.h"
#include "License.h"
#include "Presets.h"
#include "UserPresets.h"

#include <cmath>

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

Theme themeFromIndex (int i) noexcept
{
    return (Theme) juce::jlimit (0, (int) themeCount - 1, i);
}

static void walkRetint (juce::Component* c);

void setTheme (Theme t, juce::Component* root)
{
    activeTheme = t;
    switch (t)
    {
        case themeSteel:  applyPalette (steelPalette());  break;
        case themeAnalog: applyPalette (analogPalette()); break;
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
    for (int i = 0; i < param::modSlots && n < 3; ++i)
    {
        auto* dst = apvts.getParameter (param::modDst (i));
        auto* amt = apvts.getParameter (param::modAmt (i));
        if (dst == nullptr || amt == nullptr)
            continue;
        const int dstIdx = (int) dst->getNormalisableRange().convertFrom0to1 (dst->getValue());
        if (dstIdx <= 0)
            continue;
        const auto& row = param::modDestList()[(size_t) dstIdx];
        const float a = amt->getNormalisableRange().convertFrom0to1 (amt->getValue());
        if (paramId != row.param || std::abs (a) < 0.003f)
            continue;
        auto* src = apvts.getParameter (param::modSrc (i));
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

    // The control's own label, best-effort: the destination display name
    // (from the dest list) is the authoritative long name.
    juce::String dstName = paramId;
    const auto& list = param::modDestList();
    for (int i = 1; i < (int) list.size(); ++i)
        if (paramId == list[(size_t) i].param) { dstName = list[(size_t) i].label; break; }

    juce::StringArray lines;
    for (int k = 0; k < n; ++k)
        lines.add ("MOD " + juce::String (slot[k] + 1)
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
        label.setBounds (b.removeFromTop (12));
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

    // Serum-style readout: the live value under the name, in compact units
    // (1.5k, 12.0ct, 85, 250m). Drawn here (not in the Label child) so the
    // value can change every frame; the label keeps just the name. Combos
    // skip it - their selection already shows in the box itself.
    if (! useCombo)
    {
        const juce::String txt = liveValueText (*modSource, paramId, true);
        // Keep the readout clear of the mod dots in the top-right corner.
        auto band = getLocalBounds().removeFromTop (12)
                        .removeFromBottom (7).toFloat()
                        .withTrimmedRight (n > 0 ? (float) n * 8.0f + 3.0f : 0.0f);
        g.setColour (textBright.withAlpha (0.8f));
        g.setFont (juce::Font (juce::FontOptions (7.0f, juce::Font::bold)));
        g.drawText (txt, band, juce::Justification::centredRight);
    }

    // Live-value tooltip: name + value in engine units, refreshed every
    // paint. MOD routings take the first line, value stays second.
    const juce::String valueLine = useCombo
        ? combo.getText()
        : liveValueText (*modSource, paramId);
    juce::String tip;
    if (modDotTip (*modSource, paramId, tip))
        setTip (tip + (baseTip.isNotEmpty() ? "\n" + baseTip : "")
                  + (valueLine.isNotEmpty() ? " \u2014 " + valueLine : ""));
    else
        setTip (baseTip + (valueLine.isNotEmpty() ? " \u2014 " + valueLine : ""));
}

// Pick flash on top of the knob (children stay visible above the glow).
void Ctl::paintOverChildren (juce::Graphics& g)
{
    paintFlash (g, *this, flashFade());
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
ToggleCtl::ToggleCtl (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId,
                      const juce::String& text)
    : paramId (paramId), modSource (&apvts)
{
    btn.setButtonText ({});
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
    btn.setBounds (b.withSizeKeepingCentre (16, 16));
}

//==============================================================================
Panel::Panel (const juce::String& titleText, ColRole r)
    : title (titleText), role (r), headCol (roleColour (r))
{
    setOpaque (false);
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
    g.fillRoundedRectangle (b, 5.0f);

    auto head = b.removeFromTop (18.0f);
    g.setColour (bgHeader);
    g.fillRoundedRectangle (head, 5.0f);
    g.fillRect (head.withTrimmedTop (7.0f));

    g.setColour (headCol);
    g.fillEllipse (head.getX() + 7.0f, head.getCentreY() - 2.5f, 5.0f, 5.0f);

    g.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
    g.drawText (title.toUpperCase(), head.reduced (17.0f, 0.0f), juce::Justification::centredLeft);

    g.setColour (headCol.withAlpha (0.35f));
    g.fillRect (b.getX() + 6.0f, head.getBottom() - 1.0f, b.getWidth() - 12.0f, 1.0f);

    g.setColour (border.withAlpha (0.85f));
    g.drawRoundedRectangle (b.reduced (0.5f), 5.0f, 1.0f);
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
    return (i >= 0 && i < 7 && frac <= 26.0f) ? i : -1;
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

            // Tools row: transforms + one-click shapes.
            {
                static constexpr const char* labels[7] =
                    { "SMOOTH", "FLIP", "NORM", "P25", "P50", "FORM", "SPK" };
                const auto tb = toolsRect();
                for (int i = 0; i < 7; ++i)
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
    g.setColour (col.withAlpha (0.12f));
    g.fillPath (fill);
    g.setColour (col);
    g.strokePath (curve, juce::PathStrokeType (1.5f));

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
          " gate settings; SYNC/OCT cycle their params; COPY mirrors the arp pattern"
        : "Arp sequencer: click/drag steps for semitones, lower band sets velocity;"
          " right-click accents; SYNC/OCT/DIR/SCALE/FILL cycle their params");
    if (kind == gateStrip)
    {
        syncPar = proc.apvts.getParameter (param::gateSync);
        octPar  = proc.apvts.getParameter (param::gateDepth);
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
        for (int i = 0; i < 16; ++i)
            vels[(size_t) i] = proc.apvts.getParameter (param::arpVel (i));
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
    auto b = getLocalBounds().toFloat().withTrimmedLeft (kind == gateStrip ? 182.0f : 226.0f);
    const float w = b.getWidth() / 16.0f;
    return b.removeFromLeft (w * (float) (i + 1)).removeFromRight (w).reduced (1.5f);
}

juce::Rectangle<float> StepStrip::velRect (int i) const
{
    const auto c = cellRect (i);
    return { c.getX(), c.getBottom() - 7.0f, c.getWidth(), 7.0f };
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
            auto body = cell.withTrimmedBottom (9.0f);   // leave room for the vel bar
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
                g.setColour (goaui::textBright);
                g.fillRect (juce::Rectangle<float> (cell.getX(), cell.getY(),
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
            g.setColour (goaui::textBright.withAlpha (0.85f));
            g.drawRoundedRectangle (cell, 2.0f, 1.4f);
            g.setColour (goaui::textBright.withAlpha (0.5f));
            g.fillRect (cell.removeFromTop (2.0f));
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
        cycle (dirPar, fine ? -1 : 1, 5);
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

void StepStrip::mouseDown (const juce::MouseEvent& e)
{
    const bool right = juce::ModifierKeys::currentModifiers.isRightButtonDown();
    lastCell = -1;
    velDragging = false;

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
    }

    if (right || syncCell().contains (e.position) || octCell().contains (e.position)
        || (kind == gateStrip && (copyCell().contains (e.position)
                                  || patCell().contains (e.position)))
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

    head.setText ("MOD MATRIX", juce::dontSendNotification);
    head.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    head.setColour (juce::Label::textColourId, accent);
    head.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (head);

    // Interaction cheat-sheet along the card's bottom edge (UX audit): every
    // affordance is invisible in a painted matrix, so spell it out.
    hint.setColour (juce::Label::textColourId, textDim);
    hint.setFont (juce::Font (juce::FontOptions (9.0f)));
    hint.setJustificationType (juce::Justification::centredLeft);
    hint.setText ("click SRC/DST to cycle \u00b7 right-click backwards \u00b7 click DST then a knob, or"
                  " SRC then an LFO/ENV graph, to assign \u00b7 click the amount bar to jump \u00b7"
                  " shift-drag = fine \u00b7 double-click amount = 0 \u00b7 ESC closes",
                  juce::dontSendNotification);
    addAndMakeVisible (hint);

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

    // Interaction cheat-sheet along the card's bottom edge.
    auto foot = cardBounds().removeFromBottom (20).reduced (16, 0);
    hint.setBounds (foot.removeFromTop (18));
}

juce::Rectangle<float> ModOverlay::rowsRect() const
{
    return cardBounds().toFloat().reduced (16.0f).withTrimmedTop (40.0f);
}

ModOverlay::RowGeo ModOverlay::rowGeo (int row) const
{
    const auto all = rowsRect();
    const float h = all.getHeight() / (float) param::modSlots;
    auto r = all.withTrimmedTop (h * (float) row).withHeight (h)
                .reduced (0.0f, 2.0f);
    RowGeo g;
    g.src = r.removeFromLeft (110.0f).reduced (2.0f);
    r.removeFromLeft (6.0f);
    g.dst = r.removeFromLeft (110.0f).reduced (2.0f);
    r.removeFromLeft (6.0f);
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
    labelled ({ rows.getX(), rows.getY() - 16.0f, 110.0f, 14.0f }, "SOURCE", textDim, false);
    labelled ({ rows.getX() + 116.0f, rows.getY() - 16.0f, 110.0f, 14.0f }, "DEST", textDim, false);
    labelled ({ rows.getX() + 232.0f, rows.getY() - 16.0f,
                rows.getWidth() - 232.0f, 14.0f },
              "AMOUNT  (drag, bipolar)", textDim, false);

    // Pick mode banner: sits between the title strip and the column labels.
    if (pickRow >= 0)
        labelled ({ rows.getX(), rows.getY() - 32.0f, rows.getWidth(), 14.0f },
                  pickSrc ? "PICK A SOURCE: click an LFO scope or envelope graph to route it here \u2014"
                            " same SRC or ESC cancels"
                          : "PICK A KNOB: click any control in the synth to route it here \u2014"
                            " same DST or ESC cancels",
                  accentB, false);

    for (int i = 0; i < param::modSlots; ++i)
    {
        const auto g_ = rowGeo (i);
        auto* src = proc.apvts.getParameter (param::modSrc (i));
        auto* dst = proc.apvts.getParameter (param::modDst (i));
        auto* amt = proc.apvts.getParameter (param::modAmt (i));
        if (src == nullptr || dst == nullptr || amt == nullptr)
            continue;

        const int srcIdx = (int) src->getNormalisableRange().convertFrom0to1 (src->getValue());
        const int dstIdx = (int) dst->getNormalisableRange().convertFrom0to1 (dst->getValue());
        const float amtV = amt->getNormalisableRange().convertFrom0to1 (amt->getValue());
        const bool active = srcIdx != 0 && dstIdx != 0;

        labelled (g_.src, param::modSourceName()[(size_t) srcIdx],
                  active ? accentA : textDim);
        labelled (g_.dst, param::modDestList()[(size_t) dstIdx].label,
                  active ? accent : textDim);

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
        auto* amt = proc.apvts.getParameter (param::modAmt (dragRow));
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
        if (auto* amt = proc.apvts.getParameter (param::modAmt (row)))
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
        cycle (proc.apvts.getParameter (param::modSrc (row)),
               param::modSourceName().size());
    else if (g_.dst.contains (pos))
        cycle (proc.apvts.getParameter (param::modDst (row)),
               (int) param::modDestList().size());
    repaint();
}

void ModOverlay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragRow < 0)
        return;
    auto* amt = proc.apvts.getParameter (param::modAmt (dragRow));
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
    auto* p = proc.apvts.getParameter (src ? param::modSrc (row) : param::modDst (row));
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

    head.setText ("GOASYNTH \u2014 ACTIVATION REQUIRED", juce::dontSendNotification);
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
        if (onSave != nullptr)
            onSave (name.getText(), collectTags(), sharedBtn.getToggleState());
    };
    addAndMakeVisible (saveBtn);

    cancelBtn.setColour (juce::TextButton::buttonColourId, bgPanelLo);
    cancelBtn.setColour (juce::TextButton::textColourOffId, textDim);
    cancelBtn.onClick = [this] { setVisible (false); };
    addAndMakeVisible (cancelBtn);
    retint();
}

void SavePresetOverlay::retint()
{
    head.setColour (juce::Label::textColourId, accentA);
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
    addAndMakeVisible (search);

    tagCombo.setColour (juce::ComboBox::backgroundColourId, bgPanel);
    tagCombo.setColour (juce::ComboBox::textColourId, accentA);
    tagCombo.setColour (juce::ComboBox::arrowColourId, accentA);
    tagCombo.setColour (juce::ComboBox::outlineColourId, border);
    tagCombo.setTextWhenNothingSelected ("TAG: ALL");
    tagCombo.onChange = [this] { if (onChanged != nullptr) onChanged(); };
    addAndMakeVisible (tagCombo);

    for (auto* tab : { &allTab, &factoryTab, &userTab })
    {
        tab->setColour (juce::TextButton::buttonColourId, bgPanel);
        tab->setColour (juce::TextButton::textColourOffId, textDim);
        addAndMakeVisible (tab);
    }
    allTab.onClick     = [this] { folderIdx = 0; syncTabs(); if (onChanged != nullptr) onChanged(); };
    factoryTab.onClick = [this] { folderIdx = 1; syncTabs(); if (onChanged != nullptr) onChanged(); };
    userTab.onClick    = [this] { folderIdx = 2; syncTabs(); if (onChanged != nullptr) onChanged(); };

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

    list.setColour (juce::ListBox::backgroundColourId, bgInset);
    list.setColour (juce::ListBox::outlineColourId, border);
    list.setModel (this);
    list.setRowHeight (22);
    list.setMultipleSelectionEnabled (false);
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
    tagCombo.setColour (juce::ComboBox::backgroundColourId, bgPanel);
    tagCombo.setColour (juce::ComboBox::textColourId, accentA);
    tagCombo.setColour (juce::ComboBox::arrowColourId, accentA);
    tagCombo.setColour (juce::ComboBox::outlineColourId, border);
    for (auto* tab : { &allTab, &factoryTab, &userTab })
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
    list.setColour (juce::ListBox::backgroundColourId, bgInset);
    list.setColour (juce::ListBox::outlineColourId, border);
    syncTabs();
    list.repaint();
    repaint();
}

void PresetBrowserOverlay::syncTabs()
{
    const int active = folderIdx;
    for (auto* tab : { &allTab, &factoryTab, &userTab })
        tab->setColour (juce::TextButton::textColourOffId,
                        tab == &allTab && active == 0 ? accent
                        : tab == &factoryTab && active == 1 ? accent
                        : tab == &userTab && active == 2 ? accent : textDim);
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
}

void PresetBrowserOverlay::resized()
{
    auto card = cardBounds();
    auto body = card.reduced (14, 10);

    auto top = body.removeFromTop (30);
    head.setBounds (top.removeFromLeft (180));
    closeBtn.setBounds (top.removeFromRight (30).reduced (4, 2));

    auto filters = body.removeFromTop (28);
    tagCombo.setBounds (filters.removeFromRight (110).reduced (0, 3));
    search.setBounds (filters.reduced (0, 3));

    auto tabs = body.removeFromTop (26);
    exportBtn.setBounds (tabs.removeFromRight (112).reduced (0, 2));
    tabs.removeFromRight (6);
    importBtn.setBounds (tabs.removeFromRight (122).reduced (0, 2));
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
    rows = r;
    selectedRow = sel;
    list.updateContent();
    if (juce::isPositiveAndBelow (selectedRow, getNumRows()))
        list.scrollToEnsureRowIsOnscreen (selectedRow);
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

    if (sel || r.selected)
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

    g.setColour (r.selected ? textBright : (r.isUser ? accentA : textBright).withAlpha (0.92f));
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    const int nameW = w - 178;   // room for the SHARED badge + tag column
    g.drawText (r.label, 14, 0, nameW, h, juce::Justification::centredLeft);

    if (r.shared)
    {
        // Machine-wide shared bank: teal pill badge so users can tell which
        // patches come from C:\Users\Public\Documents\GoaSynth.
        auto b = juce::Rectangle<int> (w - 170, (h - 13) / 2, 52, 13);
        g.setColour (accentA.withAlpha (0.14f));
        g.fillRoundedRectangle (b.toFloat(), 5.0f);
        g.setColour (accentA.withAlpha (0.55f));
        g.drawRoundedRectangle (b.toFloat(), 5.0f, 1.0f);
        g.setColour (accentA);
        g.setFont (juce::Font (juce::FontOptions (8.0f, juce::Font::bold)));
        g.drawText ("SHARED", b, juce::Justification::centred);
    }

    if (r.tagLabel.isNotEmpty())
    {
        g.setColour (textDim.withAlpha (0.75f));
        g.setFont (juce::Font (juce::FontOptions (9.0f)));
        g.drawText (r.tagLabel, w - 110, 0, 100, h, juce::Justification::centredRight);
    }
}

void PresetBrowserOverlay::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    if (juce::isPositiveAndBelow (row, (int) rows.size()) && ! rows[(size_t) row].header)
    {
        selectedRow = row;
        list.repaint();
        if (onSelect != nullptr)
            onSelect (rows[(size_t) row].visibleIndex);
        setVisible (false);
    }
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
        }
        presetBrowser->setVisible (! presetBrowser->isVisible());
    };

    presetBrowser = std::make_unique<goaui::PresetBrowserOverlay>();
    presetBrowser->setVisible (false);
    presetBrowser->onSelect = [this] (int visIdx) { applyPreset (visIdx); };
    presetBrowser->onChanged = [this] { rebuildPresetList(); };
    presetBrowser->onExport = [this] { exportPresetPack(); };
    presetBrowser->onImport = [this] { importPresetPack(); };
    addChildComponent (*presetBrowser);

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
    themeBtn.setTooltip ("Cycle the skin: UV Goa / steel / warm analog");
    addAndMakeVisible (themeBtn);
    themeBtn.onClick = [this]
    {
        const int next = ((int) goaui::activeTheme + 1) % (int) goaui::themeCount;
        goaui::setTheme (goaui::themeFromIndex (next), this);
        rebuildBackdrop();
    };

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
        saveOverlay->prefill ("MY GOA PATCH");
        saveOverlay->setBounds (getLocalBounds());
        saveOverlay->setVisible (true);
    };

    deleteBtn.setColour (juce::TextButton::buttonColourId, goaui::bgPanelLo);
    deleteBtn.setColour (juce::TextButton::textColourOffId, goaui::accentB);
    deleteBtn.setTooltip ("Delete the selected user preset");
    addAndMakeVisible (deleteBtn);
    deleteBtn.onClick = [this] { deleteUserPreset(); };

    saveOverlay = std::make_unique<goaui::SavePresetOverlay>();
    saveOverlay->setVisible (false);
    saveOverlay->onSave = [this] (const juce::String& n, const juce::StringArray& t, bool shared)
    {
        saveUserPreset (n, t, shared);
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
    setResizeLimits (960, 640, 2048, 1440);
    loadSizePref();
    setSize (defaultWidth, defaultHeight);
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
    if (proc.licensedFlag.load())
        return false;

    for (const auto& f : files)
        if (juce::File (f).hasFileExtension (goa::License::fileExtension))
            return true;

    return false;
}

void GoaSynthAudioProcessorEditor::filesDropped (const juce::StringArray& files, int x, int y)
{
    juce::ignoreUnused (x, y);

    if (anyOverlayUp() || proc.licensedFlag.load())
        return;

    for (const auto& path : files)
    {
        const juce::File f (path);
        if (! f.hasFileExtension (goa::License::fileExtension))
            continue;

        // During a trial the overlay is hidden - bring it up as a peek so
        // success/failure feedback isn't written to an invisible component.
        if (! licenseOverlay->isVisible())
            showLicenseOverlay (true);

        licenseOverlay->importLicenseFile (f);   // onActivated -> refreshLicenseUi()
        return;
    }
}

// Push the active theme into every component. Called once at construction
// (after all children exist) and from the THEME button.
// ---- interface zoom ----------------------------------------------------------
// The editor's layout is written for exactly 1120x780 logical pixels; a
// component transform scales the whole thing to any size without touching
// that code. The VST3 wrapper's childBoundsChanged picks the new bounds up
// and resizes the host window, so the plug-in grows in the DAW too.

juce::File GoaSynthAudioProcessorEditor::zoomPrefFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("GoaSynth").getChildFile ("zoom.txt");
}

juce::File GoaSynthAudioProcessorEditor::sizePrefFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("GoaSynth").getChildFile ("size.txt");
}

void GoaSynthAudioProcessorEditor::loadSizePref()
{
    const juce::StringArray parts =
        juce::StringArray::fromTokens (sizePrefFile().loadFileAsString(), " ", "");
    if (parts.size() == 2)
        setSize (juce::jlimit (960, 2048, parts[0].getIntValue()),
                 juce::jlimit (640, 1440, parts[1].getIntValue()));
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
    themeBtn.setButtonText (goaui::activeTheme == goaui::themeUv     ? "THEME: UV"
                            : goaui::activeTheme == goaui::themeSteel ? "THEME: STEEL"
                                                                      : "THEME: WARM");
    zoomBtn.setColour (juce::TextButton::buttonColourId, goaui::bgPanelLo);
    zoomBtn.setColour (juce::TextButton::textColourOffId, goaui::accentA);
    if (presetBrowser != nullptr && presetBrowser->isVisible())
        presetBrowser->retint();
    rebuildBackdrop();
    repaint();
}

GoaSynthAudioProcessorEditor::~GoaSynthAudioProcessorEditor()
{
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
    if (! proc.licensedFlag.load() && ! proc.trial
        && licenseOverlay != nullptr && ! licenseOverlay->isVisible())
    {
        showLicenseOverlay (false);
    }

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
        return c;
    };
    auto toggle = [this, &apvts] (const char* id, const char* name)
    {
        auto c = std::make_unique<goaui::ToggleCtl> (apvts, id, name);
        c->liveEngine = &proc.synth;
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

    subWaveCtl = knob (param::subWave,   "WAVE", true);
    subOctCtl  = knob (param::subOct,    "OCT");
    subCtl     = knob (param::subLevel,  "LEVEL");
    noiseCtl   = knob (param::noiseLevel, "LEVEL");

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
    modDepthCtl = knob (param::modDepth, "MODWHEEL");

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

    // FX row
    addAndMakeVisible (chorusFrame);
    addAndMakeVisible (phaserFrame);
    addAndMakeVisible (delayFrame);
    addAndMakeVisible (reverbFrame);
    addAndMakeVisible (moveFrame);
    addAndMakeVisible (ottFrame);
    addAndMakeVisible (noiseFrame);

    chRateCtl  = knob (param::chorusRate,  "RATE");
    chDepthCtl = knob (param::chorusDepth, "DEPTH");
    chMixCtl   = knob (param::chorusMix,   "MIX");
    phRateCtl  = knob (param::phRate,      "RATE");
    phDepthCtl = knob (param::phDepth,     "DEPTH");
    phMixCtl   = knob (param::phMix,       "MIX");
    dSyncCtl   = knob (param::delaySync,   "SYNC", true);
    dTimeCtl   = knob (param::delayTime,   "TIME");
    dFbCtl     = knob (param::delayFb,     "FEEDBACK");
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
    uniDetCtl    = knob (param::uniDetune, "DETUNE");
    uniSpreadCtl = knob (param::uniSpread, "WIDTH");
}

void GoaSynthAudioProcessorEditor::applyPreset (int index)
{
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

void GoaSynthAudioProcessorEditor::rebuildPresetList()
{
    const juce::String q = presetBrowser->search.getText().trim();
    const int tagIdx = presetBrowser->tagCombo.getSelectedItemIndex();
    const juce::String wantTag = tagIdx > 0
        ? presetBrowser->tagCombo.getItemText (tagIdx).toLowerCase() : juce::String();
    const int folder = presetBrowser->folder();   // 0 ALL, 1 FACTORY, 2 USER

    visiblePresets.clear();
    for (const auto& e : allPresets)
    {
        bool ok = true;
        if (folder == 1)
            ok = e.factoryIndex >= 0 || e.name == "Init Patch";   // FACTORY tab
        else if (folder == 2)
            ok = e.isUser;                                        // USER tab
        if (ok && wantTag.isNotEmpty())
            ok = e.tags.contains (wantTag, true);
        if (ok && q.isNotEmpty())
            ok = e.name.toLowerCase().contains (q.toLowerCase());
        if (ok)
            visiblePresets.push_back (e);
    }

    // Keep the selection valid; stay on the same patch when possible.
    if (selectedPreset >= (int) visiblePresets.size())
        selectedPreset = (int) visiblePresets.size() - 1;

    if (presetBrowser == nullptr)
        return;

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
        r.isUser = e.isUser;
        r.shared = e.shared;
        r.selected = (int) i == selectedPreset;
        r.visibleIndex = (int) i;
        juce::StringArray t;
        for (int k = 0; k < juce::jmin (3, e.tags.size()); ++k)
            t.add (e.tags[k].toUpperCase());
        r.tagLabel = t.joinIntoString (" ");
        rows.push_back (r);
    }
    presetBrowser->setRows (rows, -1);
}

// Async file chooser for pack import/export. Only one dialog can be alive at a
// time. JUCE requires the FileChooser to outlive its async operation, so the
// callback keeps it alive via a shared_ptr: if the plugin window closes while
// the native dialog is up, the chooser still exists when the dialog returns.
// A SafePointer guards every editor touch so a closed editor can't be touched.
bool GoaSynthAudioProcessorEditor::asyncPackChooser (const juce::String& title, int browserFlags,
                                                     std::function<void (const juce::File&)> onChosen)
{
    if (packChooser != nullptr)
        return false; // a dialog is already open

    const auto wildcards = "*." + juce::String (userpresets::packExtension);
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

    const bool opened = asyncPackChooser ("Export preset pack (.goapack)",
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
    const bool opened = asyncPackChooser ("Import preset pack (.goapack)",
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
        allPresets.push_back (std::move (e));
    }

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

void GoaSynthAudioProcessorEditor::deleteUserPreset()
{
    if (! juce::isPositiveAndBelow (selectedPreset, (int) visiblePresets.size()))
        return;
    const auto& entry = visiblePresets[(size_t) selectedPreset];
    if (! entry.isUser || ! entry.file.existsAsFile())
        return;
    userpresets::deletePreset (entry.file);
    refreshUserPresets();
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
    // DEL only means something while a user preset is selected.
    deleteBtn.setEnabled (juce::isPositiveAndBelow (selectedPreset, (int) visiblePresets.size())
                          && visiblePresets[(size_t) selectedPreset].isUser);
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
    auto bottom = b.removeFromBottom (110);
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
    if (presetBrowser != nullptr && presetBrowser->isVisible())
        presetBrowser->setBounds (getLocalBounds());

    // Reserve must fit all buttons (master 72 + panic 56 + theme 62 + ai 46 +
    // mod 42 + save 46 + delete 34 + 6x2 spacers = 370) plus the trial badge
    // (100) = 484, with slack - or JUCE silently clamps removeFromRight and
    // the leftmost buttons vanish.
    auto right = header.removeFromRight (484);
    trialBadge.setBounds (right.removeFromRight (100).reduced (2, 12));
    masterCtl->setBounds (right.removeFromRight (72).reduced (6, 2));
    panicBtn.setBounds (right.removeFromRight (56).reduced (4, 16));
    right.removeFromRight (2);
    themeBtn.setBounds (right.removeFromRight (62).reduced (2, 16));
    right.removeFromRight (2);
    aiBtn.setBounds (right.removeFromRight (46).reduced (2, 16));
    right.removeFromRight (2);
    modBtn.setBounds (right.removeFromRight (42).reduced (2, 16));
    right.removeFromRight (2);
    saveBtn.setBounds (right.removeFromRight (46).reduced (2, 16));
    right.removeFromRight (2);
    deleteBtn.setBounds (right.removeFromRight (34).reduced (2, 16));

    auto presetZone = header.withTrimmedLeft (280).withTrimmedRight (104).reduced (0, 13);
    nextBtn.setBounds (presetZone.removeFromRight (30).reduced (2, 0));
    prevBtn.setBounds (presetZone.removeFromLeft (30).reduced (2, 0));
    browseBtn.setBounds (presetZone.removeFromRight (64).reduced (4, 0));
    presetName.setBounds (presetZone.reduced (4, 0));

    // ---- rows ----
    auto row1 = content.removeFromTop (276);
    content.removeFromTop (gap);
    auto row2 = content.removeFromTop (196);
    content.removeFromTop (gap);
    auto row3 = content;

    // ROW 1 : OSC A | OSC B | FILTER | SUB
    auto sideCol = row1.removeFromRight (92);
    subFrame.setBounds (sideCol);
    {
        auto inner = subFrame.getBounds().reduced (7, 24);
        subWaveCtl->setBounds (inner.removeFromTop (20));
        auto band = inner.withSizeKeepingCentre (inner.getWidth(), juce::jmin (130, inner.getHeight()));
        const int kh = band.getHeight() / 2;
        subCtl->setBounds (band.removeFromTop (kh));
        subOctCtl->setBounds (band);
    }

    filtFrame.setBounds (row1.removeFromRight (300).withTrimmedRight (gap));
    {
        auto inner = filtFrame.getBounds().reduced (9, 24);
        ftypeCtl->setBounds (inner.removeFromTop (20));
        inner.removeFromTop (3);
        filtCurve.setBounds (inner.removeFromTop (94));
        inner.removeFromTop (3);
        auto knobs = inner;
        const int kw = knobs.getWidth() / 2;
        const int kh = knobs.getHeight() / 3;   // three knob rows
        auto kr1 = knobs.removeFromTop (kh);
        auto kr2 = knobs.removeFromTop (kh);
        auto kr3 = knobs;
        cutoffCtl->setBounds (kr1.removeFromLeft (kw).reduced (1));
        resoCtl->setBounds   (kr1.removeFromRight (kw).reduced (1));
        driveCtl->setBounds  (kr2.removeFromLeft (kw).reduced (1));
        keyCtl->setBounds    (kr2.removeFromRight (kw).reduced (1));
        // F-DRIVE / F-FB: documented filter-character knobs, but they were
        // never given bounds here, so they rendered at width zero (invisible)
        // — found by the OverlayTest extremes sweep.
        fdCtl->setBounds (kr3.removeFromLeft (kw).reduced (1));
        fbCtl->setBounds (kr3.reduced (1));
    }

    filt2Frame.setBounds (row1.removeFromRight (148).withTrimmedRight (gap));
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

    const int oscW = (row1.getWidth() - gap) / 2;
    oscAFrame.setBounds (row1.removeFromLeft (oscW));
    row1.removeFromLeft (gap);
    oscBFrame.setBounds (row1);

    auto layoutOsc = [] (goaui::Panel& frame, goaui::WaveDisplay& disp, juce::Component* waveCombo,
                         juce::Component* oct, juce::Component* fin,
                         juce::Component* uni, juce::Component* det, juce::Component* wid,
                         juce::Component* pan, juce::Component* wtPos, juce::Component* lvl,
                         juce::Component* ph, juce::Component* prand, juce::Component* extra)
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
        // share: row 1 is the same six cells for both oscillators (pitch
        // pair OCT+FIN reunited, unison trio UNI/DET/WID, stereo PAN), and
        // row 2 anchors output (LEVEL, PHASE) then the panel's tail — OSC A
        // has four cells, OSC B five (FM > A joins between PHASE and
        // WT POS). Equal-per-row beats the old fixed-6 grid: OSC A no
        // longer ends row 2 with an empty slot, and OSC B's P.RAND no
        // longer rides a reduced sliver next to FM > A.
        auto rowCells = [] (juce::Rectangle<int> r, const std::vector<juce::Component*>& cs)
        {
            const int n = (int) cs.size();
            if (n == 0)
                return;
            const int w = r.getWidth() / n;
            for (auto* c : cs)
                c->setBounds (r.removeFromLeft (w).reduced (1));
        };
        rowCells (kr1, { oct, fin, uni, det, wid, pan });
        std::vector<juce::Component*> row2 { lvl, ph };
        if (extra != nullptr)
            row2.push_back (extra);   // OSC B: FM > A
        row2.push_back (wtPos);
        if (prand != nullptr)
            row2.push_back (prand);
        rowCells (kr2, row2);
    };

    layoutOsc (oscAFrame, waveA, wave1Ctl.get(), oct1Ctl.get(), fin1Ctl.get(),
               uni1Ctl.get(), det1Ctl.get(), wid1Ctl.get(),
               pan1Ctl.get(), wt1Ctl.get(), lvl1Ctl.get(), ph1Ctl.get(), prand1Ctl.get(), nullptr);
    layoutOsc (oscBFrame, waveB, wave2Ctl.get(), oct2Ctl.get(), fin2Ctl.get(),
               uni2Ctl.get(), det2Ctl.get(), wid2Ctl.get(),
               pan2Ctl.get(), wt2Ctl.get(), lvl2Ctl.get(), ph2Ctl.get(), prand2Ctl.get(), fmCtl.get());

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

    // ROW 3 : CHORUS | PHASER | DELAY | REVERB | OTT | MOVEMENT | NOISE.
    // Panel widths are weighted by knob count so every FX knob cell lands at
    // the same width as the rest of the UI: NOISE takes a fixed slice for its
    // single LEVEL knob and the rest splits across the six shared frames in
    // 3 : 3 : 4 : 3 : 5 : 3 (CHORUS : PHASER : DELAY : REVERB : OTT : MOVE).
    // (An earlier draft split 24/18 of the width across the six frames and
    // silently squeezed NOISE to a sliver — found by the extremes sweep.)
    const int noiseW = juce::jmin (140, juce::jmax (64, row3.getWidth() / 8));
    const int shared = juce::jmax (360, row3.getWidth() - noiseW - 6 * gap);
    chorusFrame.setBounds (row3.removeFromLeft (shared * 3 / 21));
    row3.removeFromLeft (gap);
    phaserFrame.setBounds (row3.removeFromLeft (shared * 3 / 21));
    row3.removeFromLeft (gap);
    delayFrame.setBounds (row3.removeFromLeft (shared * 4 / 21));
    row3.removeFromLeft (gap);
    reverbFrame.setBounds (row3.removeFromLeft (shared * 3 / 21));
    row3.removeFromLeft (gap);
    ottFrame.setBounds (row3.removeFromLeft (shared * 5 / 21));
    row3.removeFromLeft (gap);
    moveFrame.setBounds (row3.removeFromLeft (shared * 3 / 21));
    row3.removeFromLeft (gap);
    noiseFrame.setBounds (row3);

    auto row = [] (goaui::Panel& frame, const juce::Array<juce::Component*>& cells)
    {
        auto inner = frame.getBounds().reduced (8, 24);
        auto band = inner.withSizeKeepingCentre (inner.getWidth(), juce::jmin (66, inner.getHeight()));
        const int n = cells.size();
        const int kw = band.getWidth() / n;
        for (int i = 0; i < n; ++i)
            cells[i]->setBounds (band.removeFromLeft (kw).reduced (2));
    };

    row (chorusFrame, { chRateCtl.get(), chDepthCtl.get(), chMixCtl.get() });
    row (phaserFrame, { phRateCtl.get(), phDepthCtl.get(), phMixCtl.get() });
    row (delayFrame,  { dSyncCtl.get(), dTimeCtl.get(), dFbCtl.get(), dMixCtl.get() });
    row (reverbFrame, { rSizeCtl.get(), rDampCtl.get(), rMixCtl.get() });
    row (ottFrame,    { oDepthCtl.get(), oLowCtl.get(), oMidCtl.get(), oHighCtl.get(), oOutCtl.get() });
    row (moveFrame,   { driftCtl.get(), uniDetCtl.get(), uniSpreadCtl.get() });
    // NOISE panel is the narrowest in the row: give its LEVEL knob the full
    // inner width (the 3-digit 100% readout has to fit alongside a mod dot).
    noiseCtl->setBounds (noiseFrame.getBounds().reduced (8, 24)
                             .withSizeKeepingCentre (noiseFrame.getWidth() - 16, 66));

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

    // Second right-hand row: scale quantizer + microtuning.
    {
        auto r2 = rightCol.removeFromTop ((rightCol.getHeight() + 1) / 2);
        scaleCtl->setBounds (r2.removeFromLeft (106).reduced (2, 2));
        rootCtl->setBounds  (r2.removeFromLeft (52).reduced (2, 2));
        lockCtl->setBounds  (r2.removeFromLeft (44).reduced (2, 2));
        tuneCtl->setBounds  (r2.removeFromLeft (46).reduced (2, 2));
        sclBtn.setBounds    (r2.reduced (1, 2));
    }

    // Third right-hand row: sidechain pump + analog character + zoom.
    {
        auto r3 = rightCol;
        pumpSyncCtl->setBounds  (r3.removeFromLeft (56).reduced (2, 2));
        pumpDepthCtl->setBounds (r3.removeFromLeft (50).reduced (2, 2));
        analogCtl->setBounds    (r3.removeFromLeft (46).reduced (2, 2));
        hqCtl->setBounds        (r3.removeFromLeft (52).reduced (2, 2));
        zoomBtn.setBounds       (r3.reduced (1, 2));
    }

    auto stripRow = bb.removeFromTop (30);
    gateStrip.setBounds (stripRow.removeFromLeft ((stripRow.getWidth() - 6) / 2));
    stripRow.removeFromLeft (6);
    arpStrip.setBounds (stripRow);
    bb.removeFromTop (4);

    octDown.setBounds (bb.removeFromLeft (34).reduced (5, 8));
    octUp.setBounds (bb.removeFromLeft (34).reduced (5, 8));
    keyboard.setBounds (bb.reduced (2, 2));
}


