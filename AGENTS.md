# Working on this repository

- The project is deterministic lockstep for X4 9.00. Read README.md, then
  docs/ARCHITECTURE.md; read docs/LAUNCHER.md before running X4.
- Never load, write or delete the player's normal X4 profile, saves or settings.
  Use the launcher's disposable profiles. Never modify or redistribute Egosoft
  files; the gate patches memory only and must keep failing closed on any other
  build.
- Close X4 before native tests. Run X4 test clients windowed.
- Checks: `tools/dev/check.py` (the full suite targets Linux/WSL; skipped tests
  count as failures), and `tools/dev/build_native_gate.py --zig <0.15.2> --test
  --native-image <X4.exe>` for gate changes. A synthetic or mocked test is not
  evidence of behaviour in the real game; say which one you ran.
- Keep `.local/` for logs, overlays, snapshots, toolchains and credentials; never commit it.
- Write original code. Neresco projects are research references only.
