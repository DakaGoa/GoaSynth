#pragma once

#include <array>
#include "AiCloudGen.h"
#include "PluginProcessor.h"
#include "SynthEngine.h"
#include "Tuning.h"

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

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        auto glow = b.expanded (6.0f);
        juce::ColourGradient halo (accent.withAlpha (0.28f), glow.getCentreX(),
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
    void retint();                       // re-apply theme colours to label/knob/combo
    void setTip (const juce::String&);   // dots' hover text -> child widgets

    juce::Label label;
    juce::Slider slider;
    juce::ComboBox combo;
    bool useCombo = false;
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

private:
    struct RowGeo { juce::Rectangle<float> src, dst, amt; };
    juce::Rectangle<float> rowsRect() const;
    RowGeo rowGeo (int row) const;
    int rowAt (juce::Point<float>) const;
    void cycleAtCell (juce::Point<float> pos);
    void setDestIndex (int row, int index);   // write a destination id + repaint
    void setSlotIndex (int row, bool src, int index);   // generic cell write
    float amountFromClickX (juce::Rectangle<float> bar, float x) const;
    void updateCursor (juce::Point<float> pos);
    juce::Component* hitControlAt (juce::Point<float> pos);   // shared click resolve
    juce::Point<float> targetPointFor (juce::Component* target) const;   // flash pointer end

    GoaSynthAudioProcessor& proc;
    juce::Label head;
    juce::Label hint;                    // interaction cheat-sheet, card footer
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

// Modal save dialog for user presets: name + tags + SAVE/CANCEL.
struct SavePresetOverlay : juce::Component
{
    std::function<void (const juce::String&, const juce::StringArray&, bool)> onSave;

    SavePresetOverlay();
    void paint (juce::Graphics&) override;
    void resized() override;
    void retint();
    void mouseDown (const juce::MouseEvent&) override;
    void visibilityChanged() override { if (isVisible()) name.grabKeyboardFocus(); }
    void prefill (const juce::String& s) { name.setText (s, juce::dontSendNotification); name.selectAll(); }
    juce::StringArray collectTags() const;

    juce::Rectangle<int> cardBounds() const;
    juce::Label head;
    juce::TextEditor name;
    juce::TextEditor tags;   // space-separated tags, e.g. "acid bass night"
    juce::ToggleButton sharedBtn;  // on = write to the machine-wide shared bank
    juce::TextButton saveBtn { "SAVE", "Save preset" };
    juce::TextButton cancelBtn { "CANCEL", "Cancel" };
};

// Serum-style preset browser overlay: folder tabs (ALL / FACTORY / USER),
// search box, tag filter, and a grouped click-to-load list. The editor owns
// the filtering logic; the overlay just displays rows and reports clicks.
struct PresetBrowserOverlay : juce::Component, juce::ListBoxModel
{
    struct Row
    {
        juce::String label;
        juce::String tagLabel;
        bool header = false;
        bool isUser = false;
        bool shared = false;      // from the machine-wide shared bank
        bool selected = false;
        int  visibleIndex = -1;   // index into the editor's visiblePresets
    };

    std::function<void (int)> onSelect;   // clicked a preset row
    std::function<void ()> onChanged;     // search / tag / folder changed
    std::function<void ()> onExport;      // EXPORT PACK clicked
    std::function<void ()> onImport;      // IMPORT PACK clicked
    PresetBrowserOverlay();
    void paint (juce::Graphics&) override;
    void resized() override;
    void retint();
    void mouseDown (const juce::MouseEvent&) override;

    juce::Rectangle<int> cardBounds() const;
    void setRows (const std::vector<Row>& r, int selectedRow);
    void setStatus (const juce::String& text, bool ok);
    int folder() const noexcept { return folderIdx; }   // 0 ALL, 1 FACTORY, 2 USER

    // juce::ListBoxModel
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool rowSelected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;

    juce::Label head;
    juce::TextButton closeBtn { "X", "Close" };
    juce::TextEditor search;
    juce::ComboBox tagCombo;
    juce::TextButton allTab { "ALL", "All presets" };
    juce::TextButton factoryTab { "FACTORY", "Factory presets" };
    juce::TextButton userTab { "USER", "User presets" };
    juce::TextButton exportBtn { "EXPORT PACK", "Save all user presets as a .goapack file" };
    juce::TextButton importBtn { "IMPORT PACK", "Add presets from a .goapack file" };
    juce::ListBox list;
    juce::Label status;

private:
    void syncTabs();
    std::vector<Row> rows;
    int selectedRow = -1;
    int folderIdx = 0;
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
    juce::Rectangle<float> syncCell() const;
    juce::Rectangle<float> octCell() const;
    juce::Rectangle<float> copyCell() const;   // gate strip: ARP → GATE copy
    juce::Rectangle<float> patCell() const;
    juce::Rectangle<float> dirCell() const;    // arp strip: playback direction
    juce::Rectangle<float> scaleCell() const;  // arp strip: scale quantizer
    juce::Rectangle<float> fillCell() const;   // arp strip: in-key generative fill
    void fillInKey();                          // write in-scale steps into rests
    bool patternStepsChanged();                // arp strip: step edits need repaint
    int stepAt (juce::Point<float> pos) const;
    void poke (juce::Point<float> pos, bool erase);
    void setVelocity (int step, float v01);
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
    juce::RangedAudioParameter* scalePar = nullptr; // arp strip: scale quantizer
    juce::RangedAudioParameter* rootPar  = nullptr; // arp strip: scale root
    int lastScaleIdx = -1;                          // repaint when the scale changes
    int lastRootIdx  = -1;                          // repaint when the root changes
    std::array<juce::uint8, 16> lastSteps {};       // step-value cache for repaints
    std::array<juce::RangedAudioParameter*, 16> steps {};
    std::array<juce::RangedAudioParameter*, 16> vels {};
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

    void paint (juce::Graphics&) override;
    void resized() override;

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
    bool asyncPackChooser (const juce::String& title, int browserFlags,
                           std::function<void (const juce::File&)> onChosen);
    void exportPresetPack();
    void importPresetPack();
    void reportPackStatus (const juce::String& text, bool ok);
    void saveUserPreset (const juce::String& name, const juce::StringArray& tags,
                         bool shared);
    void deleteUserPreset();
    void updatePresetTint();
    void updateArrows();

    std::vector<PresetEntry> allPresets;      // Init + factory + user, always full
    std::vector<PresetEntry> visiblePresets;  // filtered view the browser shows
    int selectedPreset = -1;                  // index into visiblePresets
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
    juce::TextButton themeBtn { "THEME", "Cycle the skin: UV Goa / steel / warm analog" };
    juce::TextButton saveBtn { "SAVE", "Save user preset" };
    juce::TextButton deleteBtn { "DEL", "Delete selected user preset" };
    std::unique_ptr<goaui::Ctl> masterCtl;
    std::unique_ptr<goaui::AiOverlay> aiOverlay;
    std::unique_ptr<goaui::SavePresetOverlay> saveOverlay;
    std::unique_ptr<goaui::PresetBrowserOverlay> presetBrowser;
    std::unique_ptr<goaui::ModOverlay> modOverlay;
    juce::TextButton modBtn { "MOD", "Open the modulation matrix (8 routable slots)" };
    std::unique_ptr<goaui::LicenseOverlay> licenseOverlay;
    std::shared_ptr<juce::FileChooser> packChooser;   // one dialog at a time

    // OSC row
    goaui::Panel oscAFrame { "OSC A", goaui::roleA };
    goaui::Panel oscBFrame { "OSC B", goaui::roleB };
    goaui::Panel filtFrame { "FILTER", goaui::roleAccent };
    goaui::Panel filt2Frame { "FILTER B", goaui::roleB };
    goaui::Panel subFrame  { "SUB", goaui::roleNeutral };
    goaui::Panel noiseFrame{ "NOISE", goaui::roleNeutral };
    goaui::WaveDisplay waveA, waveB;
    goaui::FilterGraph filtCurve;
    std::unique_ptr<goaui::Ctl> wave1Ctl, oct1Ctl, fin1Ctl, uni1Ctl, det1Ctl, wid1Ctl,
        pan1Ctl, wt1Ctl, lvl1Ctl, ph1Ctl, wave2Ctl, oct2Ctl, fin2Ctl, uni2Ctl, det2Ctl, wid2Ctl,
        pan2Ctl, wt2Ctl, lvl2Ctl, ph2Ctl, fmCtl, subWaveCtl, subOctCtl, subCtl, noiseCtl,
        ftypeCtl, cutoffCtl, resoCtl, driveCtl, keyCtl,
        ftype2Ctl, cutoff2Ctl, reso2Ctl, routeCtl,
        vMorphCtl, vResCtl, vMixCtl,             // vowel/formant filter
        fdCtl, fbCtl;                            // filter drive + feedback
    std::unique_ptr<goaui::ToggleCtl> prand1Ctl, prand2Ctl, vOnCtl, hqCtl;

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
    goaui::Panel ottFrame    { "OTT",    goaui::roleAccent };
    std::unique_ptr<goaui::Ctl> chRateCtl, chDepthCtl, chMixCtl,
        phRateCtl, phDepthCtl, phMixCtl,
        dSyncCtl, dTimeCtl, dFbCtl, dMixCtl,
        rSizeCtl, rDampCtl, rMixCtl,
        oDepthCtl, oLowCtl, oMidCtl, oHighCtl, oOutCtl,
        driftCtl, uniDetCtl, uniSpreadCtl;

    // bottom bar
    juce::TextButton octDown { "OCT-", "Octave down" }, octUp { "OCT+", "Octave up" };
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
