#!/usr/bin/env python3
"""Cut the distributable GoaSynth release package for Windows.

One command turns the built VST3 bundle into the ZIP buyers download, and
publishes the numbers they verify it with:

    python tools/make-release.py

What it writes
--------------
    dist/GoaSynth-<version>-win64.zip    the package: bundle, licence helper,
                                         README.txt, EULA.txt
    dist/MANIFEST.txt                    every member, size and SHA-256
    dist/SHA256SUMS.txt                  copy of the published checksum file
    docs/downloads/SHA256SUMS.txt        the published checksum file (served)
    docs/index.html                      the "Verify your download" block,
                                         regenerated from the real hashes

The ZIP is deterministic: entries are sorted, timestamps come from the plugin
binary, and the compression level is pinned. Re-running it on the same
artefact produces the same bytes and so the same SHA-256 - which is what makes
a published checksum worth anything.

It refuses to package
---------------------
  * a tree with modified tracked files (the release must come from a commit);    * a plugin binary older than the newest source file under Source/ or
      CMakeLists.txt - the trap where a build "succeeds" without relinking and
      the ZIP quietly ships last week's code;
  * a missing bundle or licence helper.

Plain Python 3, no third-party modules, nothing to install.
"""
from __future__ import annotations

import hashlib
import html
import re
import subprocess
import sys
import zipfile
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build"
VST3_BUNDLE = BUILD / "GoaSynth_artefacts" / "Release" / "VST3" / "GoaSynth.vst3"
VST3_BINARY = VST3_BUNDLE / "Contents" / "x86_64-win" / "GoaSynth.vst3"
LICENSE_HELPER = BUILD / "GoaSynthLicense_artefacts" / "Release" / "GoaSynthLicense.exe"
DIST = ROOT / "dist"
INDEX_HTML = ROOT / "docs" / "index.html"
PUBLISHED_SUMS = ROOT / "docs" / "downloads" / "SHA256SUMS.txt"
EULA_HTML = ROOT / "docs" / "legal" / "eula.html"

REPO_URL = "https://github.com/Y4m4/GoaSynth"
SITE_URL = "https://y4m4.github.io/GoaSynth/"


def die(message: str) -> None:
    print("make-release: " + message, file=sys.stderr)
    sys.exit(1)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git(*args: str) -> str:
    result = subprocess.run(
        ["git", *args], cwd=ROOT, capture_output=True, text=True, check=True
    )
    return result.stdout.strip()


def pretty_size(count: int) -> str:
    if count >= 1_000_000:
        return f"{count / 1_000_000:.1f} MB"
    if count >= 1_000:
        return f"{count / 1_000:.0f} kB"
    return f"{count} bytes"


def thousands(count: int) -> str:
    return f"{count:,}"


# ---------------------------------------------------------------------------
# Guards
# ---------------------------------------------------------------------------
def project_version() -> str:
    match = re.search(r"project\(\s*GoaSynth\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)", 
                      (ROOT / "CMakeLists.txt").read_text(encoding="utf-8"))
    if not match:
        die("could not read the version from project() in CMakeLists.txt")
    return match.group(1)


def guard_tree_is_committed() -> str:
    """The package must be buildable again from a commit, so nothing may be dirty."""
    status = git("status", "--porcelain")
    dirty = [line for line in status.splitlines() if line[:2] != "??"]

    if dirty:
        print("make-release: these tracked files have uncommitted changes:")
        for line in dirty:
            print("   " + line)
        die("commit them first - a release is built from a commit, not from a working tree")

    return git("rev-parse", "--short", "HEAD")


def guard_build_is_current(version: str) -> None:
    """A stale binary is the failure this script exists to prevent."""
    for needed in (VST3_BINARY, LICENSE_HELPER):
        if not needed.exists():
            die(f"missing {needed.relative_to(ROOT)} - build it first:\n"
                "    cmake --build build --config Release --target "
                "GoaSynth_VST3 GoaSynthLicense --parallel")

    # Only the things the GoaSynth_VST3 target is compiled from. A test source
    # or a page under docs/ can change all day without touching the plugin, so
    # treating those as "the build is stale" would cry wolf until nobody
    # believed it. Source/ and CMakeLists.txt are what the binary is made of.
    built = VST3_BINARY.stat().st_mtime

    newest_source, newest_path = 0.0, None
    for path in [*(ROOT / "Source").rglob("*"), ROOT / "CMakeLists.txt"]:
        if path.is_file() and path.stat().st_mtime > newest_source:
            newest_source, newest_path = path.stat().st_mtime, path

    if newest_path is not None and newest_source > built:
        stale = datetime.fromtimestamp(newest_source).strftime("%Y-%m-%d %H:%M:%S")
        this = datetime.fromtimestamp(built).strftime("%Y-%m-%d %H:%M:%S")
        die(f"{newest_path.relative_to(ROOT)} was modified at {stale}, after the plugin was "
            f"linked at {this}.\n"
            "    The binary on disk is older than the source, so the ZIP would ship the wrong\n"
            "    code. Rebuild and try again:\n"
            "        cmake --build build --config Release --target GoaSynth_VST3 --clean-first --parallel")

    print(f"  version {version}, committed, and the binary is newer than every source file")


# ---------------------------------------------------------------------------
# Text the package carries
# ---------------------------------------------------------------------------
def readme_text(version: str, commit: str, built: str, zip_name: str,
                plugin_hash: str, plugin_size: int) -> str:
    return f"""GoaSynth {version} — Goa trance synthesizer (Windows, VST3)
================================================================

Built {built} from commit {commit} of {REPO_URL}
Website and manual: {SITE_URL}

What is in this package
-----------------------
  GoaSynth.vst3/          the plugin bundle - copy the whole folder
  GoaSynthLicense.exe     optional: opens .goalicense files on double-click
  README.txt              this file
  EULA.txt                the licence agreement for the software

Install (Windows)
-----------------
1. Copy the whole GoaSynth.vst3 folder into one of:

       C:\\Program Files\\Common Files\\VST3\\        (all DAWs; needs admin rights)
       %LOCALAPPDATA%\\Programs\\Common\\VST3\\      (just you, no admin)
       your DAW's own VST3 folder

   It is a folder, not a file: the folder *is* the plugin. Do not unpack the
   contents one level up.

2. In your DAW, rescan plugins (usually Settings -> Plug-ins -> Rescan), then
   add GoaSynth to an instrument track.

Unlicensed, GoaSynth runs fully for 24 hours from first launch - every feature,
no account, nothing to cancel. After that it asks for a licence.

Activating
----------
1. Open GoaSynth. The activation screen shows your MACHINE ID (20 characters,
   with a copy button). It is a fingerprint of this computer, not of you.
2. Send that machine ID with your order - the checkout asks for it, or reply to
   your receipt with it.
3. You get back a .goalicense file (a serial, wrapped). Double-click it, drag it
   onto the plugin window, or press IMPORT on the activation screen and pick it.
   Pasting the plain serial works too: a file and its serial are interchangeable.
4. Activation is offline. There is no server to reach and no dongle, and it
   keeps working without a network.

Your personal licence covers three machines you use yourself, and commercial
releases are included. 14-day refund. Free updates for the whole 1.x line.

Verify your download
--------------------
The plugin binary inside this package should hash to:

    {plugin_hash}    GoaSynth.vst3  ({thousands(plugin_size)} bytes)

The hash of the ZIP itself cannot live inside the ZIP, so it is published
next to the download at {SITE_URL}downloads/SHA256SUMS.txt along with the
plugin hash above. Check it before you install: a mismatch means the file was
damaged or replaced in transit. On Windows (PowerShell):

    Get-FileHash .\\{zip_name} -Algorithm SHA256

On macOS or Linux, in the folder you downloaded to:

    shasum -a 256 {zip_name}
    sha256sum -c SHA256SUMS.txt

Source code
-----------
GoaSynth's source is published under the AGPLv3 at {REPO_URL}. You are buying
the compiled build, the tested one, with support - not a locked-down binary.
The agreement that covers this build is in EULA.txt.

Support and refunds
-------------------
Use the support address in your purchase email (or the contact link on the
site) with your order number. A machine move or a reinstall changes the machine
ID and needs a replacement serial - we issue those free, so ask rather than
give up on it.

VST is a trademark of Steinberg Media Technologies GmbH, registered in Europe
and other countries. GoaSynth is an independent project, not affiliated with or
endorsed by Steinberg.
"""


def html_to_text(markup: str) -> str:
    """Good enough for a legal page of headings, paragraphs and lists."""
    markup = re.sub(r"(?s)<!--.*?-->", "", markup)
    markup = re.sub(r"(?s)<(script|style|nav)\b.*?</\1>", "", markup)
    markup = re.sub(r"(?i)</(p|h1|h2|h3|h4|li|dd|dt|blockquote)>", "\n\n", markup)
    markup = re.sub(r"(?i)<br\s*/?>", "\n", markup)
    markup = re.sub(r"(?i)<li\b[^>]*>", "  - ", markup)
    markup = re.sub(r"(?s)<[^>]+>", "", markup)
    markup = html.unescape(markup)
    lines = [re.sub(r"[ \t]+", " ", line).strip() for line in markup.split("\n")]
    text = "\n".join(lines)
    text = re.sub(r"\n{3,}", "\n\n", text)
    return text.strip()


def eula_text(version: str) -> str:
    page = EULA_HTML.read_text(encoding="utf-8")
    title = re.search(r"<h1>(.*?)</h1>", page, re.S)
    article = re.search(r"(?s)<article class=\"legal\">(.*?)</article>", page)
    if not article:
        die("could not find <article class=\"legal\"> in docs/legal/eula.html")

    body = html_to_text(article.group(1))
    heading = f"{html_to_text(title.group(1)) if title else 'Licence Agreement (EULA)'}\n"
    heading += "=" * max(len(heading) - 1, 1) + "\n\n"

    return (
        heading
        + f"GoaSynth {version} - the agreement that covers this build.\n"
          f"Published at {SITE_URL}legal/eula.html - this text is generated from that page.\n\n"
        + body
        + f"\n\nSource of the headings and contents: {SITE_URL}legal/eula.html\n"
    )


# ---------------------------------------------------------------------------
# The package
# ---------------------------------------------------------------------------
def add_file(archive: zipfile.ZipFile, path: Path, name: str, stamp: tuple) -> None:
    info = zipfile.ZipInfo(name, date_time=stamp)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o644 << 16
    archive.writestr(info, path.read_bytes(), compress_type=zipfile.ZIP_DEFLATED,
                     compresslevel=9)


def add_directory(archive: zipfile.ZipFile, name: str, stamp: tuple) -> None:
    info = zipfile.ZipInfo(name if name.endswith("/") else name + "/", date_time=stamp)
    info.compress_type = zipfile.ZIP_STORED
    info.external_attr = (0o40755 << 16) | 0x10   # directory
    archive.writestr(info, b"")


def add_text(archive: zipfile.ZipFile, text: str, name: str, stamp: tuple) -> None:
    info = zipfile.ZipInfo(name, date_time=stamp)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o644 << 16
    archive.writestr(info, text.encode("utf-8"), compress_type=zipfile.ZIP_DEFLATED,
                     compresslevel=9)


def build_zip(zip_path: Path, version: str, commit: str, built: str) -> tuple[int, str, str, int]:
    """Returns (zip bytes, zip sha, plugin sha, plugin size)."""
    stamp = datetime.fromtimestamp(VST3_BINARY.stat().st_mtime).timetuple()[:6]
    plugin_sha = sha256_file(VST3_BINARY)
    plugin_size = VST3_BINARY.stat().st_size

    members = sorted(
        (p for p in VST3_BUNDLE.rglob("*") if p.is_file()),
        key=lambda p: p.relative_to(VST3_BUNDLE.parent).as_posix(),
    )

    readme = readme_text(version, commit, built, zip_path.name, plugin_sha, plugin_size)
    eula = eula_text(version)

    # Explicit directory entries, because the bundle *is* a folder and some
    # extractors (and every double-click) are happier when the archive says so.
    directories = {VST3_BUNDLE.name}
    for member in members:
        parent = member.relative_to(VST3_BUNDLE.parent).parent.as_posix()
        while parent not in ("", "."):
            directories.add(parent)
            parent = parent.rsplit("/", 1)[0] if "/" in parent else ""

    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for directory in sorted(directories):
            add_directory(archive, directory, stamp)

        add_file(archive, LICENSE_HELPER, LICENSE_HELPER.name, stamp)

        for member in members:
            name = member.relative_to(VST3_BUNDLE.parent).as_posix()
            add_file(archive, member, name, stamp)

        add_text(archive, readme, "README.txt", stamp)
        add_text(archive, eula, "EULA.txt", stamp)

    data = zip_path.read_bytes()
    return len(data), hashlib.sha256(data).hexdigest(), plugin_sha, plugin_size


def write_manifests(zip_name: str, zip_size: int, zip_sha: str,
                    plugin_sha: str, plugin_size: int,
                    version: str, commit: str, built: str) -> None:
    published = f"""# GoaSynth {version} - checksums for the published download
#
# Built {built}, from commit {commit}.
# Site and install notes: {SITE_URL}
#
# Check the ZIP before you install it. Windows (PowerShell):
#
#     Get-FileHash .\\{zip_name} -Algorithm SHA256
#
# macOS or Linux, in the folder you downloaded to:
#
#     shasum -a 256 {zip_name}
#     sha256sum -c SHA256SUMS.txt
#
# The hash below must match the file you have. If it does not, the download was
# damaged or replaced on the way to you - do not install it, and tell support.
#
{zip_sha}  {zip_name}
#
# And the plugin inside the ZIP, once it is unpacked, should hash to:
#
#     {plugin_sha}  GoaSynth.vst3  ({thousands(plugin_size)} bytes)
#
# The ZIP is {pretty_size(zip_size)}. Its contents are listed in the manifest
# next to it on the download page.
"""
    PUBLISHED_SUMS.parent.mkdir(parents=True, exist_ok=True)
    PUBLISHED_SUMS.write_text(published, encoding="utf-8", newline="\n")
    (DIST / "SHA256SUMS.txt").write_text(published, encoding="utf-8", newline="\n")


def write_manifest(zip_name: str, zip_size: int, zip_sha: str,
                   version: str, commit: str, built: str) -> None:
    with zipfile.ZipFile(DIST / zip_name) as archive:
        rows = []
        for info in sorted(archive.infolist(), key=lambda i: i.filename):
            if info.is_dir():
                continue
            payload = archive.read(info)
            rows.append((info.filename, len(payload), hashlib.sha256(payload).hexdigest()))

    width = max(len(name) for name, _, _ in rows)
    listing = "\n".join(
        f"  {name.ljust(width)}  {thousands(size).rjust(12)} bytes  {digest}"
        for name, size, digest in rows
    )

    (DIST / "MANIFEST.txt").write_text(
        f"GoaSynth {version} - package manifest\n"
        f"{'=' * (len('GoaSynth ' + version + ' - package manifest'))}\n\n"
        f"Built     {built}\n"
        f"Commit    {commit}\n"
        f"Package   {zip_name}, {pretty_size(zip_size)}\n"
        f"ZipSHA256 {zip_sha}\n\n"
        f"Members, in archive order:\n\n{listing}\n\n"
        f"Reproduce this package from the commit above with:\n\n"
        f"    cmake --build build --config Release --target GoaSynth_VST3 --clean-first --parallel\n"
        f"    python tools/make-release.py\n",
        encoding="utf-8",
        newline="\n",
    )


def refresh_site_block(zip_name: str, zip_size: int, zip_sha: str,
                       plugin_sha: str, plugin_size: int,
                       version: str, commit: str, built: str) -> bool:
    """The site quotes the hashes, so the release writes them there itself.

    A published checksum that has to be edited by hand is a checksum that goes
    stale on the next release; this block is generated instead.
    """
    begin = "<!-- release:begin — generated by tools/make-release.py; edit that, not this -->"
    end = "<!-- release:end -->"
    page = INDEX_HTML.read_text(encoding="utf-8")

    block = "\n".join([
        "        " + begin,
        '        <dl class="kv">',
        f'          <dt>Version</dt><dd>{version}</dd>',
        f'          <dt>Built</dt><dd>{built}, from commit <code>{commit}</code></dd>',
        f'          <dt>Download</dt><dd><code>{zip_name}</code> — {pretty_size(zip_size)}</dd>',
        f'          <dt>ZIP SHA-256</dt><dd><code>{zip_sha}</code></dd>',
        f'          <dt>Plugin SHA-256</dt><dd><code>{plugin_sha}</code> — the unpacked '
        f'<code>GoaSynth.vst3</code> binary, {thousands(plugin_size)} bytes</dd>',
        "        </dl>",
        "        " + end,
    ])

    pattern = re.compile(r"(?s)<!-- release:begin.*?<!-- release:end -->")
    if not pattern.search(page):
        die("docs/index.html has no release:begin/release:end block to write the hashes into")

    updated = pattern.sub(lambda _: block, page, count=1)
    changed = updated != page
    INDEX_HTML.write_text(updated, encoding="utf-8", newline="\n")
    return changed


# ---------------------------------------------------------------------------
def main() -> int:
    print("make-release: cutting the Windows package\n")

    version = project_version()
    commit = guard_tree_is_committed()
    guard_build_is_current(version)

    built = datetime.fromtimestamp(VST3_BINARY.stat().st_mtime)
    built_text = built.strftime("%d %B %Y at %H:%M")
    zip_name = f"GoaSynth-{version}-win64.zip"
    DIST.mkdir(exist_ok=True)

    zip_size, zip_sha, plugin_sha, plugin_size = build_zip(
        DIST / zip_name, version, commit, built_text)

    write_manifests(zip_name, zip_size, zip_sha, plugin_sha, plugin_size,
                    version, commit, built_text)
    write_manifest(zip_name, zip_size, zip_sha, version, commit, built_text)
    rewritten = refresh_site_block(zip_name, zip_size, zip_sha, plugin_sha, plugin_size,
                                  version, commit, built_text)

    print(f"  dist/{zip_name}            {pretty_size(zip_size)}")
    print(f"      sha256 {zip_sha}")
    print(f"  plugin binary              {thousands(plugin_size)} bytes")
    print(f"      sha256 {plugin_sha}")
    print(f"  docs/downloads/SHA256SUMS.txt   published")
    print(f"  dist/MANIFEST.txt               {zip_name} + every member")
    print(f"  docs/index.html                 verify block "
          f"{'regenerated' if rewritten else 'already current'}")
    print("\nNot committed and not uploaded: the ZIP belongs on your store or "
          "customer-library\nhosting, next to the checksum file, and never in a "
          "public repository.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
