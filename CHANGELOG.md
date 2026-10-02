# Changelog

All notable changes to **GoaSynth**. This file is the single source of truth
for release notes: `tools/make-changelog.py` turns it into the website's
"Release notes" section and the `notes` the plugin's update-available dialog
shows, and it prints the markdown body for a GitHub release.

    python tools/make-changelog.py              # regenerate site block + feed notes
    python tools/make-changelog.py --check      # exit 1 if any generated output is stale
    python tools/make-changelog.py --release-body 1.4.0   # markdown for the GitHub release

Format — a Keep a Changelog flavour the tool can parse:

    ## [1.4.0] - 2026-10-02        a released version (site + update feed show it)
    ## [Unreleased]                work since the last release (site + feed skip it)
    ### Added | Changed | Fixed    category subsections with "- " bullets
    A plain paragraph right after a version heading becomes the lede the site
    shows under the version's heading.

Scope — the plugin only. Everything this changelog publishes (the site's
"Release notes", the feed's notes, the GitHub release body) describes changes
to the VST itself. Website, repository and CI work never appears here: it
earns no entry and never touches the feed. The plugin's version only moves
when the VST does — a website-only update ships without a version change.

To cut a release: rename `[Unreleased]` to `[X.Y.Z] - YYYY-MM-DD`, run
`tools/make-changelog.py`, package with `tools/make-release.py`, commit,
then push and tag `vX.Y.Z` — the Release workflow builds the VST, runs the
tests and publishes the GitHub release with the notes from this file.

## [1.5.0] - 2026-10-02

The update dialog tells you what changed: release notes now travel in the
version feed the plugin already fetches, and a **View full change log** link
opens them in the browser.

### Added
- The update dialogs carry a **View full change log** link: update-available lands on the new version's own notes, up-to-date opens the release notes.
- The update-available dialog shows **What's new** — the release notes ride along in the version feed the plugin already fetches.

## [1.4.0] - 2026-10-02

A small, sharp release: the MENU gained Change Log, Check for updates always
answers, and the About card is closable.

### Added
- **MENU** gained **Change Log** — opens the release notes on the website.

### Fixed
- **Check for updates** no longer fails silently: every outcome — feed unreachable, update available, up to date — answers with a dialog over the plugin window, so no host can bury it.
- The **About** card has a close button you can actually see (✕ on the card's own corner), and clicking the darkened backdrop dismisses it.

## [1.3.0] - 2026-10-01

### Changed
- The **FILTER** panel was rearranged into a 3×2 knob grid (CUTOFF / RESO / KEY over DRIVE / F-DRIVE / F-FB) with evenly normalized gaps — bigger knobs, no cramped rows.

## [1.2.0] - 2026-09-30

This update is about housekeeping you can feel: a header MENU with an About
card and an update check.

### Added
- A **MENU** dropdown in the header: **About** shows the running version, your licence state and machine ID (one click to copy), and **Check for updates** fetches a tiny version feed once — anonymous, no account, no telemetry. Offline? It says so.

## [1.1.0] - 2026-09-28

The largest update the instrument has had.

### Added
- The trancegate gained the classic step contours: **SQUARE**, **SMOOTH**, **SAW** and **TRIANGLE** shapes with ramp-smoothed step edges.
- Arp timing rebuilt on **absolute sample times**: per-step gate length, no truncated note-offs, STRUM keeps its stagger at small buffers.
- True stereo metering with peak-hold ticks; **NOISE** gained a White / Pink choice.
- Oscillator hard sync and ring mod, manual pulse width, a three-band master EQ, and filter B / vowel / OTT / pump as modulation destinations.
- Undo / A/B / randomise / lock, WAV drag-and-drop wavetable slicing, editor size and zoom remembered.
- The bank grew to **175 patches** with a rebuilt browser (favourites, search, sort, audition).
- **NEON**, **PAPER** and **OLED** themes, 100–200 % zoom, active-step glow.
- Factory bank exposed to the DAW's program list, arp MIDI-out, AU/AUv3 and LV2 builds, and licence/trial polish.
