"""Run the development checks from any working directory, without launching X4.

Any skipped test fails the run. --allow-skip TEXT (repeatable) accepts skips whose
reason contains TEXT; CI uses it for checks that need hardware or files a hosted
runner cannot have (X4 executables, the released lane runner). Every skip reason
is printed either way.
"""
import argparse
from pathlib import Path
import platform
import sys
import unittest

parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--allow-skip', action='append', default=[], metavar='TEXT')
args = parser.parse_args()
ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'src'), str(ROOT / 'tools'), str(ROOT)]
try:
    from lupa.luajit21 import LuaRuntime
except ImportError:
    raise SystemExit('LuaJIT test runtime missing. Run: python3 tools/dev/bootstrap.py')
print('Project:', ROOT)
print('Version:', (ROOT/'VERSION.txt').read_text().strip())
print('Python:', platform.python_version(), 'Platform:', platform.platform())
suite = unittest.defaultTestLoader.discover(str(ROOT/'tests'))
result = unittest.TextTestRunner(verbosity=2).run(suite)
refused = [(test, why) for test, why in result.skipped if not any(text in why for text in args.allow_skip)]
for test, why in result.skipped:
    print('SKIP', 'refused' if (test, why) in refused else 'allowed', test.id(), '-', why)
if refused:
    print('Incomplete validation: skipped checks:', len(refused))
raise SystemExit(0 if result.wasSuccessful() and not refused else 1)
