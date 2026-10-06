# G-AcidBase changelog

This is the source of G-AcidBase's update-feed notes, release dates, and the
product page's per-version Change Log. Both outputs are generated; never edit
the feed or the page's gacidbase-changelog marker block by hand.

Use `## [X.Y.Z] - YYYY-MM-DD` for releases, newest first, with `### Added`,
`### Changed`, or `### Fixed` and `- ` bullets. Wrapped bullet lines are supported.
An optional `## [Unreleased]` section stays out of the feed. Each released version
must have a real calendar date and at least one note. The newest released heading
must match the stable GitHub release tag in `release.json`; future notes belong
under Unreleased until that release is published.

After publishing a G-AcidBase release, add its matching section and run:

```bash
python tools/make-gacidbase-feed.py --refresh --tag vX.Y.Z
python tools/make-gacidbase-feed.py --check
```

`--refresh` fetches public GitHub release metadata, including the exact ZIP byte
size, and validates it before updating the release record, feed, or site notes. Without `--refresh`,
generation and checking are deterministic and offline. Commit this changelog,
`tools/gacidbase/release.json`, `docs/gacidbase/version.json`, and the generated
`docs/gacidbase/index.html` notes together.
See the website README for the complete command reference.

## [Unreleased]

### Added
- TOOLS → CHANGE LOG opens the full release history on the product site. Update dialogs deep-link to the offered or running version's notes.

## [1.1.0] - 2026-10-05

### Fixed
- Sequencer transport fixed: RUN is the sequencer's play button - it starts the sequence from any play mode, browsing the 50 presets keeps it running, and choosing MIDI play mode stops it.

### Added
- Animated header mark: the logo beside the plugin name runs one cycle per beat of host tempo, swells on the beat and opens up with the resonance control.
- Pattern banks and repeatable chains: hold several lines in banks and chain them with per-entry repeat counts.
- Probability, ratchets and microtiming per step, so a line can breathe without leaving the 16-step grid.
- Macros with morphing, a MIDI modulation matrix, and controller pickup plus two relative encoder modes.
- MIDI CC learn by right-click on any continuous control, and MIDI file export with drag-out to the DAW.
