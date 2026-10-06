"""Deployment-check regressions; mocked HTTP never accesses the network."""
import contextlib
import importlib.util
import io
import mimetypes
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("check_deployed", ROOT / "tools/check-deployed.py")
deployed = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = deployed
spec.loader.exec_module(deployed)


class DeploymentTests(unittest.TestCase):
    def setUp(self):
        self.site = deployed.load_site(None)

    def response(self, url, timeout, follow=True, cache_bust=True):
        base = self.site.base
        if url == base.rstrip('/'):
            return deployed.Reply(301, url, base, "text/html", b"")
        rel = url[len(base):]
        if not rel or rel.endswith('/'):
            rel += 'index.html'
        path = self.site.files.get(rel)
        if path is None:
            return deployed.Reply(404, url, "", "text/html", b"missing")
        kind = mimetypes.guess_type(str(path))[0] or "application/octet-stream"
        if path.suffix == '.js':
            kind = "application/javascript"
        return deployed.Reply(200, url, "", kind, path.read_bytes())

    def scan(self, response):
        with patch.object(deployed, 'get', side_effect=response), contextlib.redirect_stdout(io.StringIO()):
            return deployed.check(self.site, 1, False)

    def test_product_directory_urls_resolve_to_their_index_files(self):
        self.assertEqual(self.scan(self.response), [])

    def test_missing_product_directory_still_fails(self):
        target = self.site.base + 'goasynth/'
        def missing(url, *args, **kwargs):
            if url == target:
                return deployed.Reply(404, url, "", "text/html", b"missing")
            return self.response(url, *args, **kwargs)
        problems = self.scan(missing)
        self.assertTrue(any('sitemap.xml' in p and target in p and '404' in p for p in problems), problems)

    def test_directory_serving_the_wrong_page_still_fails(self):
        target = self.site.base + 'gacidbase/'
        def wrong(url, *args, **kwargs):
            reply = self.response(url, *args, **kwargs)
            if url == target:
                reply.body = b'wrong product page'
            return reply
        problems = self.scan(wrong)
        self.assertTrue(any('sitemap.xml' in p and target in p and 'different bytes' in p for p in problems), problems)


if __name__ == '__main__':
    unittest.main()
