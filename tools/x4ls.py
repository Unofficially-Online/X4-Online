"""Run the x4ls launcher from a checkout: python tools/x4ls.py <command> ..."""
from pathlib import Path
import sys

sys.dont_write_bytecode = True   # an installed kit stays as it was installed
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'src'))
from lockstep.launcher.__main__ import main  # noqa: E402

sys.exit(main())
