#!/usr/bin/env python3
"""Generate G-AcidBase's update feed and site release notes; --check never writes.

Sources: tools/gacidbase/CHANGELOG.md and tools/gacidbase/release.json.
Use --refresh after publishing a release to fetch GitHub's latest stable tag
and ZIP size. Optional --tag vX.Y.Z requires that exact tag (it cannot select
an older release). Validation must finish before any output is written.
Plain Python 3; no dependencies, credentials, or auto-publishing.
"""
from __future__ import annotations

import argparse
from datetime import date, datetime
import difflib
import html
import json
from pathlib import Path
import re
import sys
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parent.parent
CHANGELOG = ROOT / "tools/gacidbase/CHANGELOG.md"
RELEASE = ROOT / "tools/gacidbase/release.json"
FEED = ROOT / "docs/gacidbase/version.json"
PAGE = ROOT / "docs/gacidbase/index.html"
NOTES_BEGIN = "<!-- gacidbase-changelog:begin -->"
NOTES_END = "<!-- gacidbase-changelog:end -->"
PRODUCT_URL = "https://dakagoa.github.io/GoaSynth/gacidbase/"
REPOSITORY_URL = "https://github.com/DakaGoa/G-AcidBase"
RELEASE_API = "https://api.github.com/repos/DakaGoa/G-AcidBase/releases/latest"
ASSET_NAME = "G-AcidBase-Windows-x64.zip"
VERSION = r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)"
HEADING = re.compile(r"## \[(" + VERSION + r")\] - (\d{4}-\d{2}-\d{2})")
MONTHS = ("January", "February", "March", "April", "May", "June", "July",
          "August", "September", "October", "November", "December")


def plain(text: str) -> str:
    text = re.sub(r"\[([^\]]+)\]\([^)]*\)", r"\1", text)
    text = re.sub(r"\*\*(.+?)\*\*", r"\1", text)
    text = re.sub(r"`([^`]+)`", r"\1", text)
    return text.strip()


def parse_changelog(text: str) -> list[dict]:
    """Read released sections only; reject ambiguity instead of guessing."""
    releases: list[dict] = []
    seen: set[str] = set()
    current = None
    category = None
    in_unreleased = False
    last_bullet = False
    for number, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if line.startswith("## "):
            current, category, last_bullet = None, None, False
            if line == "## [Unreleased]":
                if in_unreleased or releases:
                    raise ValueError("Unreleased must occur once, before released sections")
                in_unreleased = True
                continue
            match = HEADING.fullmatch(line)
            if not match:
                raise ValueError(f"changelog line {number}: expected ## [X.Y.Z] - YYYY-MM-DD")
            version, iso = match.groups()
            released = date.fromisoformat(iso)
            if version in seen:
                raise ValueError(f"duplicate changelog version {version}")
            if releases and tuple(map(int, version.split('.'))) >= tuple(map(int, releases[-1]['version'].split('.'))):
                raise ValueError("released changelog sections must be newest first")
            seen.add(version)
            current = {"version": version, "date": released, "notes": [], "entries": []}
            releases.append(current)
            continue
        if current is None:
            continue
        if line.startswith("### "):
            category = line[4:]
            if category not in {"Added", "Changed", "Fixed", "Removed", "Deprecated", "Security"}:
                raise ValueError(f"changelog line {number}: unknown category {category!r}")
            last_bullet = False
        elif line.startswith("- ") or line == "-":
            if category is None:
                raise ValueError(f"changelog line {number}: note needs a category")
            note = plain(line[2:])
            if not note:
                raise ValueError(f"changelog line {number}: empty note")
            current["notes"].append(note)
            current["entries"].append((category, note))
            last_bullet = True
        elif line and raw.startswith(("  ", "\t")) and last_bullet:
            continuation = " " + plain(line)
            current["notes"][-1] += continuation
            kind, previous = current["entries"][-1]
            current["entries"][-1] = (kind, previous + continuation)
        elif line and category is not None:
            raise ValueError(f"changelog line {number}: expected a bullet or indented continuation")
        else:
            last_bullet = False
    if not releases:
        raise ValueError("changelog has no released sections")
    if any(not release["notes"] for release in releases):
        raise ValueError("every released changelog section needs at least one note")
    return releases


def validate_release(release: dict, expected_tag: str | None = None) -> tuple[str, int]:
    if not isinstance(release, dict):
        raise ValueError("release metadata must be an object")
    tag = release.get("tag_name")
    if not isinstance(tag, str) or not re.fullmatch("v" + VERSION, tag):
        raise ValueError("release tag must be vMAJOR.MINOR.PATCH (stable only)")
    if expected_tag is not None and tag != expected_tag:
        raise ValueError(f"release tag is {tag}, expected {expected_tag}")
    if release.get("draft") is not False or release.get("prerelease") is not False:
        raise ValueError("feed requires a published stable release, not draft/prerelease")
    if release.get("html_url") != REPOSITORY_URL + "/releases/tag/" + tag:
        raise ValueError("release URL does not name the G-AcidBase repository and tag")
    published = release.get("published_at")
    if not isinstance(published, str) or not published.endswith('Z'):
        raise ValueError("release needs a published_at UTC timestamp")
    datetime.fromisoformat(published.replace('Z', '+00:00'))
    assets = release.get("assets")
    if not isinstance(assets, list) or any(not isinstance(asset, dict) for asset in assets):
        raise ValueError("release assets must be a list of objects")
    matching = [asset for asset in assets if asset.get("name") == ASSET_NAME]
    if len(matching) != 1:
        raise ValueError(f"release must contain exactly one {ASSET_NAME}")
    size = matching[0].get("size")
    if type(size) is not int or size <= 0:
        raise ValueError("release ZIP size must be a positive integer byte count")
    return tag[1:], size


def make_feed(changelog: str, release: dict, expected_tag: str | None = None) -> dict:
    version, size = validate_release(release, expected_tag)
    newest = parse_changelog(changelog)[0]
    if newest["version"] != version:
        raise ValueError(f"newest changelog release {newest['version']} does not match release tag v{version}")
    released = newest["date"]
    return {
        "latest": version,
        "url": PRODUCT_URL,
        "notes": newest["notes"],
        "released": f"{released.day} {MONTHS[released.month - 1]} {released.year}",
        # Preserve the existing plugin/site size presentation (MiB rounded as MB).
        "installer_size": f"{size / (1024 * 1024):.1f} MB",
    }


def render_notes(changelog: str) -> str:
    """Stable #vX.Y.Z anchors for every released section; no Unreleased leaks."""
    releases = parse_changelog(changelog)
    links = ' · '.join(f'<a href="#v{item["version"]}">{item["version"]}</a>'
                       for item in releases)
    lines = [f'      <nav class="release-versions" aria-label="Release versions">{links}</nav>']
    for item in releases:
        version, released = item["version"], item["date"]
        lines.extend([
            f'      <article class="release-entry" aria-labelledby="v{version}">',
            f'        <h3 id="v{version}"><a href="#v{version}">Version {version}</a> '
            f'<time class="ver-date" datetime="{released.isoformat()}">'
            f'{released.day} {MONTHS[released.month - 1]} {released.year}</time></h3>',
            '        <ul class="changelog">',
        ])
        for category, note in item["entries"]:
            lines.append(f'          <li><span class="cat">{html.escape(category)}</span> '
                         f'{html.escape(note)}</li>')
        lines.extend(['        </ul>', '      </article>'])
    return '\n'.join(lines)


def update_page(page: str, changelog: str) -> str:
    """Replace only the generated block; fail if the page contract is broken."""
    if page.count(NOTES_BEGIN) != 1 or page.count(NOTES_END) != 1:
        raise ValueError("product page requires exactly one release-notes marker pair")
    start = page.index(NOTES_BEGIN) + len(NOTES_BEGIN)
    end = page.index(NOTES_END)
    if end < start:
        raise ValueError("product page release-notes markers are reversed")
    return page[:start] + '\n' + render_notes(changelog) + '\n      ' + page[end:]


def fetch_release() -> dict:
    request = urllib.request.Request(RELEASE_API, headers={
        "Accept": "application/vnd.github+json", "User-Agent": "DakaGoaAudio-feed-generator"})
    with urllib.request.urlopen(request, timeout=20) as response:
        release = json.load(response)
    validate_release(release)
    # Save only public fields that the generator actually consumes.
    return {**{key: release[key] for key in
              ("tag_name", "html_url", "draft", "prerelease", "published_at")},
            "assets": [{"name": asset["name"], "size": asset["size"]}
                       for asset in release["assets"] if asset["name"] == ASSET_NAME]}


def render(data: dict) -> str:
    return json.dumps(data, ensure_ascii=False, indent=2) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="compare exact generated bytes; never write or fetch")
    parser.add_argument("--refresh", action="store_true", help="fetch latest stable GitHub release metadata")
    parser.add_argument("--tag", help="require the release tag to match exactly (e.g. v1.1.0)")
    args = parser.parse_args()
    if args.check and args.refresh:
        parser.error("--check and --refresh cannot be combined")
    if args.tag is not None and not re.fullmatch("v" + VERSION, args.tag):
        parser.error("--tag must be vMAJOR.MINOR.PATCH")
    try:
        release = fetch_release() if args.refresh else json.loads(RELEASE.read_text(encoding="utf-8"))
        changelog = CHANGELOG.read_text(encoding="utf-8")
        wanted = render(make_feed(changelog, release, args.tag))
        current = FEED.read_bytes() if FEED.exists() else b""
        page = PAGE.read_bytes()
        wanted_page = update_page(page.decode('utf-8'), changelog).encode('utf-8')
        if args.check:
            stale = False
            for label, actual, expected in (("feed", current, wanted.encode('utf-8')),
                                            ("site release notes", page, wanted_page)):
                if actual == expected:
                    continue
                stale = True
                print(f"G-AcidBase {label} is stale; run python tools/make-gacidbase-feed.py", file=sys.stderr)
                print(''.join(difflib.unified_diff(actual.decode('utf-8').splitlines(True),
                      expected.decode('utf-8').splitlines(True), fromfile=f"current {label}",
                      tofile=f"generated {label}")), file=sys.stderr)
            if stale:
                return 1
            print(f"G-AcidBase feed and site release notes match changelog and {release['tag_name']} (offline check)")
            return 0
        if args.refresh:
            RELEASE.write_text(render(release), encoding="utf-8", newline="\n")
        if current != wanted.encode('utf-8'):
            FEED.write_text(wanted, encoding="utf-8", newline="\n")
            print("Generated docs/gacidbase/version.json")
        else:
            print("docs/gacidbase/version.json is already current")
        if page != wanted_page:
            PAGE.write_bytes(wanted_page)
            print("Generated per-version release notes in docs/gacidbase/index.html")
        else:
            print("docs/gacidbase/index.html release notes are already current")
        return 0
    except (ValueError, OSError, urllib.error.URLError) as exc:
        print(f"G-AcidBase feed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
