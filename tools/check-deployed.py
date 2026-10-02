#!/usr/bin/env python3
"""Compare the deployed site with this repository, file by file.

    python tools/check-deployed.py              # is the live site this commit?
    python tools/check-deployed.py --wait 60    # ...right after a push, let it publish
    python tools/check-deployed.py -v           # list every file compared
    python tools/check-deployed.py --base URL   # compare against another host

The whole promotion path here is "push to main", after which GitHub Pages
publishes docs/ on its own, somewhere in the next few seconds to a minute.
Nothing in the build can see that step, so everything above it is an assumption:
the commit is pushed, therefore the site is that commit. This checks the
assumption instead, by fetching the site and comparing it with the tree.

What is compared
----------------
  * every file under docs/, byte for byte, at the URL it is published at - so a
    deploy that has not finished, a page still being served from an older commit
    or a file that never made it into the deploy fails here rather than in
    somebody's browser. Both digests are printed on a mismatch, so it is clear
    which side is stale;
  * each page's <link rel="canonical"> and its og:url against the address the
    page was actually served at - the one thing a crawler reads that a wrong
    deploy can silently contradict;
  * the search-engine verification token, byte for byte, and its exact form:
    Google fetches that file and looks for its own name inside it, so a token
    that shifted by one byte un-verifies the property with no visible symptom;
  * robots.txt's Sitemap: line, and every <loc> in sitemap.xml: each has to be
    served, byte-identical to the file it names, and name something the
    repository actually publishes;
  * the site's one-address property: the address without its trailing slash has
    to redirect to the canonical one, and the index.html spelling has to serve
    the same bytes rather than a second copy of the landing page.

Why every request is cache-busted
---------------------------------
GitHub Pages serves with `Cache-Control: max-age=600`, so a plain request can
return the *previous* deploy for ten minutes after a push - which would have
this report drift that is really just the CDN doing its job. Each request
carries a throwaway query parameter, so the deployment is seen as it is now.
Addresses are always compared without that parameter.

Exit codes
----------
0 the deployed site is this tree   1 it is not (drift, or a file missing)
2 could not be checked (no canonical to derive the site from, docs/ has
uncommitted edits, or the host did not answer at all)

Needs the network; nothing else here does. Plain Python 3, no third-party
modules, nothing to install.
"""
from __future__ import annotations

import argparse
import hashlib
import os
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DOCS = ROOT / "docs"

EXIT_OK, EXIT_DRIFT, EXIT_SETUP = 0, 1, 2

CANONICAL_RE = re.compile(r'<link\s+rel="canonical"\s+href="([^"]+)"')
OG_URL_RE = re.compile(r'<meta\s+property="og:url"\s+content="([^"]*)"')
LOC_RE = re.compile(r"<loc>([^<]+)</loc>")
SITEMAP_LINE_RE = re.compile(r"^\s*Sitemap:\s*(\S+)\s*$", re.M)

# Google's HTML-file verification method, and the shape that file has to have.
TOKEN_FILE_RE = re.compile(r"^google[a-z0-9]+\.html$", re.I)
TOKEN_BODY_RE = re.compile(r"^google-site-verification: (google[a-z0-9]+\.html)$", re.I)

# What a host should say a file is. Only the major class is checked, and only
# for the extensions where the wrong one actually breaks something.
CONTENT_TYPES = {
    ".html": "text/html",
    ".css": "text/css",
    ".js": "javascript",
    ".png": "image/",
    ".ico": "image/",
    ".txt": "text/plain",
    ".xml": "xml",
}

USER_AGENT = "GoaSynth-check-deployed/1.0 (+https://github.com/Y4m4/GoaSynth)"


class StopAtRedirect(urllib.request.HTTPRedirectHandler):
    """Refuse to follow a redirect, because the redirect is what is being checked.

    urllib follows them silently by default, so the only way to see that
    /GoaSynth answers 301 /GoaSynth/ - rather than serving a second copy of the
    landing page - is to decline and read the response.
    """

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


OPENER = urllib.request.build_opener(StopAtRedirect)


def die(message: str, code: int = EXIT_SETUP) -> int:
    print(f"\ncheck-deployed: {message}", file=sys.stderr)
    return code


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def read(path: Path) -> bytes:
    return path.read_bytes()


#==============================================================================
# what the repository publishes, and at which address
#==============================================================================
@dataclass
class Site:
    declared: str = ""                                     # the address the pages declare
    base: str = ""                                         # the address under test
    files: dict[str, Path] = field(default_factory=dict)   # docs-relative -> file on disk
    pages: dict[str, str] = field(default_factory=dict)    # page -> declared canonical URL
    token: str | None = None                               # the verification file, if any
    order: list[str] = field(default_factory=list)

    @property
    def addresses_match(self) -> bool:
        """Whether the host under test is the address the pages say they live at.

        When it is not - somebody checking a staging copy, or a custom domain
        before the pages have been rewritten to it - the address comparisons are
        not wrong, they are meaningless, and reporting them would bury the file
        comparisons that are still worth having.
        """
        return self.base == self.declared


def load_site(base_override: str | None) -> Site:
    """The site as the repository declares it: one address, from the pages.

    The canonical links are the only place the address is written down, which is
    what lets this check follow a move to a custom domain without being told -
    and the same reason the generators read them.
    """
    if not DOCS.is_dir():
        raise SystemExit(die(f"{DOCS} does not exist - run this from the repository"))

    site = Site()

    for path in sorted(p for p in DOCS.rglob("*") if p.is_file()):
        rel = path.relative_to(DOCS).as_posix()
        site.files[rel] = path
        site.order.append(rel)

    for rel in site.order:
        if TOKEN_FILE_RE.match(rel):
            if "/" in rel:
                raise SystemExit(die(f"docs/{rel}: a verification token has to sit at the site "
                                     f"root, or the console will not find it"))
            if site.token is not None:
                raise SystemExit(die(f"docs/ contains two verification tokens ({site.token} and "
                                     f"{rel}) - which one is live is a question, not a check"))
            site.token = rel
            continue

        if not rel.endswith(".html"):
            continue

        page = read(site.files[rel]).decode("utf-8")
        found = CANONICAL_RE.findall(page)

        if len(found) != 1:
            raise SystemExit(die(f"docs/{rel}: {len(found)} canonical link(s); every page needs "
                                 f"exactly one, and it is what this check trusts"))
        if not found[0].startswith("https://"):
            raise SystemExit(die(f"docs/{rel}: its canonical is {found[0]!r}, which is not an "
                                 f"absolute https:// URL"))

        site.pages[rel] = found[0]

    if "index.html" not in site.pages:
        raise SystemExit(die("docs/index.html has no canonical link, so there is no site base to "
                             "check against"))

    base = site.pages["index.html"]

    if not base.endswith("/"):
        raise SystemExit(die(f"docs/index.html: its canonical is {base!r} - the landing page's "
                             f"address is the site base and has to end in a slash"))

    for rel, url in site.pages.items():
        if not url.startswith(base):
            raise SystemExit(die(f"docs/{rel}: its canonical {url} is not under the site base "
                                 f"{base} - one of the two is wrong"))

    site.declared = base
    site.base = (base_override.rstrip("/") + "/") if base_override else base

    return site


def url_of(site: Site, rel: str) -> str:
    """Where a docs-relative path is published."""
    return site.base + ("" if rel == "index.html" else rel)


#==============================================================================
# fetching
#==============================================================================
@dataclass
class Reply:
    status: int
    url: str             # where it ended up, without the cache-buster
    location: str        # a redirect's target, when one was refused
    content_type: str
    body: bytes
    error: str = ""


def get(url: str, timeout: float, follow: bool = True, cache_bust: bool = True) -> Reply:
    """GET one URL, reporting rather than raising."""
    request_url = f"{url}{'&' if '?' in url else '?'}cb={os.urandom(4).hex()}" if cache_bust else url
    request = urllib.request.Request(request_url, headers={"User-Agent": USER_AGENT})

    try:
        opener = urllib.request.urlopen if follow else OPENER.open
        with opener(request, timeout=timeout) as response:
            return Reply(status=response.status,
                         url=response.geturl().split("?")[0],
                         location=response.headers.get("Location", ""),
                         content_type=response.headers.get("Content-Type", ""),
                         body=response.read())
    except urllib.error.HTTPError as exc:
        return Reply(status=exc.code, url=url, location=exc.headers.get("Location", ""),
                     content_type=exc.headers.get("Content-Type", ""), body=exc.read(),
                     error=f"HTTP {exc.code}")
    except urllib.error.URLError as exc:
        return Reply(status=0, url=url, location="", content_type="", body=b"",
                     error=str(exc.reason))
    except TimeoutError:
        return Reply(status=0, url=url, location="", content_type="", body=b"", error="timed out")


#==============================================================================
# the checks
#==============================================================================
def check(site: Site, timeout: float, verbose: bool) -> list[str]:
    problems: list[str] = []
    memo: dict[tuple[str, bool], Reply] = {}

    def fetch(url: str, follow: bool = True) -> Reply:
        key = (url, follow)
        if key not in memo:
            memo[key] = get(url, timeout, follow=follow)
        return memo[key]

    def note(text: str) -> None:
        if verbose:
            print(f"  {text}")

    # ---- every published file, byte for byte -------------------------------
    served = 0

    for rel in site.order:
        want = read(site.files[rel])
        got = fetch(url_of(site, rel))

        if got.status != 200:
            problems.append(f"docs/{rel}: the site answers {got.error or got.status} at "
                            f"{url_of(site, rel)}, but the repository publishes it")
            continue

        if got.body != want:
            problems.append(f"docs/{rel}: the deployed bytes differ from the repository's\n"
                            f"            repo  {digest(want)}\n"
                            f"            live  {digest(got.body)}\n"
                            f"            ({len(want)} bytes in the repository, {len(got.body)} "
                            f"deployed)")
            continue

        # Bytes that are right and a type that is wrong is a broken deploy no
        # digest can see: the browser refuses the file either way.
        kind = CONTENT_TYPES.get(Path(rel).suffix.lower())

        if kind and kind not in got.content_type.lower():
            problems.append(f"docs/{rel}: served as {got.content_type or '(no content type)'}, "
                            f"but it is a {kind} file - the bytes are right and the browser will "
                            f"still refuse them")
            continue

        served += 1
        note(f"{len(want):>9} B  {rel}")

    # ---- the verification token -------------------------------------------
    token_line = "verification token: none under docs/"
    if site.token:
        live = fetch(url_of(site, site.token))
        body = live.body.decode("utf-8", errors="replace")
        claimed = TOKEN_BODY_RE.match(body)

        if live.status != 200:
            token_line = f"verification token: {live.error or live.status}"
            problems.append(f"docs/{site.token}: the site answers {live.error or live.status} - "
                            f"the token has to be reachable at the site root for verification")
        elif claimed is None:
            token_line = "verification token: not in the console's form"
            problems.append(f"docs/{site.token}: its content is {body!r}, but a verification file "
                            f"has to hold exactly \"google-site-verification: {site.token}\" - "
                            f"anything else reads as not verified, whatever the file is called")
        elif claimed.group(1).lower() != site.token.lower():
            token_line = f"verification token: names {claimed.group(1)}"
            problems.append(f"docs/{site.token}: its content names {claimed.group(1)}, a different "
                            f"file - the console looks for the name it issued")
        else:
            token_line = (f"verification token: {site.token}, {len(live.body)} bytes, "
                          f"byte-identical")

    # ---- canonical and og:url, as served ----------------------------------
    pages_checked = 0

    for rel in site.pages:
        url = url_of(site, rel)
        live = fetch(url)

        if live.status != 200:
            continue                      # the file pass has already reported this

        page = live.body.decode("utf-8", errors="replace")
        canonicals = CANONICAL_RE.findall(page)
        og_urls = OG_URL_RE.findall(page)
        pages_checked += 1

        if not site.addresses_match:
            note(f"canonical  docs/{rel} -> {canonicals[0] if canonicals else '(nothing)'} "
                 f"(not compared)")
            continue

        if canonicals != [url]:
            problems.append(f"docs/{rel}: served at {url} but its canonical link says "
                            f"{canonicals[0] if canonicals else '(nothing)'} - the address a page "
                            f"claims and the address it answers on have to be the same one")

        if og_urls and og_urls[0] != (canonicals[0] if canonicals else ""):
            problems.append(f"docs/{rel}: its og:url is {og_urls[0]} while its canonical link says "
                            f"{canonicals[0] if canonicals else '(nothing)'}")


    # ---- robots.txt, and the sitemap it names -----------------------------
    robots_line = "robots.txt: not served"
    robots = fetch(url_of(site, "robots.txt"))

    if robots.status == 200:
        found = SITEMAP_LINE_RE.search(robots.body.decode("utf-8", errors="replace"))

        if found is None:
            robots_line = "robots.txt: no Sitemap: line"
            problems.append("docs/robots.txt: no Sitemap: line, so nothing connects a crawler to "
                            "the page list")
        else:
            sitemap_url = found.group(1)
            robots_line = f"robots.txt: Sitemap: {sitemap_url}"

            # The Sitemap: line names the declared address, not the host under
            # test, so following it would be checking production from a check of
            # somebody else's copy.
            if site.addresses_match:
                if not sitemap_url.startswith(site.base):
                    problems.append(f"docs/robots.txt: points a crawler at {sitemap_url}, which "
                                    f"is not under the site base {site.base}")

                if fetch(sitemap_url).status != 200:
                    problems.append(f"docs/robots.txt: the Sitemap: line names {sitemap_url}, "
                                    f"which does not answer")

    # ---- every address the sitemap advertises -----------------------------
    sitemap_line = "sitemap.xml: not served"
    sitemap = fetch(url_of(site, "sitemap.xml"))

    if sitemap.status == 200:
        locs = LOC_RE.findall(sitemap.body.decode("utf-8", errors="replace"))
        sitemap_line = (f"sitemap.xml: {len(locs)} <loc>(s)"
                        + ("" if site.addresses_match else ", addresses not compared"))

        if not site.addresses_match:
            locs = []

        if not locs and site.addresses_match:
            problems.append("docs/sitemap.xml: no <loc> entries at all - nothing for a crawler to "
                            "follow")

        for loc in locs:
            if not loc.startswith(site.base):
                problems.append(f"docs/sitemap.xml: advertises {loc}, which is not under the site "
                                f"base {site.base}")
                continue

            rel = loc[len(site.base):] or "index.html"

            if rel not in site.files:
                problems.append(f"docs/sitemap.xml: advertises {loc}, which the repository does not "
                                f"publish - a crawler is being sent to a URL that serves nothing")
                continue

            declared = fetch(loc)

            if declared.status != 200:
                problems.append(f"docs/sitemap.xml: {loc} answers "
                                f"{declared.error or declared.status}")
            elif declared.body != read(site.files[rel]):
                problems.append(f"docs/sitemap.xml: {loc} serves different bytes than docs/{rel} "
                                f"in the repository")

    # ---- one address, not three -------------------------------------------
    address_line = "one address: not checked"
    if not site.addresses_match:
        address_line = (f"one address: not checked - the pages declare {site.declared}, this run "
                        f"is against {site.base}")
    elif "index.html" in site.pages:
        index = read(site.files["index.html"])
        bare = site.base.rstrip("/")
        live = fetch(bare, follow=False)

        # The redirect's Location echoes the cache-buster back, so the query is
        # stripped before the target is judged - the address is the address.
        target = live.location.split("?")[0] if live.location else ""

        if live.status in (301, 302, 307, 308):
            if target.rstrip("/") != bare:
                problems.append(f"{bare} redirects to {target or '(nowhere)'}, which is not "
                                f"the canonical address {site.base}")
        elif live.status == 200 and live.body != index:
            problems.append(f"{bare} serves a different page than {site.base} - two addresses "
                            f"serving two landing pages is a site competing with itself")
        elif live.status != 200:
            problems.append(f"{bare} answers {live.error or live.status}; the canonical address "
                            f"{site.base} has to be the one that works")

        alias = fetch(url_of(site, "index.html"))

        if alias.status == 200 and alias.body != index:
            problems.append(f"{url_of(site, 'index.html')} serves different bytes than the landing "
                            f"page - the canonical link is what keeps that from being indexed "
                            f"twice, and it cannot if the page differs")

        address_line = (f"one address: {bare} -> {live.status}"
                        + (f" {target}" if target else "")
                        + f", index.html alias -> {alias.status}")

    print(f"\n  {served}/{len(site.order)} file(s) served byte-identical")
    print(f"  {pages_checked}/{len(site.pages)} page(s) fetched: canonical and og:url "
          + ("against the address served" if site.addresses_match
             else "not compared: this host is not the address the pages declare"))
    print(f"  {token_line}")
    print(f"  {robots_line}")
    print(f"  {sitemap_line}")
    print(f"  {address_line}")

    return problems


#==============================================================================
def dirty_docs() -> str:
    """Uncommitted changes under docs/, or an empty string.

    Comparing the live site with a tree that has not been pushed asks a question
    with two right answers: the difference may be the deploy lagging, or an edit
    that was never committed. Refusing to guess is the point of the check.
    """
    try:
        result = subprocess.run(["git", "status", "--porcelain", "--", "docs"],
                               cwd=str(ROOT), capture_output=True, text=True)
    except FileNotFoundError:
        return ""

    return result.stdout.strip() if result.returncode == 0 else ""


def main() -> int:
    parser = argparse.ArgumentParser(
        prog="check-deployed.py",
        description="Compare the deployed site with this repository, file by file.")
    parser.add_argument("--base", default=None,
                        help="check this address instead of the one the pages declare")
    parser.add_argument("--wait", type=float, default=0.0,
                        help="if something differs, keep re-checking for this many seconds "
                             "(GitHub Pages takes a moment to publish a push)")
    parser.add_argument("--timeout", type=float, default=20.0,
                        help="seconds to allow one request (default: 20)")
    parser.add_argument("--allow-dirty", action="store_true",
                        help="check even when docs/ has uncommitted changes")
    parser.add_argument("-v", "--verbose", action="store_true",
                        help="list every file and page compared")
    args = parser.parse_args()

    if not args.allow_dirty:
        dirty = dirty_docs()
        if dirty:
            return die("docs/ has uncommitted changes:\n\n" + dirty +
                       "\n\n  The live site cannot have them yet, so any difference would be\n"
                       "  ambiguous - it could be a deploy lagging or an edit that was never\n"
                       "  pushed. Commit and push first, or pass --allow-dirty.")

    site = load_site(args.base)
    print(f"check-deployed: comparing {site.base} with docs/")

    if not site.addresses_match:
        print(f"                the pages declare {site.declared}; canonical, sitemap origin and\n"
              f"                the one-address checks are skipped because this host is not that"
              f"\n                address - the file comparison below is still exact")

    deadline = time.monotonic() + args.wait
    attempt = 0
    problems: list[str] = []

    while True:
        attempt += 1
        problems = check(site, args.timeout, args.verbose)

        if not problems or time.monotonic() >= deadline:
            break

        left = max(0.0, deadline - time.monotonic())
        print(f"\ncheck-deployed: {len(problems)} difference(s) so far - waiting for the deploy "
              f"({left:.0f}s left)")
        time.sleep(min(5.0, left + 0.1))

    if not problems:
        print(f"\ncheck-deployed: {site.base} is exactly this repository's docs/ tree")
        return EXIT_OK

    print(f"\ncheck-deployed: {len(problems)} difference(s) between {site.base} and docs/\n")

    for problem in problems:
        print(f"  {problem}")

    print("\n  A deploy that has not finished looks exactly like this. GitHub Pages takes roughly\n"
          "  10-40 seconds after a push, so if the push was recent, re-run with --wait 60 before\n"
          "  believing any of it.")

    if attempt > 1:
        print(f"\n  (checked {attempt} times over {args.wait:.0f}s)")

    return EXIT_DRIFT


if __name__ == "__main__":
    raise SystemExit(main())
