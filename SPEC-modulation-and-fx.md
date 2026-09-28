# Spec — modulation matrix & FX chain extensions

**Status: partly implemented.** The §1.7 boundary fix is done, the M4 destination
set is done for the rows listed below, and the master EQ from the FX list is done.
Everything else in this document is still planning.

### What has landed since this was written

- **§1.7 fixed.** `publishGlobal` no longer tests `s.dst < 14`; it uses
  `param::mdFirstGlobalDest` (derived from the enum), so inserting a per-voice
  destination above the FX block can no longer silently reclassify it.
- **§1.6 partly fixed.** Both switches in `ModMatrix` now use *named enum cases*
  (`case param::mdCutoff:` …) instead of bare indices, so inserting a row can no
  longer retarget an existing destination. The multipliers are still duplicated
  between the switch and `modDestList()`'s `range` — that part is unchanged.
- **M4 per-voice destinations done:** FILTER B cutoff (`CUTOFF B`), FILTER B
  resonance (`RESO B`), pulse width (`PULSE W`), vowel morph (`VOWEL`), analog
  character (`ANALOG`). Plus a new `pulseWidth` parameter, since PWM previously
  had no manual duty control at all.
- **M4 global destinations done:** OTT depth (`OTT DEPTH`) and sidechain pump
  depth (`PUMP DEPTH`).
- **FX chain:** the master 3-band EQ (low shelf / peaking mid / high shelf) is
  implemented, between `masterGain` and the limiter, bypassed at 0 dB.
- Destination count went from 20 to 26.

Still open from this document: MSEG shapes, LFO phase/delay/fade/unipolar/retrig,
envelope curves + loop + a third aux envelope, mod-of-mod, more macros,
distortion/bitcrush/wavefolder, flanger, per-FX bypass and FX order, the
delay/reverb extensions, and the remaining metering rows.

Covers the two areas picked out of the feature review:

1. **Modulation matrix** — MSEG shapes, LFO phase/delay/fade/unipolar/retrig,
   envelope curves + loop + a third aux envelope, extra sources and destinations,
   mod-of-mod, more macros.
2. **FX chain** — distortion/bitcrush/wavefolder, flanger, EQ, stereo utility,
   per-FX bypass + FX order, delay/reverb extensions, meters.

The point of this document is that the two tables are *not* equally cheap. Some
rows are a knob and a `switch` case; two of them are multi-day DSP projects. The
constraints in §1 are what decide which is which, and they were read out of the
source rather than assumed.

---

## 1. Ground rules — what adding anything actually costs

These were established by reading the code, and they apply to every stage below.
Several of them are counter-intuitive.

### 1.1 A new 0..1 parameter costs almost nothing

`createParameterLayout()` (`Source/Parameters.cpp:3`) exposes `addF` / `addI` /
`addB` / `addC` helpers. One line adds a parameter, and `liveValueText()` has a
**percentage fallback** at `Source/PluginEditor.cpp:362-364`:

```cpp
// Everything else is a 0..1 amount (mixes, depths, sizes, phases):
return juce::String (juce::roundToInt (v * 100.0f)) + unit ("%");
```

So a plain 0..1 float needs **no** readout work, and `OverlayTest`'s unit sweep
(`tests/OverlayTest.cpp:680-748`) passes it automatically.

### 1.2 Any other range needs two edits, not one

A parameter with a real unit (Hz, ms, dB, ct, deg, s, oct) needs:

- a branch in `liveValueText()` (`Source/PluginEditor.cpp:320-365`), **and**
- the unit added to the accepted list in `tests/OverlayTest.cpp:707`:
  `{ "Hz", "ms", "ct", "dB", "s", "%", "oct" }`.

Miss the second and the sweep fails with `unknown unit: <id> -> "<value>"`.

### 1.3 The extremes sweep punishes long readouts

`tests/OverlayTest.cpp:750-830` drives every knob to min and max and fails if the
**compact** readout overflows the on-knob band (knob width minus the space mod
dots claim). So a new parameter with a wide range must produce short compact
text — copy the `1500 -> "1.5k"` treatment at `Source/PluginEditor.cpp:334-336`.

### 1.4 Factory presets do **not** need touching

`applyPreset()` resets every parameter to its default *before* applying the
patch's own values (`Source/PluginEditor.cpp:4633-4634`):

```cpp
for (auto* par : proc.getParameters())
    par->setValueNotifyingHost (par->getDefaultValue());
```

So all 175 factory presets inherit the new default automatically. This is a
significant saving and it means §1.5 is the only preset-compatibility problem.

### 1.5 …but the **user-preset** path does not reset, and that will bite

`applyStateXml()` (`Source/PluginProcessor.cpp:839-867`) goes straight to
`apvts.replaceState()`. It does **not** reset to defaults first, unlike
`applyPreset`. JUCE's `replaceState` applies the parameters present in the
incoming tree and leaves absent ones at their current value — so the moment a
parameter is added, **every existing `.goapreset` predating it inherits that
parameter's value from whatever patch was loaded before it.**

**Prerequisite for any stage below:** confirm this with a test, and if confirmed,
add the same reset loop to `applyStateXml`. Otherwise stage M1 ships a
user-preset bug on day one. This is a bug in the *existing* code that only
becomes visible once there is a new parameter to be missing.

### 1.6 Mod destinations are position-dependent and duplicated in four places

Adding a destination is the most expensive kind of parameter here:

| Where | What |
| --- | --- |
| `Source/Parameters.h:223-230` | the `ModDestId` enum |
| `Source/Parameters.h:232-257` | `modDestList()` — the `range` used by the UI arcs |
| `Source/SynthEngine.h:359-375` | `ModMatrix::computeAll` — per-voice switch, multipliers hardcoded (`a * 4.0f`, `a * 1200.0f`, …) |
| `Source/SynthEngine.h:395-439` | `ModMatrix::publishGlobal` — global FX switch |

Note the multipliers are **hardcoded in the switch** and *also* present as
`range` in `modDestList()`. They must agree; nothing enforces it. Worth collapsing
into one source of truth while adding rows.

### 1.7 The per-voice/global boundary is a magic number — fix it first

`publishGlobal` decides scope with a literal (`Source/SynthEngine.h:402`):

```cpp
if (s.dst < 14 || s.src == 0 || s.amt == 0.0f)
    continue;
```

`14` is `mdDelayTime`. So **a new per-voice destination appended at the end of
the list is silently treated as a global FX destination** — and since
`computeAll`'s `switch` has a `default: break`, it is also silently ignored
per-voice. Net effect: the routing appears to do nothing.

Replace the literal with a named predicate (e.g. `isGlobalDest (int)`) before
adding destinations. The destination *parameter* range is derived from the list
size (`Source/Parameters.cpp:225-227`), so appending also extends the range
automatically — that part is free.

### 1.8 Mod sources are also duplicated in four places

`modSourceName()` (`Source/Parameters.h:202-208`), plus a switch in
`computeAll` (`SynthEngine.h:330-343`), a second in `publishGlobal`
(`SynthEngine.h:405-418`), and a third for the live mod-dot/arc values in
`Ctl::paint` (`Source/PluginEditor.cpp:467-480`). `modDotSources` /
`paintModDot` add a colour mapping on top. Four switches that must stay in step.

### 1.9 Routable controls must carry their parameter id

`Ctl::paramId` drives tooltips, the mod dots, the depth arcs and pick-to-assign
(`Source/PluginEditor.cpp:426-531`). A new control without it goes silent in all
four. New mod *sources* need ids on their widget too (`LfoGraph::rateParamId`,
`EnvGraph::attackParamId()`). This is documented in README "UI conventions" and
is easy to forget.

### 1.10 The editor is a fixed grid, and the FX row is full

`defaultWidth = 1120`, `defaultHeight = 780` (`Source/PluginEditor.h:875-876`).
The FX row is a weighted 21-unit split across seven frames
(`Source/PluginEditor.cpp:5346-5371`). There is no room to append a module —
new FX need either a **fourth row** (window height change → every screenshot in
`docs/assets/` and the site's spec table churns) or a **paged/tabbed FX row**.
Recommend the latter. The `ModOverlay` fills the whole editor
(`PluginEditor.cpp:3984`), so the matrix card does have room for more rows.

### 1.11 Two smaller traps

- `getTailLengthSeconds()` returns `6.0` (`Source/PluginProcessor.h:48`). If
  reverb freeze is exposed, this must rise or bounces cut the tail off.
- `DocsCheck` (`tests/DocsCheck.cpp`) fails if the plugin creates a file the
  privacy policy doesn't list. **Prefer persisting settings as parameters rather
  than new files** — e.g. FX order as a choice parameter, not an `order.txt`.

---

## 2. Modulation matrix — staged plan

Ordered cheapest-first. Each stage is independently shippable.

### Stage M1 — LFO depth: phase, delay, fade-in, unipolar, retrig, LFO 3

Cheapest stage with the most audible payoff, and it needs no refactor.

- **Parameters:** `lfo1Phase`/`lfo2Phase` (0..360 → unit branch `deg`),
  `lfo1Delay`/`lfo1Fade` (0..2 s → unit branch `s`), `lfo1Unipolar` (bool),
  `lfo1Retrig` (bool), and a full `lfo3*` set mirroring `lfo1*`.
- **Engine:** extend `Lfo` (`Source/SynthEngine.h:267-276`) with
  `phaseOffset`, `delaySamples`, `fadeSamples`, `unipolar`, `retrig`; apply in
  `Lfo::next`. The LFO is **already per-voice** (`GoaVoice` owns `lfo1, lfo2`),
  so per-voice retrig is nearly free. Free-running needs a transport-derived
  phase — reuse the `StepClock` ppq approach (`Source/PluginProcessor.h:19-29`).
- **UI:** `layoutLfo()` (`Source/PluginEditor.cpp:5341-5344`) is already shared by
  both LFO panels — one helper edit covers all three.
- **Tests (RoundTrip):** retrig starts every note at the same phase; unipolar
  output never goes negative; delay holds the output at zero for the delay window.
- **Risk:** `targets` (`Source/Parameters.cpp:36`) is shared by all LFOs and
  indexed by the engine. Appending entries is the established pattern (VOWEL was
  appended), so no renumbering.

### Stage M2 — envelope curves, loop/hold, third aux envelope

More work than it looks: `GoaVoice` uses `juce::ADSR` directly
(`Source/SynthEngine.h:475`), and `juce::ADSR` exposes **no curve control** —
only attack/decay/sustain/release. Exponential/log curves mean writing a small
replacement envelope, not flipping a flag.

- **Parameters:** `envCurve` per envelope, `ampLoop`/`filtLoop` (bool + rate),
  and a third envelope (`auxA/auxD/auxS/auxR`, `auxCurve`).
- **Engine:** custom `Env` struct replacing the two `juce::ADSR` members; add
  `GoaVoice::auxEnv`; add source id 11 to `modSourceName()` and to both switches
  (§1.8).
- **Value:** loop mode turns an envelope into a slow shapeable LFO, which is a
  real sound-design win for evolving pads and Goa "breathing" leads.

### Stage M3 — extra sources

Note number, pitch bend, expression/breath CC, per-note random, transport, LFO 3.

- Four switches to update (§1.8), plus a colour mapping.
- **Per-voice sources need a signature change**: `computeAll`
  (`Source/SynthEngine.h:319-322`) currently takes only LFO/env/velocity/MW/AT/S&H
  scalars. Note number and per-note random live in `GoaVoice` and must be passed
  in. Roll the per-note random in `startNote`.
- **Transport position is the expensive one** — ppq is a processor-level value and
  is not currently plumbed to the voices. Budget it separately.

### Stage M4 — extra destinations

**Blocked on §1.7.** Do the boundary fix first or these silently do nothing.

- **Per-voice:** sub level, unison detune/width, fDrive/fFeedback, vowel morph.
  `osc1Oct`/`osc2Oct` are integer parameters — modulating them needs hysteresis or
  the value will zipper; recommend exposing them as continuous detune in cents
  instead, or skip.
- **Global (block-rate, "deepest wins"):** arp rate, gate depth, pump depth,
  master gain, stereo width. These belong in the `Global` publish path
  (`Source/SynthEngine.h:388-393`), not the per-voice one.
- **LFO shape as a destination: recommend dropping it.** Modulating a discrete
  choice parameter is a poor fit. If a "wave morph" is wanted, it belongs on the
  MSEG (M5), not the four fixed shapes.

### Stage M5 — drawable MSEG LFO shapes *(largest item)*

- Reuses the lock-free `UserWave` publish pattern
  (`Source/SynthEngine.h:21-70`) with a single-cycle table (~128 points) and a
  mode switch in `Lfo::next`.
- UI mirrors the wavetable editor's gestures (left-drag draw, right-drag erase,
  double-click reset) — that interaction code already exists to copy from.
- **Recommend doing this last.** It is the biggest single piece of work in either
  table and the least urgent: M1's phase/unipolar/retrig covers most of the
  practical gap for far less.

### Stage M6 — mod-of-mod, more macros

- **Mod-of-mod** (a slot amount as a destination) means the matrix writes to its
  own slots: needs a defined evaluation order and costs one block of latency.
  Cheaper alternative that gets most of the value: allow a slot's source to be
  "MACRO" and let a macro sum two sources.
- **More macros:** 2 → 4 knobs, with C/D on CC 16/17. Mechanical, and it unblocks
  the mod-of-mod use case without new evaluation semantics.

---

## 3. FX chain — staged plan

### Stage F1 — EQ + stereo utility *(cheapest, immediate mixing value)*

- **Params:** `fxTilt` (dB), `fxWidth` (0..2 → unit branch), `monoBelow` (Hz →
  unit branch).
- **Placement:** after reverb, before `masterGain`
  (`Source/PluginProcessor.cpp:700-723`).
- **Tests:** width 0 gives identical L and R; mono-maker removes side energy
  below the corner and leaves it above.
- No new DSP framework needed — one tilt shelf and one M/S matrix.

### Stage F2 — distortion / bitcrush / wavefolder insert

- A `Dist` struct beside `Ott` (`Source/PluginProcessor.h:129-150`) with the same
  `prepare/reset/process` shape — that is the established pattern for a
  processor-level effect here.
- **Insert after OTT, before the delay**, so the delay repeats the mangled signal.
- **Params:** `distMode` {OFF, TUBE, FOLD, CRUSH, RECT}, `distDrive`, `distTone`,
  `distMix`.
- **Tests:** OFF is bit-transparent; CRUSH reduces the count of distinct sample
  values; FOLD output stays bounded before the limiter.
- **Known limitation to document:** `masterHQ` oversamples the *voice* path only
  (`Source/PluginProcessor.h:204-207`), so a post-FX waveshaper will alias. Either
  document it, or run this stage in a 2x oversampler when HQ is on.

### Stage F3 — flanger

- Cheap: `juce::dsp::Chorus` is already used for the chorus
  (`Source/PluginProcessor.h:153`). A flanger is the same delay line with a much
  shorter centre delay, feedback, and a negative mix — a second instance with
  `setCentreDelay (1..3 ms)` and `setFeedback (~0.6)`, or a dedicated short delay
  line for finer control.
- **Params:** `flRate`, `flDepth`, `flFb`, `flMix`.

### Stage F4 — per-FX bypass + FX order

- **Per-FX bypass:** follow the rule the OTT already sets — depth 0 is a *true*
  DSP bypass (`Source/Parameters.h:112-114`). Give each module an explicit on/off
  bool and skip the `process()` call entirely rather than passing mix 0, so a
  bypassed module costs nothing.
- **FX order: recommend fixed preset orders** (e.g. CLASSIC / DIST-LAST /
  WET-FIRST) as a choice parameter, **not** free drag-and-drop. Free reordering of
  a nine-stage chain with per-stage state is a large refactor with a hard-to-test
  failure mode, and the audible win over three curated orders is small. This is a
  place where I would push back on the original suggestion.

### Stage F5 — delay & reverb extensions

- **Delay** (`delayL/delayR`, `Source/PluginProcessor.h:155`, processed at
  `PluginProcessor.cpp:636-645`):
  - *Multi-tap* — easy, `DelayLine` supports arbitrary read taps.
  - *HPF damping* — one extra one-pole in the feedback path (a 5 kHz LPF already
    sits there per the README).
  - *Tape mode* — modulate the read delay with a slow LFO; reuse the analog wow
    phase that already exists (`GoaVoice::wowPhase`).
  - *Reverse* — needs a second buffer written backwards. Genuinely more work.
- **Reverb** (`juce::dsp::Reverb`, `Source/PluginProcessor.h:156`):
  - *Freeze already exists in JUCE* (`Parameters::freezeMode`) — exposing it is
    nearly free, and it is the highest-value item in this stage. Raise
    `getTailLengthSeconds()` (§1.11).
  - *Pre-delay* — a short delay line in front. Easy.
  - *Width* — `Parameters::width` already exists; just expose it.
  - **Hall/plate variants are not available** from `juce::dsp::Reverb`. That needs
    a custom FDN and is a project in itself — recommend cutting it from scope.

### Stage F6 — meters

- `uiLevel` already exists (`Source/PluginProcessor.h:107`) as a smoothed level for
  the swirl backdrop. Add peak + clip-hold atomics beside it, a voice count, and
  OTT gain reduction published from `Ott::process`.
- **UI:** the bottom bar's right-hand column (`Source/PluginEditor.cpp:5405-5420`)
  has room for a compact strip.
- **Tests:** driving the synth to clipping latches the clip flag; silence clears it.

---

## 4. Cost table — what each new parameter drags along

| If the parameter is… | You must also touch |
| --- | --- |
| 0..1 float, not routable | **nothing** — the percentage fallback covers the readout |
| any other range (Hz/ms/dB/ct/deg/s/oct) | a branch in `liveValueText()` **and** the unit list in `OverlayTest.cpp:707` |
| a mod destination | `modDestList()` + `computeAll` switch + `publishGlobal` switch + the arc lookup in `Ctl::paint` |
| a mod source | `modSourceName()` + both engine switches + `Ctl::paint`'s live switch + `modDotSources` colour map |
| a choice shown in a combo | nothing extra (combos skip the value readout) |
| shown as a knob | set `Ctl::paramId`, or tooltips/dots/pick-to-assign go silent |
| persistent but not a parameter | `DocsCheck` fails unless the privacy policy lists the path — prefer a parameter |

---

## 5. Recommended order

```
M1  LFO phase/delay/fade/unipolar/retrig/LFO 3     small, very audible
F1  EQ + stereo utility                            small, immediate value
F3  flanger                                        small
F2  distortion / bitcrush / wavefolder             medium
M2  envelope curves + loop + third envelope        medium, larger than it looks
──  §1.7 boundary fix + §1.5 applyStateXml fix     prerequisite refactor
M4  extra destinations                             unblocked by the fix above
F4  per-FX bypass + preset FX orders               medium
F5  delay/reverb extensions (freeze first)         medium
M3  extra sources (transport last)                 medium
M6  mod-of-mod / 4 macros                          small-medium
F6  meters                                         small
M5  drawable MSEG                                  large — do last
```

Rationale: M1 and F1/F3 are small and immediately audible; M4 is gated on a
refactor that is worth doing on its own merits; M5 is the largest and least
urgent item in either table.

---

## 6. Verification per stage

1. **Build.** `unset http_proxy https_proxy` first — the sandbox exports both
   `HTTP_PROXY` and `http_proxy` and MSBuild's env dictionary is case-insensitive,
   which fails as `MSB6001`.
   ```bash
   cmake --build build --config Release --parallel
   ```
2. **Suite.** `ctest --test-dir build -C Release` → **7/7**.
3. **Read the sweeps, don't just check the exit code.** `OverlayTest` prints
   `[sweep] N knobs checked` — N must **rise** with each new knob, and there must
   be **zero** `unknown unit:` and zero `overflow:` lines. A passing run with an
   unchanged N means the new control isn't in the tree.
4. **RoundTrip** phases print `[phase] ...` — add one per stage.
5. **Docs.** README's "Sound engine" / "FX chain" bullets and the spec table in
   `docs/index.html`. `DocsCheck` does not guard these, but the project's
   convention (and the reason `DocsCheck` exists) is that the site matches the
   plugin. `PublishCheck` only guards `docs/`; this file is outside it on purpose.

---

## 7. Open questions

1. **MSEG** — 128-point table, and should the shape be per-LFO or shared between
   LFO 1/2/3?
2. **FX order** — accept fixed preset orders instead of free drag-and-drop?
   (Recommended.)
3. **Reverb** — freeze + pre-delay + width only, cutting hall/plate? (Recommended;
   hall/plate needs a custom FDN.)
4. **Matrix UI** — should new destinations scroll the existing 8-slot card, or
   grow the card into the free `ModOverlay` space?
5. **Scope check** — is M5 (MSEG) wanted at all in this pass, or deferred?
