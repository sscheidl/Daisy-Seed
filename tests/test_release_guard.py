"""Release preflight against a real temporary Git repo and a fake paginated API."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'scripts/check-release-available.sh'


class ReleaseGuardTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        subprocess.run(['git', 'init', '-q', str(self.root)], check=True)
        subprocess.run(['git', '-c', 'user.name=Test', '-c', 'user.email=test@example.invalid',
                        'commit', '--allow-empty', '-qm', 'test'], cwd=self.root, check=True)
        self.pages = self.root / 'pages.json'
        self.pages.write_text('[[]]')
        self.log = self.root / 'api-call.json'
        fake = self.root / 'gh'
        fake.write_text(f'#!{sys.executable}\n' + '''import json, os, sys
from pathlib import Path
Path(os.environ['API_LOG']).write_text(json.dumps(sys.argv[1:]))
if os.environ.get('API_FAIL') == '1':
    sys.exit(42)
pages = json.loads(Path(os.environ['API_PAGES']).read_text())
if '--paginate' not in sys.argv:
    pages = pages[:1]
for page in pages:
    for release in page:
        print(release['tag_name'])
''')
        fake.chmod(0o755)
        self.env = dict(os.environ, GITHUB_REPOSITORY='test/repo',
                        API_LOG=str(self.log), API_PAGES=str(self.pages))
        self.env['PATH'] = str(self.root) + os.pathsep + self.env['PATH']

    def run_guard(self, pages=None, version='0.2'):
        if pages is not None:
            self.pages.write_text(json.dumps(pages))
        return subprocess.run(['bash', str(SCRIPT), version], cwd=self.root,
                              env=self.env, text=True, capture_output=True)

    def test_available_version(self):
        self.assertEqual(self.run_guard().returncode, 0)
        self.assertEqual(json.loads(self.log.read_text()), [
            'api', '--paginate', 'repos/test/repo/releases?per_page=100',
            '--jq', '.[].tag_name'])

    def test_draft_without_tag_is_rejected(self):
        result = self.run_guard([[{'tag_name': 'v0.2', 'draft': True}]])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('published or draft', result.stderr)

    def test_published_release_without_local_tag_is_rejected(self):
        self.assertNotEqual(self.run_guard([[{'tag_name': 'v0.2', 'draft': False}]]).returncode, 0)

    def test_draft_on_later_page_is_rejected(self):
        result = self.run_guard([[{'tag_name': 'v0.1', 'draft': False}],
                                 [{'tag_name': 'v0.2', 'draft': True}]])
        self.assertNotEqual(result.returncode, 0)

    def test_similar_version_does_not_match(self):
        self.assertEqual(self.run_guard([[{'tag_name': 'v0.20', 'draft': True}]]).returncode, 0)

    def test_existing_tag_stops_before_api(self):
        subprocess.run(['git', 'tag', 'v0.2'], cwd=self.root, check=True)
        self.assertNotEqual(self.run_guard().returncode, 0)
        self.assertFalse(self.log.exists())

    def test_api_error_fails_closed(self):
        self.env['API_FAIL'] = '1'
        self.assertEqual(self.run_guard().returncode, 42)

    def test_invalid_version_is_rejected(self):
        self.assertNotEqual(self.run_guard(version='0.2\nother').returncode, 0)
        self.assertFalse(self.log.exists())


if __name__ == '__main__':
    unittest.main()
