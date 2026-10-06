"""Run with python -m unittest discover -s tests -p test_gacidbase_feed.py."""
import contextlib
import copy
import importlib.util
import io
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import urllib.error

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("gacidbase_feed", ROOT / "tools/make-gacidbase-feed.py")
feed = importlib.util.module_from_spec(spec)
spec.loader.exec_module(feed)


class FeedTests(unittest.TestCase):
    def setUp(self):
        self.changelog = "## [1.1.0] - 2026-10-05\n### Fixed\n- A release note.\n"
        self.setup_asset = {"name": feed.installer_asset("1.1.0"), "size": 15421440}
        self.sums_asset = {"name": feed.SUMS_NAME, "size": 1215}
        self.release = {
            "tag_name": "v1.1.0", "html_url": feed.REPOSITORY_URL + "/releases/tag/v1.1.0",
            "draft": False, "prerelease": False, "published_at": "2026-10-05T03:28:43Z",
            "assets": [{"name": feed.ASSET_NAME, "size": 5798127}, self.setup_asset,
                       self.sums_asset],
        }

    def test_checked_in_feed_exactly_matches_sources(self):
        changelog = (ROOT / "tools/gacidbase/CHANGELOG.md").read_text(encoding="utf-8")
        release = json.loads((ROOT / "tools/gacidbase/release.json").read_text(encoding="utf-8"))
        wanted = feed.render(feed.make_feed(changelog, release, release["tag_name"]))
        self.assertEqual((ROOT / "docs/gacidbase/version.json").read_bytes(), wanted.encode("utf-8"))
        result = json.loads(wanted)
        self.assertEqual(set(result), {"latest", "url", "notes", "released", "installer_size"})
        self.assertEqual(result["latest"], release["tag_name"][1:])
        self.assertEqual(result["url"], feed.PRODUCT_URL)
        self.assertTrue(result["notes"])

    def test_site_has_one_anchor_per_released_version_and_no_future_notes(self):
        changelog = "## [Unreleased]\n### Added\n- Secret future note.\n" + self.changelog
        changelog += "\n## [1.0.0] - 2026-09-30\n### Added\n- Older <script> & note.\n"
        result = feed.render_notes(changelog)
        self.assertEqual(result.count('id="v1.1.0"'), 1)
        self.assertEqual(result.count('id="v1.0.0"'), 1)
        self.assertLess(result.index('id="v1.1.0"'), result.index('id="v1.0.0"'))
        self.assertIn('datetime="2026-09-30"', result)
        self.assertIn('href="#v1.0.0"', result)
        self.assertIn('Older &lt;script&gt; &amp; note.', result)
        self.assertNotIn('Secret future', result)
        self.assertNotIn('<script>', result)

    def test_site_block_preserves_surrounding_page_and_rejects_broken_markers(self):
        page = ('before' + feed.NOTES_BEGIN + '\nold\n' + feed.NOTES_END + 'middle'
                + feed.DOWNLOAD_BEGIN + '\nold row\n' + feed.DOWNLOAD_END + 'after')
        updated = feed.update_page(page, self.changelog, self.release)
        self.assertTrue(updated.startswith('before' + feed.NOTES_BEGIN))
        self.assertTrue(updated.endswith(feed.DOWNLOAD_END + 'after'))
        self.assertIn('middle', updated)
        self.assertNotIn('old row', updated)
        self.assertEqual(feed.update_page(updated, self.changelog, self.release), updated)
        for invalid in ('no markers', page + feed.NOTES_BEGIN, feed.NOTES_END + feed.NOTES_BEGIN,
                        page.replace(feed.DOWNLOAD_END, ''), page.replace(feed.DOWNLOAD_BEGIN, '')):
            with self.subTest(page=invalid), self.assertRaises(ValueError):
                feed.update_page(invalid, self.changelog, self.release)

    def test_download_row_links_only_the_assets_the_release_carries(self):
        row = feed.download_row("1.1.0", self.release)
        self.assertIn('<span class="dl-label">Download 1.1.0</span>', row)
        for label in ("ZIP package", "Installer", "SHA-256 checksums", "all files"):
            self.assertIn(f'>{label}</a>', row)
        self.assertEqual(row.count('<a '), 4)
        self.assertLess(row.index('ZIP package'), row.index('Installer'))
        self.assertLess(row.index('Installer'), row.index('SHA-256 checksums'))
        # An older release carries only the ZIP: its row links what it has, and
        # never a file nobody uploaded.
        zip_only = {"tag_name": "v1.1.0", "assets": [{"name": feed.ASSET_NAME, "size": 5798127}]}
        bare = feed.download_row("1.1.0", zip_only)
        self.assertIn('>ZIP package</a>', bare)
        self.assertIn('>all files</a>', bare)
        self.assertNotIn('Installer', bare)
        self.assertNotIn(feed.SUMS_NAME, bare)

    def test_the_row_rides_under_the_newest_release_only(self):
        changelog = self.changelog + "\n## [1.0.0] - 2026-09-30\n### Added\n- Older feature.\n"
        result = feed.render_notes(changelog, self.release)
        self.assertEqual(result.count('class="release-downloads"'), 1)
        self.assertLess(result.index('id="v1.1.0"'), result.index('class="release-downloads"'))
        self.assertLess(result.index('class="release-downloads"'), result.index('id="v1.0.0"'))
        # No release metadata: pure history, no row to get wrong.
        self.assertNotIn('release-downloads', feed.render_notes(changelog))

    def test_fixture_wire_values(self):
        result = feed.make_feed(self.changelog, self.release)
        self.assertEqual(result["latest"], "1.1.0")
        self.assertEqual(result["released"], "5 October 2026")
        self.assertEqual(result["installer_size"], "14.7 MB")

    def test_unreleased_does_not_leak_and_multiline_notes_are_plain(self):
        changelog = """# Changelog
## [Unreleased]
### Added
- A future feature.
## [1.1.0] - 2026-10-05
### Fixed
- **RUN** plays `notes` with a [link](https://example.test).
  This line continues the note.
## [1.0.0] - 2026-09-30
### Added
- Older feature.
"""
        result = feed.make_feed(changelog, self.release)
        self.assertEqual(result["notes"], ["RUN plays notes with a link. This line continues the note."])
        self.assertNotIn("future", json.dumps(result))
        self.assertNotIn("Older", json.dumps(result))

    def test_versions_dates_notes_and_order_are_strict(self):
        invalid = [
            "## [1.1.0]\n### Added\n- Note",
            "## [1.1.0] - 2026-02-30\n### Added\n- Note",
            "## [1.1.0-beta] - 2026-10-05\n### Added\n- Note",
            "## [01.1.0] - 2026-10-05\n### Added\n- Note",
            "## [1.1.0] - 2026-10-05\n- No category",
            "## [1.1.0] - 2026-10-05\n### Added\n- ** **",
            "## [1.1.0] - 2026-10-05\n### Unsupported\n- Note",
            "## [1.1.0] - 2026-10-05\n### Added\nNo bullet",
            "## [1.1.0] - 2026-10-05\n### Added\n",
            "## [Unreleased]\n### Added\n- Future only",
            "## [1.1.0] - 2026-10-05\n### Added\n- Note\n## [Unreleased]",
        ]
        for text in invalid:
            with self.subTest(text=text), self.assertRaises(ValueError):
                feed.make_feed(text, self.release)
        base = "## [1.1.0] - 2026-10-05\n### Added\n- Note\n"
        for extra in (base, base.replace("1.1.0", "1.2.0")):
            with self.subTest(extra=extra), self.assertRaises(ValueError):
                feed.make_feed(base + extra, self.release)

    def test_newest_changelog_and_expected_tag_must_match_release(self):
        with self.assertRaisesRegex(ValueError, "does not match release tag"):
            feed.make_feed(self.changelog.replace("[1.1.0]", "[1.2.0]"), self.release)
        with self.assertRaisesRegex(ValueError, "expected v1.2.0"):
            feed.make_feed(self.changelog, self.release, "v1.2.0")

    def test_invalid_release_metadata_is_rejected(self):
        changes = [
            ("tag_name", "v1.1.0-beta"), ("tag_name", "1.1.0"),
            ("draft", True), ("prerelease", True), ("draft", None),
            ("html_url", "https://github.com/another/project/releases/tag/v1.1.0"),
            ("published_at", None), ("published_at", "not-a-dateZ"),
            ("assets", []), ("assets", [None]), ("assets", {}),
            ("assets", [{"name": "wrong.zip", "size": 100}]),
            ("assets", [{"name": feed.ASSET_NAME, "size": 0}]),
            ("assets", [{"name": feed.ASSET_NAME, "size": True}]),
            ("assets", self.release["assets"] * 2),
            ("assets", self.release["assets"] + [dict(self.setup_asset)]),
            ("assets", [self.release["assets"][0],
                        {"name": feed.installer_asset("1.1.0"), "size": 0}]),
        ]
        for key, value in changes:
            release = copy.deepcopy(self.release)
            release[key] = value
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                feed.make_feed(self.changelog, release)

    def test_installer_size_is_the_installers_and_falls_back_to_the_zip(self):
        # The dialog offers the installer, so the size it quotes is the
        # installer's - not the package's, and never a number copied from the
        # feed that was there before.
        self.release["assets"][0]["size"] = 8 * 1024 * 1024
        self.assertEqual(feed.make_feed(self.changelog, self.release)["installer_size"], "14.7 MB")
        # A release from before the installer existed still validates: the
        # dialog quotes the package it does have instead of inventing a size.
        without_installer = copy.deepcopy(self.release)
        without_installer["assets"] = [{"name": feed.ASSET_NAME, "size": 8 * 1024 * 1024}]
        self.assertEqual(feed.make_feed(self.changelog, without_installer)["installer_size"], "8.0 MB")

    def test_refresh_fetch_is_bounded_and_saves_only_needed_public_fields(self):
        raw = copy.deepcopy(self.release)
        raw["body"] = "Ignored markdown"
        raw["assets"].append({"name": "other.zip", "size": 5})
        stream = io.BytesIO(json.dumps(raw).encode("utf-8"))
        with patch.object(feed.urllib.request, "urlopen", return_value=stream) as fetch:
            result = feed.fetch_release()
        request = fetch.call_args.args[0]
        self.assertEqual(request.full_url, feed.RELEASE_API)
        self.assertEqual(fetch.call_args.kwargs["timeout"], 20)
        self.assertEqual(result, self.release)


class CliTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.script = self.root / "tools/make-gacidbase-feed.py"
        self.script.parent.mkdir()
        shutil.copy2(ROOT / "tools/make-gacidbase-feed.py", self.script)
        for rel in ("tools/gacidbase/CHANGELOG.md", "tools/gacidbase/release.json", "docs/gacidbase/version.json", "docs/gacidbase/index.html"):
            target = self.root / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / rel, target)
        self.output = self.root / "docs/gacidbase/version.json"
        self.metadata = self.root / "tools/gacidbase/release.json"
        self.page = self.root / "docs/gacidbase/index.html"

    def run_cli(self, *args):
        return subprocess.run([sys.executable, str(self.script), *args], cwd=self.root,
                              capture_output=True, text=True, timeout=10)

    def test_check_is_read_only_and_regeneration_is_idempotent(self):
        original = self.output.read_bytes()
        stamp = self.output.stat().st_mtime_ns
        self.assertEqual(self.run_cli("--check").returncode, 0)
        self.assertEqual(self.run_cli().returncode, 0)
        self.assertEqual(self.output.read_bytes(), original)
        self.assertEqual(self.output.stat().st_mtime_ns, stamp)

    def test_any_hand_edited_field_or_format_fails_check_and_repairs_exactly(self):
        original = self.output.read_bytes()
        for key, value in (("latest", "9.9.9"), ("url", "https://wrong.test/"),
                           ("notes", ["Hand edit"]), ("released", "wrong"),
                           ("installer_size", "999 MB"), ("unexpected", True)):
            edited = json.loads(original)
            edited[key] = value
            self.output.write_text(json.dumps(edited), encoding="utf-8")
            before = self.output.read_bytes()
            with self.subTest(key=key):
                result = self.run_cli("--check")
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertIn("feed is stale", result.stderr)
                self.assertEqual(self.output.read_bytes(), before)
                self.assertEqual(self.run_cli().returncode, 0)
                self.assertEqual(self.output.read_bytes(), original)
        self.output.write_bytes(original.replace(b"\n", b"\r\n"))
        self.assertEqual(self.run_cli("--check").returncode, 1)
        self.assertEqual(self.run_cli().returncode, 0)
        self.assertEqual(self.output.read_bytes(), original)

    def test_site_note_drift_is_rejected_and_regenerated_without_touching_other_content(self):
        original = self.page.read_bytes()
        self.page.write_bytes(original.replace(b'id="v1.1.0"', b'id="wrong-version"'))
        changed = self.page.read_bytes()
        result = self.run_cli('--check')
        self.assertEqual(result.returncode, 1)
        self.assertIn('site release notes is stale', result.stderr)
        self.assertEqual(self.page.read_bytes(), changed)
        self.assertEqual(self.run_cli().returncode, 0)
        self.assertEqual(self.page.read_bytes(), original)

    def test_missing_site_markers_fail_before_writing_feed_or_release(self):
        original = self.output.read_bytes()
        metadata = self.metadata.read_bytes()
        self.page.write_text('broken page', encoding='utf-8')
        result = self.run_cli()
        self.assertEqual(result.returncode, 1)
        self.assertIn('marker pair', result.stderr)
        self.assertEqual(self.output.read_bytes(), original)
        self.assertEqual(self.metadata.read_bytes(), metadata)

    def test_missing_feed_is_detected_and_generated(self):
        self.output.unlink()
        self.assertEqual(self.run_cli("--check").returncode, 1)
        self.assertFalse(self.output.exists())
        self.assertEqual(self.run_cli().returncode, 0)
        self.assertEqual(self.run_cli("--check").returncode, 0)

    def test_invalid_inputs_do_not_overwrite_output(self):
        original = self.output.read_bytes()
        self.metadata.write_text('{"tag_name": "v99.0.0"}', encoding="utf-8")
        self.assertEqual(self.run_cli().returncode, 1)
        self.assertEqual(self.output.read_bytes(), original)

    def test_conflicting_modes_and_invalid_expected_tag_fail(self):
        for args in (("--check", "--refresh"), ("--tag", "1.1.0"), ("--tag", "v1.1.0-beta")):
            with self.subTest(args=args):
                self.assertEqual(self.run_cli(*args).returncode, 2)
        self.assertEqual(self.run_cli("--check", "--tag", "v9.0.0").returncode, 1)

    def test_refresh_failure_keeps_both_output_files_unchanged(self):
        original = self.output.read_bytes()
        metadata = self.metadata.read_bytes()
        release = json.loads(metadata)
        errors = [urllib.error.URLError("offline")]
        bad = copy.deepcopy(release)
        bad["tag_name"] = "v9.0.0"
        bad["html_url"] = feed.REPOSITORY_URL + "/releases/tag/v9.0.0"
        for error in errors + [bad]:
            fetch = patch.object(feed, "fetch_release", side_effect=error) if isinstance(error, Exception) else patch.object(feed, "fetch_release", return_value=error)
            with patch.object(feed, "CHANGELOG", self.root / "tools/gacidbase/CHANGELOG.md"), \
                 patch.object(feed, "RELEASE", self.metadata), patch.object(feed, "FEED", self.output), patch.object(feed, "PAGE", self.page), \
                 patch.object(sys, "argv", ["make-gacidbase-feed.py", "--refresh"]), \
                 fetch, contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(feed.main(), 1)
            self.assertEqual(self.output.read_bytes(), original)
            self.assertEqual(self.metadata.read_bytes(), metadata)

    def test_offline_check_never_fetches(self):
        with patch.object(feed, "CHANGELOG", self.root / "tools/gacidbase/CHANGELOG.md"), \
             patch.object(feed, "RELEASE", self.metadata), patch.object(feed, "FEED", self.output), patch.object(feed, "PAGE", self.page), \
             patch.object(sys, "argv", ["make-gacidbase-feed.py", "--check"]), \
             patch.object(feed, "fetch_release", side_effect=AssertionError("network forbidden")), \
             contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(feed.main(), 0)


if __name__ == "__main__":
    unittest.main()
