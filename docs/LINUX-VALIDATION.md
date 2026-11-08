# Local Linux lockstep validation

This developer workflow runs two native Linux X4 instances from one disposable
snapshot. It exercises the simulation gate and compares periodic world captures.
It does not provide the Windows launcher's player input, UI replay or hot-join
feature set. A passing run is not evidence of Windows/Linux interoperability or
of a session between different PCs.

## Requirements

- The pinned Linux X4 9.00 executable and its installed game data.
- Steam already running as your desktop user; close any existing X4 game.
- Python 3.11+, the project's bootstrap environment, Zig 0.15.2 and `bwrap`.
- A local X11/XWayland display with a readable `XAUTHORITY` file.
- Enough available memory: the runner requires 6.5 GiB per local instance.

The examples use an existing desktop and a hardware Vulkan driver. They do not
demonstrate operation without a GPU. The separate Xvfb/lavapipe mode remains
available for server experiments.

## Capture a new test world

Use a new output directory. The capture tool starts the creative game with the
12-ship test fixture, waits for X4's save-completion event, validates the complete
gzip/XML save and then closes its own game process.

```sh
python tools/dev/bootstrap.py
.venv/bin/python tools/dev/linux_snapshot_capture.py \
  --game "/path/to/X4 Foundations" \
  --out .local/captures/linux-example \
  --display :0 --seed 2222222
```

The resulting save is `.local/captures/linux-example/save_009.xml.gz`.
Inspect `result.json`: a valid save alone does not imply successful cleanup.
The tool retains failed attempts and their logs too.

## Build and run a comparison

The `linux-core` preset explicitly enables the supported simulation options,
including time-based MD checks. Leaving that option out preserves the manual
configuration mode used for individual hook experiments. Explicit value options,
such as `--find-order observe`, override the preset; omit the preset when testing
with a Boolean feature disabled. This preset does not enable player control.

```sh
.venv/bin/python tools/dev/linux_gate_live.py bundle \
  --zig /path/to/zig-0.15.2/zig \
  --snapshot .local/captures/linux-example/save_009.xml.gz \
  --out .local/linux-example --preset linux-core --ticks 6000 --stride 120

.venv/bin/python .local/linux-example/linux_gate_live.py run \
  --root .local/linux-example --user "$(id -un)" \
  --game "/path/to/X4 Foundations" --display :0 \
  --vulkan-icd /path/to/64-bit-vulkan-driver.json \
  --no-discard --name comparison-1
```

For example, Fedora's AMD driver is commonly
`/usr/share/vulkan/icd.d/radeon_icd.x86_64.json`; select an installed driver for
your machine. Add `--preflight` to the run command to check prerequisites without
launching anything. A second run can use `--name comparison-2 --launch-order 2,1`.
Repeated differences that follow a client number still need diagnosis; startup
and relay scheduling can affect that observation.

The runner reads the world seed from the snapshot and uses it for both clients.
It verifies the bundled file hashes and refuses an unexpected X4 executable.

## Read the result

Each run retains `run-result.json`, `relay.json`, and both clients' logs and
installed configuration. Exit zero requires:

- Both clients finish the requested tick and restore their gate hooks.
- Every scheduled capture exists exactly once, with valid identity and scan
  completion records and no fatal gate messages, including late messages.
- The captured world hashes match and the local relay reports success.
- The requested portable math preload is active, Steam's process identity is
  unchanged, and no owned processes remain.

World captures occur at tick 1 and every `stride` ticks. Equality at those points
does not establish equality of all world fields between captures. The native
census reports unknown class, motion and reference counts as coverage limits.
Retain these counts with any result; do not describe a passing digest as proof
that unobserved state is synchronized. An external-relay run validates only its
local participants until both hosts' evidence is compared.

For a focused motion investigation, add `--world-motion-watch SAVED_ID ...`
and `--world-motion-window FIRST LAST` to the bundle command. This diagnostic
is off by default, accepts up to four imported saved IDs and an explicit window
of at most 18,000 ticks within the run. It reads only NavigationController state;
a missing object, lost identity, unsupported controller or unstable read fails
the evidence check. It does not add fields to report digests or census coverage.

The automatic runner validation checks every declared tick and ID exactly once,
including the arm receipt and successful stop/footer. `WORLD_MOTION` rows contain
the existing canonical read/write motion and timing preimages, including census
metadata trailers. Retained census detail or object leafs at matching ticks are
checked against them. Physical read/write selectors and cursor are diagnostics;
their raw values are excluded from equality. These receipts bypass status
thinning, including when the regular census uses rolling slices. Results appear
as `world_motion_*` fields in `run-result.json`; a single local client reports
zero compared pairs and no peer-equality verdict. There is no standalone motion
analysis command. Passing this observer covers only the selected objects and
window, and does not establish equal hidden pathing state or causality.

All game views use private HOME/XDG profiles, disabled autosaves and Workshop
sync, and only official DLC plus the test extension. Normal profiles and game
files are mounted read-only. Device access, shared memory, Steam IPC and the
desktop display remain shared. Cleanup checks process identity and lock-file
ownership before changing either. Raw saves and logs stay under `.local/` and
must not be committed or distributed with the code.

See [the retained test report](../reports/LOCKSTEP-LINUX-LOCAL-20261108.txt) for
specific runs and their limitations.

## Reduce diagnostic stalls

`--world-log-mode hashes` omits the large object-detail pages while retaining
the complete census at every scheduled capture tick. `--probe-profile` records
scan and log-output time separately. CPU-supported SHA acceleration and an
exact-byte leaf cache preserve the SHA-256 values and all captured fields.
Neither logging mode repairs a simulation divergence.

The Linux-only `--world-rolling-slices 120 --world-log-mode hashes` experiment
spreads the census across 120 ticks. It is off by default and requires
created-identity capture, enabled by `--preset linux-core` or the explicit
`--created-identities` option. At the beginning of each cycle, the native gate
fixes a sorted cohort of imported and previously captured created identities.
Tick by tick, it reads a disjoint range of that
cohort; object pointers are resolved again at the sampling tick. Newly created
objects enter at the next cycle boundary and can wait almost two cycles for
their first state sample. Creation-event digests continue to run separately.
An object born and destroyed entirely between cycle boundaries may never enter
the census cohort; its state is not covered by these slices. The existing sparse
instantaneous mode likewise cannot capture objects absent at all capture ticks.

Every slice carries its tick, cycle, range, cohort size, coverage counters and
four digests in the distinct `WORLD_ROLLING_1` schema. Peers compare the same
range at the same tick. These records represent samples at different times,
not an instantaneous universe snapshot. They cannot be used for hot join,
checkpoint recovery, full object-detail reconstruction or strict per-tick
player-transition checks; incompatible configurations are refused. The full
instantaneous mode remains available for those investigations.

The evidence validator requires every requested slice in order and reports any
partial final cycle explicitly. Unknown fields remain outside coverage in both
modes. A rolling pass establishes only agreement in the sampled fields at the
declared sample times. It is not equivalent to passing the old instantaneous
census, nor proof of Windows interoperability or playable multiplayer.

Optional `--status-stride N` (1 to 3600) reduces ordinary diagnostic status
output; omitting it preserves the existing configuration. Native ring drains,
per-tick digest reports and rolling evidence remain enabled. Intermediate
diagnostic status rows can be omitted, and failure counters monitored through
those statuses may only be noticed on a written status tick or at stop. Use
the default when an investigation needs the intermediate diagnostic rows.

## Experimental ordered pathing

The `linux-core` preset normally captures pathing snapshots. Adding
`--travel-determinism` replaces that strategy with ordered, synchronous execution
of the two pinned path-job dispatch sites. The runner refuses a configuration
that explicitly enables both strategies. This experiment is off by default:
it changes scheduler bookkeeping, and indirect scheduler readers and transitive
thread-local effects are not fully established.

To reproduce the ordered rolling comparison, add these options to the bundle
command above:

```sh
--travel-determinism --world-log-mode hashes --world-rolling-slices 120 \
--status-stride 60 --probe-profile
```

The native adapter verifies the executable bodies and the complete owned jump
stubs before activation. A runtime dispatch mismatch exits the disposable
process with status 190; that is a failed run without a successful restoration
claim. Retain the peer's cleanup evidence too. A successful run must retain both
the main STOP and `TRAVEL_DETERMINISM_STOP` with restored hooks.

Ordered pathing passed one local 12000-tick rolling comparison, but the
final-source repeat with reversed launch order diverged in motion at tick 11612.
An earlier snapshot-mode control also diverged. The desync is still unresolved;
one passing comparison is not evidence of reliable whole-world synchronization.

For a bounded transform investigation, add `--path-transform-observe FIRST LAST`
to an ordered-pathing bundle, for example `--path-transform-observe 8461 8520`.
The interval is inclusive and may cover at most 600 ticks. This diagnostic
requires ordered pathing and cannot run with `--step-chain`, which owns one of
the same call sites. Retain `PATH_TRANSFORM_OBSERVE_ARMED`, the STOP status and
all `PATH_TRANSFORM_OBSERVE_ROW` lines from each client.

The observer compares native cached results with candidate live transforms only
for eligible, identified ancestor chains inside ordered jobs. It records rows
only for differences; `compared=0` is inconclusive, and excluded calls are not
covered. A full row buffer fails the run rather than producing complete evidence.
The candidate invokes engine virtual methods and may have side effects, so this
is an experimental diagnostic rather than a passive or production-safe observer.

See [the performance investigation](../reports/LOCKSTEP-LINUX-PERFORMANCE-20261108.txt)
for measured intervals, retained failures and graphics findings.
