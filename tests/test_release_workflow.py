""".github/workflows/release.yml, read as text (no YAML library): pinned actions, the same Zig as ci.yml, a pinned
embeddable Python, no secrets but the run's own token, write access only where a release is made, and no expression
pasted into a script. Nothing runs on GitHub here."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RELEASE = ROOT / '.github' / 'workflows' / 'release.yml'
CI = ROOT / '.github' / 'workflows' / 'ci.yml'

OFFICIAL_ACTION = re.compile(r'actions/[A-Za-z0-9_.-]+@v\d+')                         # GitHub's own, by major tag
PINNED_ACTION = re.compile(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+(?:/[A-Za-z0-9_./-]+)?@[0-9a-f]{40}')   # anyone else's, by commit
JOB_START = re.compile(r'^  ([A-Za-z_][A-Za-z0-9_-]*):\s*$')
RUN_KEY = re.compile(r'^(\s*)(?:-\s+)?run:\s*(.*)$')


def env_values(text, key):
    return re.findall(rf'^\s+{key}:\s*(\S+)\s*$', text, re.MULTILINE)


def jobs(text):
    """{job id: its lines}: the blocks under the top-level 'jobs:' key."""
    lines = text.splitlines()
    start = lines.index('jobs:')
    found = {}
    current = None
    for line in lines[start + 1:]:
        if line and not line.startswith(' ') and not line.startswith('#'):
            break   # the next top-level key
        match = JOB_START.match(line)
        if match:
            current = match.group(1)
            found[current] = []
        elif current:
            found[current].append(line)
    return {name: '\n'.join(body) for name, body in found.items()}


def run_scripts(text):
    """The text of every run: value (one line, or the block under 'run: |')."""
    lines = text.splitlines()
    scripts = []
    for index, line in enumerate(lines):
        match = RUN_KEY.match(line)
        if not match:
            continue
        indent = len(match.group(1))
        value = match.group(2).strip()
        if value not in ('|', '>', '|-', '>-'):
            scripts.append(value)
            continue
        block = []
        for following in lines[index + 1:]:
            if following.strip() and len(following) - len(following.lstrip()) <= indent:
                break
            block.append(following)
        scripts.append('\n'.join(block))
    return scripts


class ReleaseWorkflow(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = RELEASE.read_text(encoding='utf-8')
        cls.ci = CI.read_text(encoding='utf-8')
        cls.jobs = jobs(cls.text)

    def test_triggers_and_jobs(self):
        self.assertRegex(self.text, r'(?m)^on:\n  workflow_dispatch:\n  push:\n    tags: \[\'v\*\'\]\n')
        self.assertEqual(sorted(self.jobs), ['kit', 'publish'])
        self.assertIn('runs-on: windows-2022', self.jobs['kit'])
        self.assertIn('needs: kit', self.jobs['publish'])

    def test_actions_are_official_major_tags_or_pinned_commits(self):
        uses = re.findall(r'^\s*(?:-\s+)?uses:\s*(\S+)', self.text, re.MULTILINE)
        self.assertTrue(uses)
        for action in uses:
            self.assertTrue(OFFICIAL_ACTION.fullmatch(action) or PINNED_ACTION.fullmatch(action), action)

    def test_the_zig_pin_is_ci_s(self):
        for key in ('ZIG_VERSION', 'ZIG_SHA256'):
            release_values = env_values(self.text, key)
            ci_values = env_values(self.ci, key)
            self.assertEqual(len(release_values), 1, key)
            self.assertEqual(release_values, ci_values, key)
        self.assertRegex(env_values(self.text, 'ZIG_SHA256')[0], r'\A[0-9a-f]{64}\Z')
        self.assertIn('if ($hash -ne $env:ZIG_SHA256) { throw', self.text)

    def test_the_embeddable_python_is_pinned_and_checked(self):
        (version,) = env_values(self.text, 'PYTHON_EMBED_VERSION')
        (digest,) = env_values(self.text, 'PYTHON_EMBED_SHA256')
        self.assertRegex(version, r'\A3\.12\.\d+\Z')
        self.assertRegex(digest, r'\A[0-9a-f]{64}\Z')
        self.assertIn('https://www.python.org/ftp/python/$env:PYTHON_EMBED_VERSION/python-$env:PYTHON_EMBED_VERSION-embed-amd64.zip',
                      self.text)
        self.assertIn('if ($hash -ne $env:PYTHON_EMBED_SHA256) { throw', self.text)

    def test_no_secrets_and_write_access_only_for_the_release(self):
        self.assertLessEqual(set(re.findall(r'secrets\.([A-Za-z_][A-Za-z0-9_]*)', self.text)), {'GITHUB_TOKEN'})
        self.assertRegex(self.text, r'(?m)^permissions:\n  contents: read\n')
        self.assertEqual(self.text.count('contents: write'), 1)
        self.assertIn('contents: write', self.jobs['publish'])
        self.assertIn("if: startsWith(github.ref, 'refs/tags/v')", self.jobs['publish'])
        self.assertNotIn('permissions:', self.jobs['kit'])

    def test_no_expression_is_pasted_into_a_script(self):
        scripts = run_scripts(self.text)
        self.assertGreaterEqual(len(scripts), 6)
        for script in scripts:
            self.assertNotIn('${{', script)

    def test_the_build_steps(self):
        kit = self.jobs['kit']
        builds = [line for line in kit.splitlines() if 'build_native_gate.py' in line and not line.strip().startswith('#')]
        self.assertEqual(len(builds), 1)
        self.assertNotIn('--native-image', builds[0])   # the pinned X4.exe is never on a runner
        self.assertNotIn('--test', builds[0])
        # the master's URL only from the repository variable, and only when it is set
        self.assertIn('X4MP_MASTER: ${{ vars.X4MP_MASTER }}', kit)
        self.assertIn("if ($env:X4MP_MASTER) { $kitArguments += @('--master', $env:X4MP_MASTER) }", kit)
        self.assertEqual(self.text.count('vars.'), 1)
        # the quick tests run before anything is packaged
        tests = kit.index('unittest discover')
        self.assertIn("'test_release*.py', 'test_playtest*.py'", kit)
        self.assertLess(tests, kit.index('build_native_gate.py --zig'))
        self.assertLess(tests, kit.index('python -B tools/make_release_kit.py @kitArguments'))
        self.assertIn('--python-embed', kit)
        self.assertIn('tools/make_playtest_msi.py --kit $env:X4MP_KIT', kit)
        self.assertIn('uses: actions/upload-artifact@v4', kit)
        self.assertIn('gh release create "$TAG" dist/*', self.jobs['publish'])
        self.assertIn('GH_TOKEN: ${{ github.token }}', self.jobs['publish'])


if __name__ == '__main__':
    unittest.main()
