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

## Experimental event RNG isolation

Adding `--event-rng key` scopes the random generator around each native event
delivery through the existing event-order hook. It requires `--event-order`,
which the `linux-core` preset already enables. The event option itself remains
off by default. `--event-rng observe` records generator use without replacing
the caller's word. Key mode restores that word on normal return; native
exception unwinding and process termination are outside that guarantee.

The derived seed includes the admitted tick, portable source key, native event
type and due time. An unmapped source instead shares its class key with other
sources of that class; retain the `local` count as a coverage limitation. Native
Linux vtable RVAs appear in these keys, so this mode does not establish seed
parity with Windows. Deliveries during snapshot loading still use native RNG.

For a focused comparison, add `--event-rng-rows-window FIRST LAST`. The inclusive
window may span at most 601 ticks and retains zero-draw deliveries as well as
drawn deliveries. Outside it, rows describe only deliveries that consumed RNG.
Use `--status-stride 1` when auditing complete row and status histories; optional
logging can otherwise omit intermediate rows. Retain arm, status, row and stop
receipts and reject gaps, overwritten ring rows and failed cleanup. An `entry`
word is the caller's ambient word before keying, not the derived initial word;
the receipt does not contain an independently observed exit word.

A 15000-tick two-instance comparison on source `18f3be5` agrees in all 126 full
census captures and the selected motion window. Its internal event histories
still differ, specifically in build-storage update traffic. This is a bounded
positive result, not a declaration that Linux desync is solved. See the
[motion investigation](../reports/LOCKSTEP-LINUX-MOTION-ONSET-20261108.txt)
for raw-evidence limits and the separate load-time scheduling investigation.

## Experimental build-update scheduling

`--build-rng` enables a separate, default-off hook before snapshot loading. It
requires the snapshot boot and world identities. The hook keys the randomized
delay in the pinned shared Container scheduler, which schedules build-update
events. That scheduler can run from constructors while an object still has its
Container vtable; this is broader than the final BuildStorage class alone.
Event-delivery keying cannot repair a delay already chosen during loading.

The wrapper temporarily seeds the calling thread's RNG from the available
portable identity and exact game-time bits, calls the original native draw
once and restores the ambient RNG word on normal return. It preserves the
scheduler's explicit-time path. Missing identities use a mapped class/time
fallback: multiple objects can then share a delay, so inspect fallback counts
and timing bursts. Unknown classes and invalid calls fail the gate. Function
bodies, referenced constants, the complete stub and the owned call window are
checked; restoration never overwrites a foreign patch.

`BUILD_RNG_STOP` reports keyed, native, unmapped and fallback counts, including
separate counts for calls outside admitted frames. `BUILD_RNG_ROW` retains only
the first 128 successful calls outside admitted frames; `rows` is the total
population and `overflow` states how much was not retained. The `phase` field
distinguishes actual load-phase samples from other boot phases. A complete
128-row sample with nonzero overflow is not a complete population audit.

These rows contain the selected seed and the native draw's actual float bits.
The logged caller is the scheduler continuation, not the higher loader caller.
Due times reconstructed from the sampled game time and draw are derived values,
not independent observations of the scheduled event queue. Retain event rows
around ticks 3600..4200 to compare the first update deliveries, along with world
evidence and a successful restored stop. This remains an experimental Linux
hook; Windows interoperability and playable multiplayer require separate tests.

A first same-PC run at source `fbb6f6d` passed 15,000 ticks with this hook and
event RNG enabled: 126 sampled full censuses agree, and the earlier event
aggregate split at tick 3601 is absent across all 15,000 status records. The
complete 3600..4200 delivery window and 11360..11900 script window also agree.
Loading reports 1,452 keyed calls with no fallback, but only the first 128 have
individual receipts; seven later class fallbacks remain. See the linked motion
investigation for sample coverage, cleanup evidence and remaining limits.


## Observe stored station economy deadlines

`--economy-watch SAVED_ID --economy-watch-window FIRST LAST` adds a default-off,
read-only Linux diagnostic to the bundle command. Supply one nonzero lowercase
16-digit saved ID for a Station and an inclusive window of at most 36,000 ticks
within the run. It requires the snapshot and saved identities; `linux-core`
already includes those prerequisites. Use a saved ID from the exact snapshot,
not a runtime handle from another process.

The observer reads the station's stored last/next economy times and native game
time as exact double bits. It resolves the imported identity after admission,
looks up the component afresh and checks class, identity and field stability
before emitting each row. It does not invoke an economy update or change RNG,
timers or the world-comparison payload. The reads are not locked against object
lifetime changes; matching double reads alone cannot exclude an ABA change.

Retain one `ECONOMY_WATCH_ARMED`, exactly one `ECONOMY_WATCH` row for every tick
in the requested window, a successful restored gate STOP and the matching
`ECONOMY_WATCH_STOP` footer. These rows bypass `--status-stride`. Missing objects
have explicit `present=0` rows; wrong classes, unreadable/non-finite fields or
identity changes fail the observation. The general runner's world PASS does
not validate this extra receipt stream or establish equality of these fields;
compare complete retained receipts independently before drawing conclusions.

On source `a2a19e6`, a 600-tick comparison demonstrated different first economy
deadlines for one station immediately after the same snapshot loaded. Game
time and last economy time matched. The rolling world digests still matched
because their current field set omits these deadlines. See the motion report
for the exact values, provenance and limits; the observation itself is not a
correction or proof of the earlier long-run failure's causal chain.

## Key economy draws during snapshot loading

`--economy-load-rng` is an experimental, default-off option for the Linux
`bundle` command. Add it to the build command above to key eligible economy
schedule draws during snapshot loading by saved identity and saved last-update
time. The bundle requires a snapshot and enables saved identities. When
building an overlay directly, supply `--gate-so`, `--snapshot`, `--mode clock`
and `--world-identities` as well.

The gate checks the pinned executable and the host's enabled CPU state before
arming. It requires XSAVE/OSXSAVE and a supported XCR0 layout; unsupported or
unverified state layouts are refused. Once installation reaches its code-swap
attempt, it is one-shot per process: start a fresh disposable client process
for another attempt.

If a draw cannot be keyed, the gate executes the native draw, advances the
native RNG and latches a failure. A fallback or failure means the run cannot
establish the intended deterministic result. Check the economy RNG status and
STOP records, including restoration and cleanup status, alongside the normal
run results.

`ECONOMY_LOAD_RNG_ROW` retains at most 128 indexed receipts. Their seed and
`expected_draw` describe the prepared integer draw; they do not observe the
stored floating-point deadline. Status counts and order-independent aggregates
cover all prepared draws, but cannot individually validate omitted receipts.
Use `--economy-watch` to compare actual stored station deadlines and validate
its complete receipt stream as described above.

On one PC, two Linux clients using source `ef85d188`, the same snapshot and
this option matched all 600 watched timer rows in a short run and all 36,000
watched rows for ticks 6001..42000 in a longer run. The long run's 42,000
rolling samples matched across 350 complete cycles; 33,724 retained drawn-event
rows also matched in order. A reverse-launch-order run and a separate actor-row
run each completed 4,200 ticks with matching rolling samples and watched timer
rows. Both retained internal timing differences described in the
[motion investigation](../reports/LOCKSTEP-LINUX-MOTION-ONSET-20261108.txt).
Only one station was watched. Rolling slices are sampled on different ticks and
omit the economy timer, so their equality is not an atomic whole-world census.
These results do not establish cross-PC, cross-CPU or Windows/Linux parity.

The source used by the native runs assigned stop reason 192 to an economy RNG
check failure and 191 to a path-transform observer failure; neither was a
failure observed in these runs. The subsequent integration correction
changes those codes to 201 and 202 respectively and corrects diagnostic
classification; that change was not part of these native runs.

## Time acceleration and SETA

The current Linux gate requires the native speed factor to remain `1.0` and the
relay checks a fixed game-time step of `1 / tick_hz`. SETA acceleration is not
supported by this gate. A multiplayer vote and a host-authorized factor applied
at the same admitted tick would need additional synchronized clock and relay
work; no such implementation is claimed here. Running more fixed steps per
wall-clock second, if possible, is a separate experiment from increasing game
time per step. Changing the configured tick rate also changes the step size.

## Observe NavMesh producer post attempts

`--navmesh-post-observe FIRST LAST` adds a default-off Linux diagnostic to a
snapshot bundle. It records startup (including epoch zero before the first
permit) and the inclusive live-tick window. It requires saved identities for
later diagnostic lookups. Only the pinned worker's `NavHelperGeneratedEvent`
post call is observed; rebuild and imported-event paths are not covered.

`NAVMESH_POST_ROW` identifies a producer post attempt, not successful queue
admission or event delivery. Native posting can reject the source and destroy
the event. The atomic phase/epoch stamp is the last published gate boundary,
not an exact engine timestamp. Raw handles, addresses and thread values are
local to one process. `NAVMESH_POST_IDENTITY` carries a separate saved-identity
hint looked up when draining the row; it does not prove capture-time identity.

The ring retains at most 8192 rows without overwriting them. Check contiguous
indices, the configured window, and the complete `NAVMESH_POST_STOP` counters.
Overflow, unreadable data, missing rows, active readers or uncertain restoration
invalidate the capture. The observer preserves the native call and its errno
behavior, but adds timing overhead; compare against a control without it.
No deterministic navigation correction is implemented by this observer.
