# Architecture

## Model

Deterministic lockstep. All clients load the same snapshot (a native X4 save),
advance the simulation in fixed ticks, and apply the same inputs on the same
tick. If the engine is deterministic, every client's universe stays identical
without sending world state. Economy, missions and NPC AI are shared for free,
as long as they stay deterministic.

## Pieces

**Gate** (`src/native/engine/lockstep_gate.c`, built as `x4lan_gate_64.dll` by
`tools/dev/build_native_gate.py`). It is loaded with `package.loadlib` by the
probe. It verifies the exact X4 9.00 executable (size, PE timestamp, full
SHA-256, and per-site byte hashes), then patches code in memory with
compare-and-swap and restores the original bytes on stop. Every intervention is
off unless a `native_gate_*` config key enables it. Its jobs:

- **Admission:** each simulation frame waits for the relay's permit for that tick.
- **Fixed tick:** a constant timestep (`native_gate_tick_hz`, usually 30 Hz).
- **Random streams:** engine RNG draws keyed by tick and stable object identity.
- **Ordering:** events, pathing, attention, physics bodies and containers in a
  canonical order instead of heap-address or wall-clock order.
- **Floating point:** approximate reciprocals and spline lengths redirected to
  exact code, so Windows and Linux can agree.

**Probe** (`tools/dev/native_gate_probe.lua` plus `native_gate_*.lua` modules, in
X4's UI Lua). It samples inputs, encodes them into the client's input blob,
applies everyone's inputs at *t + d*, computes the per-tick state digest, and
writes `NATIVE_GATE` log lines.

**MD fixtures** (`tools/dev/native_gate_*.xml`). These are Mission Director scripts
that spawn test fixtures, such as the 12-ship battle, and run scripted orders.

**Relay** (`tools/dev/native_gate_relay.py`, standard library only). It runs the
per-tick barrier for 2 to 8 clients. In protocol 3 each tick carries the opaque
input blobs; protocol 4 adds a lookahead window so the round trip leaves the
critical path. It compares digests and reports the first differing tick.
Wire format: [LOCKSTEP-NSLOT-WIRE.txt](LOCKSTEP-NSLOT-WIRE.txt).

**Overlays** (`tools/dev/build_native_gate_overlay.py`, `native_gate_sandbox.py`,
`native_gate_players.py`, `native_gate_scenarios.py`). Each builds a frozen
directory containing the gate DLL, probe modules, MD fixtures and a config
fragment. The launcher turns it into each client's `x4_lan_lab` extension on top
of `src/lockstep/base`.

**Launcher** (`src/lockstep/launcher`, `python tools/x4ls.py`). See [LAUNCHER.md](LAUNCHER.md).

## Inputs

- **Pilot by wire:** flight controls are sampled per tick and applied on every
  client's own Steering model at *t + 3*. X4's own controls are parked.
- **UI replay:** menu actions (autopilot, docking, weapons, missiles, the order
  queue) are re-executed on every client from replayed events.
- **Player orders:** move, attack, dock, buy, sell and more travel as the `P`
  section and run as MD orders, checked against the owner of each ship.
- **Generic FFI relay:** the UI write guard (`lockstep_ui_exports.h`) redirects
  X4.exe's export table; a game write the UI makes outside the relay is counted,
  refused or captured and sent in the `X` section, and every client runs it at the
  apply tick ([PLAYERS.md](PLAYERS.md)). Lua-global writes are wrapped by the probe.

## Desync handling today

The relay stops on the first mismatching digest, or in observe mode keeps
running and records every mismatch. The census tools (`tools/dev/census_*.py`)
compare world captures object by object to name the class that parted.
`x4ls triage RUN` reruns a diverged session with diagnostic windows around the
split and reports the first differing row of each family; soaks keep always-on
attributions (lazy station updates per call site, every drawing actor scope) for
splits that depend on timing and do not come back in a rerun. Checkpoints
(`--checkpoint-at`) save every client's world at one tick for `checkpoint_diff.py`.
Recovery (resync from a mid-game save) and joining a running game are the next
milestone.

## Limits

- One pinned X4 build. A game patch disables the gate until it is re-verified.
- Windows-only for v1. The Linux gate (`src/native/engine/linux`) is in progress.
- Tested on one PC so far. Intel vs AMD CPUs may differ in approximate float instructions.
