#!/usr/bin/env python3
"""Generate G-AcidBase's update feed and site release notes; --check never writes.

Sources: tools/gacidbase/CHANGELOG.md and tools/gacidbase/release.json.
Use --refresh after publishing a release to fetch GitHub's latest stable tag,
its installer size and the assets its download row links. Optional --tag vX.Y.Z requires that exact tag (it cannot select
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
DOWNLOAD_BEGIN = "<!-- gacidbase-download:begin -->"
DOWNLOAD_END = "<!-- gacidbase-download:end -->"
CTA_BEGIN = "<!-- gacidbase-cta:begin -->"
CTA_END = "<!-- gacidbase-cta:end -->"
VERSION_BEGIN = "<!-- gacidbase-version:begin -->"
VERSION_END = "<!-- gacidbase-version:end -->"
PRODUCT_URL = "https://dakagoa.github.io/GoaSynth/gacidbase/"
REPOSITORY_URL = "https://github.com/DakaGoa/G-AcidBase"
RELEASE_API = "https://api.github.com/repos/DakaGoa/G-AcidBase/releases/latest"
ASSET_NAME = "G-AcidBase-Windows-x64.zip"
SETUP_PREFIX = "G-AcidBase-Setup-"
SETUP_SUFFIX = ".exe"
SUMS_NAME = "SHA256SUMS.txt"
RELEASE_BASE = "https://github.com/DakaGoa/G-AcidBase/releases"
VERSION = r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)"
HEADING = re.compile(r"## \[(" + VERSION + r")\] - (\d{4}-\d{2}-\d{2})")
MONTHS = ("January", "February", "March", "April", "May", "June", "July",
          "August", "September", "October", "November", "December")


def installer_asset(version: str) -> str:
    """The Setup installer a release attaches, named for the version it installs."""
    return SETUP_PREFIX + version + SETUP_SUFFIX


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


def validate_release(release: dict, expected_tag: str | None = None) -> tuple[str, int, int | None]:
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
    # The installer is what the update dialog offers and what the download row
    # names first, so the size quoted to the user is the installer's. A release
    # that carries only the ZIP still validates: the dialog then quotes the
    # package it does have, which is what it always did.
    installers = [asset for asset in assets if asset.get("name") == installer_asset(tag[1:])]
    if len(installers) > 1:
        raise ValueError(f"release lists more than one {installer_asset(tag[1:])}")
    installer_size = installers[0].get("size") if installers else None
    if installer_size is not None and (type(installer_size) is not int or installer_size <= 0):
        raise ValueError("release installer size must be a positive integer byte count")
    return tag[1:], size, installer_size


def make_feed(changelog: str, release: dict, expected_tag: str | None = None) -> dict:
    version, zip_size, installer_size = validate_release(release, expected_tag)
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
        "installer_size": f"{(installer_size if installer_size is not None else zip_size) / (1024 * 1024):.1f} MB",
    }


def row_links(version: str, release: dict) -> list[tuple[str, str]]:
    """One link per asset the release actually carries.

    Which links appear is read from tools/gacidbase/release.json, which
    --refresh writes straight from the GitHub API after the uploads finished,
    so a link can never point at a file nobody uploaded, --check stays offline,
    and an older release that carries only the ZIP shows only the ZIP.
    """
    base = f"{RELEASE_BASE}/download/v{version}"
    names = {asset.get("name") for asset in release.get("assets", [])
             if isinstance(asset, dict)}
    links = [(f"{base}/{ASSET_NAME}", "ZIP package")]
    if installer_asset(version) in names:
        links.append((f"{base}/{installer_asset(version)}", "Installer"))
    if SUMS_NAME in names:
        links.append((f"{base}/{SUMS_NAME}", "SHA-256 checksums"))
    links.append((f"{RELEASE_BASE}/tag/v{version}", "all files"))
    return links


def download_row(version: str, release: dict) -> str:
    """The direct download row: the newest release's assets, version-named."""
    cells = []
    for index, (href, label) in enumerate(row_links(version, release)):
        if index:
            cells.append('<span class="sep">·</span>')
        cells.append(f'<a href="{href}">{label}</a>')
    return (f'<p class="release-downloads"><span class="dl-label">Download {version}</span>'
            + ''.join(cells) + '</p>')


def render_notes(changelog: str, release: dict | None = None) -> str:
    """Stable #vX.Y.Z anchors for every released section; no Unreleased leaks.

    With the release metadata, the newest version also carries the download row
    the product's other pages link to; without it (older callers, and releases
    whose assets are not recorded) the notes stay pure history.
    """
    releases = parse_changelog(changelog)
    links = ' · '.join(f'<a href="#v{item["version"]}">{item["version"]}</a>'
                       for item in releases)
    lines = [f'      <nav class="release-versions" aria-label="Release versions">{links}</nav>']
    row_version = release.get("tag_name", "")[1:] if release else None
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
        lines.append('        </ul>')
        # The row rides under the newest released version: that is the one
        # people download, and older sections stay pure history.
        if version == row_version:
            lines.append('        ' + download_row(version, release))
        lines.append('      </article>')
    return '\n'.join(lines)


def render_download(release: dict) -> str:
    """The download section's row, generated from the same recorded assets."""
    return '      ' + download_row(release["tag_name"][1:], release)


def primary_asset(version: str, release: dict) -> str:
    """What the one-click button fetches: the installer when the release has
    one, the package when it does not."""
    names = {asset.get("name") for asset in release.get("assets", [])
             if isinstance(asset, dict)}
    return installer_asset(version) if installer_asset(version) in names else ASSET_NAME


def render_cta(release: dict) -> str:
    """The hero's one-click download button, versioned from the release."""
    version = release["tag_name"][1:]
    url = f"{RELEASE_BASE}/download/v{version}/{primary_asset(version, release)}"
    return ('          <a class="btn btn-primary btn-lg" href="' + url
            + '">Download v' + version + ' — Windows</a>')


def render_version_stat(release: dict) -> str:
    """The hero's "current release" stat - the version, never a hand edit."""
    return ('          <li><b>v' + release["tag_name"][1:] + '</b><span>current release</span></li>')


def replace_block(page: str, begin: str, end: str, body: str, indent: str = '      ') -> str:
    """Replace one marker block, refusing a page whose contract is broken."""
    name = begin.strip('<!-> ')
    if page.count(begin) != 1 or page.count(end) != 1:
        raise ValueError(f"product page requires exactly one {name} marker pair")
    start = page.index(begin) + len(begin)
    stop = page.index(end)
    if stop < start:
        raise ValueError(f"product page {name} markers are reversed")
    return page[:start] + '\n' + body + '\n' + indent + page[stop:]


def update_page(page: str, changelog: str, release: dict | None = None) -> str:
    """Replace only the generated blocks; fail if the page contract is broken."""
    page = replace_block(page, NOTES_BEGIN, NOTES_END, render_notes(changelog, release))
    if release is not None:
        page = replace_block(page, DOWNLOAD_BEGIN, DOWNLOAD_END, render_download(release))
        page = replace_block(page, CTA_BEGIN, CTA_END, render_cta(release), '          ')
        page = replace_block(page, VERSION_BEGIN, VERSION_END, render_version_stat(release), '          ')
    return page


def fetch_release() -> dict:
    request = urllib.request.Request(RELEASE_API, headers={
        "Accept": "application/vnd.github+json", "User-Agent": "DakaGoaAudio-feed-generator"})
    with urllib.request.urlopen(request, timeout=20) as response:
        release = json.load(response)
    validate_release(release)
    interesting = {ASSET_NAME, installer_asset(release["tag_name"][1:]), SUMS_NAME}
    # Save only public fields that the generator actually consumes.
    return {**{key: release[key] for key in
              ("tag_name", "html_url", "draft", "prerelease", "published_at")},
            "assets": [{"name": asset["name"], "size": asset["size"]}
                       for asset in release["assets"] if asset["name"] in interesting]}


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
        wanted_page = update_page(page.decode('utf-8'), changelog, release).encode('utf-8')
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
