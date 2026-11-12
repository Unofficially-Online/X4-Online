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

The worker pin accepts the original executable bytes or the three known
exact-reciprocal preload patches. For the latter it verifies the actual exported
region, its read/execute permissions, complete patched windows and trampoline
slots, and both jump destinations. Only those verified windows are normalized
in a private copy before checking the original full worker hash. A mixed or
unknown patch is refused. The copied-ELF compatibility test in
`check_lockstep_gate_linux.py --x4 ...` invokes the real preload installer but
does not execute X4 code; it is not gameplay evidence.

## Independent Linux ship-control diagnostics

The optional `--independent-keyboard-frames N` bundle mode exercises two
independent input streams against retained native controllers. It accepts
2 through 600 frames; the scripted controller mode remains limited to 180.
The matching run uses `--controller-physics-discard` and
`--controller-physics-frames N`. This is a disposable experiment, not a
player-client launcher: both processes deliberately exit with code 223,
without restoring or resuming the retained controller state. Exit code 223
alone never indicates success. The runner checks the complete receipts and
stops the other owned process promptly when a finished peer reports failure.

The independent stream supports native states 2 through 9 (rotation and
strafe), 13 (increase throttle), and 14 (decrease throttle). Scripted throttle
and network throttle cannot be combined. Key release clears the held key;
it does not reset the native throttle command. On a German X11 layout,
synthetic input must distinguish the Z keysym from the physical SDL Z key.
Verify the captured native state rather than relying on the injector's exit
code. These tests do not validate physical keyboard or mouse input.

On source `7af0fa7`, a two-process, 180-frame Linux test transmitted state 13
and produced matching ship motion and 184 matching rolling world samples.
Source `04cf0a9` extended the comparison to 600 frames and included external
camera presentation, throttle, rotation and strafe. Both peers applied the
same ten input transitions, including state 14, and all 1,650 shared P2 body
observations agreed. The extended run nevertheless **failed at tick 417**:
the camera input-context anchor changed, followed by an upkeep call outside
the admitted recurring scope. Only 411 of 417 rolling world samples matched;
the later differences were not attributed by that run's retained leaf data.

A diagnostic repeat on `b68cb92` identified the camera-context change as one
new UI help-text registration (context 59, requester 8); all 15 prior payloads
were unchanged. The other peer's first failed upkeep predicate was
`entered=0`. These observations do not yet identify the runtime writer or
establish that either transition is safe to admit. The checks remain strict.
Normal UI changes, pause behavior, complete cockpit/HUD presentation, sector
transitions and safe controller teardown still require work. No cross-PC or
Windows/Linux player-session acceptance is claimed by these diagnostics.

A control run on `59741b4`, with the same inputs and without the external
camera, completed all 600 native frames on both peers. All 604 rolling world
samples, 2,400 recorded P2 body observations and 604 P1 poses matched. Its
strict evidence check still failed: both context counts changed from 13 to 14
at tick 417. That capture retained only the three selected payloads, so it
cannot independently identify the added entry. The result distinguishes a
completed native run from a passing validation and does not establish the
cause of the other peer's refusal in the camera experiment.

The diagnostic on `a6716a4` introduces a context policy with complete payload
receipts. It keeps the original context set immutable and permits only the
observed help-text entry to appear or disappear outside scoped P2 operations.
Each Update, upkeep and motion observation must leave the full context set
unchanged. The camera's separately validated 13-to-15 transition establishes
a new immutable baseline. The policy does not establish exclusion of other
engine readers or make controller exchange safe for a persistent session.

That combined camera/input run passed the bounded controller validator on both
peers for all 600 frames (ticks 5 through 604). Both admitted exactly the known
help-text payload at tick 417, outside protected operations, retaining every
prior context byte. All 600 applied records per slot, 2,400 P2 body observations
and 604 P1 exported poses agreed. The source inventory contained 97 files;
independent checks also matched all 53 retained source files to the commit.

The rolling WORLD comparison still matched only 594 of 604 samples. Five
samples differed in all recorded domains and five in timing alone. At tick 603,
the retained leaf data identifies only the UI helper `11f87` timing difference;
the other 8,688 selected leaf rows match. The final all-domain difference moved
to tick 597, while the chosen detailed capture was tick 598, so that run cannot
attribute the tick-597 discrepancy to a specific object. No mismatches are
filtered out. The runner's top-level `pass=false` is also deliberate for every
discard experiment; the bounded physics result and WORLD comparison must be
read separately. Both owned processes stopped, Steam state matched, and the
memory scope recorded a 9.57 GiB peak with no swap or memory-limit events.

### Player identity used by the view

The optional `--player-presentation-probe` checks the live and published cockpit
references once at tick 7. It requires the independent controller experiment
and at least four frames. The diagnostic does not change the local player,
switch cameras or install a presentation overlay. It calls a cached native
lookup only when that player's published parent already names the retained
cockpit; the acquired reference must be released and its count restored.

On source `6c0f8ef`, both clients completed 180 controller frames. P1's live and
cached lookups agreed, and the cached reference count changed from 10 to 11
and back to 10. P2's live lookup found its retained cockpit, but its parent in
the selected published bank was null. The cached P2 lookup was therefore
explicitly omitted. This identifies a missing presentation prerequisite;
it does not demonstrate a working P2 cockpit or HUD.

This neutral run used no external camera switch or input injection. All 184
rolling WORLD samples, 8,657 selected leaf rows, 720 P2 body observations and
184 exported P1 poses agreed between peers. Independent checks matched the
99-file inventory and all 54 source receipts to the named commit. Both owned
processes stopped, Steam state was unchanged, and peak memory was 9.43 GiB
with no swap or memory-limit events. These are bounded diagnostic results,
not acceptance of a persistent player session or cross-platform gameplay.

The separate `--player-view-query-observer` experiment counts calls to the
stock occupied-ship getter without changing its result. It requires the
independent controller experiment, the lifetime preload and at least four
frames. The exact pinned executable is instrumented before its main thread
starts. The observer remains resident until the disposable process exits;
it is never installed or removed in a running game.

At tick 7 the gate authenticates the frozen observer library and assigns the
observation thread. Calls made inside the probe's marked frame are counted
separately. Other calls on that thread retain their caller, returned identity,
first and last tick, and count, up to 64 distinct rows. The final receipt
reports overflow explicitly. Calls outside the marked frame can include
unmarked menu or notice work; they do not establish stock-UI-only provenance.
Unknown callers remain unknown. Neither these observations nor matching
world hashes establish a working guest cockpit, HUD or persistent session.

The first native observer attempt on `2f9e87e` activated both preloads but
refused to arm the gate observer at tick 7. Neither client produced an armed
or final query receipt, and the requested 180-frame comparison was not
completed. This is a failed diagnostic attempt, not evidence about HUD
callers or world equality. That run did not identify the failing predicate.
Both owned processes exited, Steam state matched and
the memory scope recorded a 9.68 GiB peak without swap or memory-limit events.

The subsequent interface check found that the gate's pthread-derived thread
token was passed to a preload API that requires the Linux kernel thread ID.
The retained preload rejected that token and accepted `SYS_gettid` in a
synthetic process. The adapter now obtains and validates the kernel ID only
after its existing gate-thread admission and provider checks. Refusal stages
remain sticky and are recorded without relaxing those checks.

With this correction, the bounded native run on `47f78e0` armed both observers
at tick 7 and finished at tick 184. Each client recorded 709 calls: 178 inside
the marked probe and 531 outside it, with no overflow or foreign-thread calls.
Each outside-frame aggregate has one caller outside the engine image; its
origin remains unclassified. These counts do not establish stock-UI-only
provenance or absence of other engine readers.

Both clients completed 180 independent-controller frames. All 184 WORLD rows,
8,657 selected rolling leaves and 720 normalized P2 body rows matched. This
was a neutral-input, disposable experiment ending with exit 223: it did not
restore or resume a persistent session. The P2 cached presentation lookup
remains explicitly omitted. All 105 inventory hashes and 59 source receipts
matched; owned processes stopped, Steam state matched, and peak memory was
9.334 GiB with no swap or memory-limit events. Guest cockpit/HUD acceptance
is still outstanding.

The next read-only probe on `8d87596` also validated the Linux UI-plan object
chain at tick 7. Both processes had the pinned presenter and controller
vtables, the expected cockpit/first-person/plan-change function pointers,
and a first-person flag of zero. The object identities and flag were unchanged
across the occupied-ship lookup. No UI-plan handler was called and no player
presentation fields were written by this probe.

Both disposable processes completed their 180 controller frames. The P2
published parent remained null, so its cached lookup was still omitted. The
run's bounded physics check passed; its top-level persistent-session result
remains false because this experiment intentionally exits without restoring
or resuming the game. All owned processes stopped, Steam state matched, and
peak memory was 9.350 GiB without swap or memory-limit events. These results
confirm the mapped UI objects in this scenario, not a working guest cockpit.

An independent audit matched all 105 inventory hashes and 59 source receipts
to `8d87596`, as well as all 184 WORLD rows, 8,657 selected rolling leaves and
720 normalized P2 body observations between peers. Inputs were neutral.

Static inspection identified the UI Move callback reached through the
secondary-interface thunk at `0xa77e90`. Its recursive UI mutex is also used by
the UI render handler at `0xa604e0`, so it excludes that particular handler.
It does not establish a common exclusion rule for all player-identity readers,
queued render work or object destruction.
The current worker rendezvous likewise does not establish renderer exclusion.
A temporary canonical-player presentation overlay therefore remains disabled
pending an appropriate execution and lifetime boundary.

The opt-in UI callback observer on `c690e16` now measures complete calls to
the pinned Move and render handlers. It installs the two vtable wrappers
before engine startup, authenticates their live slots at admission and closure,
and lets already admitted calls return before freezing the final counters.
Calls starting after closure still run the original handler, without counting.

In the bounded 180-frame run, each client recorded 176 entered and 176 returned
calls for each handler. Neither reported a foreign thread, changed object,
nested callback or unfinished call. The Move scope contained 704 occupied-ship
queries, including probe work; the render scope contained none. The callback
object matched the independently observed presenter plus its secondary-interface
offset. Both deferred native receipts exactly matched the tick-184 Lua records.
This establishes the observed callback boundary, not exclusion of all other
readers or readiness of the guest's cockpit.

All 184 WORLD rows, 8,657 selected leaf rows and 720 normalized P2 body rows
matched between these neutral-input Linux processes. The independent audit also
matched 105 bundle files and 59 source receipts. Both processes ended with the
intentional diagnostic exit, with no restoration or resume. Cleanup left no
owned processes; Steam state matched. Peak scope memory was 9.373 GiB, with
no swap or memory-limit events.

The comprehensive Linux gate check initially stopped because its Lua method
registry test did not include the two added methods. `f7ca118` corrects that
test and verifies refusal outside an admitted worker. The corrected bridge
passed with 250 methods; the remaining 29 synthetic scenarios met their
expectations in a separate continuation. Earlier compiled and copied-image
checks passed. Windows TLS compilation was unavailable because MinGW was
absent. This is not a claim that the complete repository suite passed.

The default-off presentation fork probe now checks the native occupied-ship
and room getters in an isolated child. On `6754177`, both Linux processes
reported the sequence P1 ship 442686, P2 ship 442530, then P1 ship 442686.
The live and cached room getters agreed on the retained P2 room; its reference
count changed from 8 to 9 and back to 8. Each child exited and was reaped, and
the parent player bytes were unchanged before the simulation continued.

Both processes then completed 180 controller frames with the bounded physics
check passing. This proves a child-only getter roundtrip, not a rendered guest
cockpit, persistent-session restoration or cross-platform gameplay. The overall
persistent-session result remains false because the harness intentionally exits.

Earlier attempts remain recorded as failures. Native diagnostics identified an
EINVAL at the memory-read syscall; explicitly passing full-width variadic
arguments removed that failure. The next attempt stopped while reading the
memory map. The successful run captured 262,189 and 262,532 bytes, each above
the original 256 KiB limit. The capture now uses a bounded 2 MiB workspace
allocated before the quiet section and rejects overflow or incomplete input.
Focused syscall, map-boundary and malformed-evidence tests passed.

The next diagnostic temporarily selects P2 for one guest UI Move callback,
restoring P1 around each Lua `onUpdate` and before returning to the worker.
The first native attempt (`0bd9485`) stopped at the return check. A diagnostic
repeat (`9a15325`) recorded three applications and two restorations: X4 had
executed two complete pause/resume pairs within that callback, while the first
implementation required exactly one. Both failed runs remain retained. They
do not establish completion of the requested 180 controller frames.

The revised diagnostic accepts one to eight complete update pairs within that
single callback. Every pause checks restored P1 fields and occupied-ship identity;
every resume checks the canonical fields and retained P2 data before applying
the temporary view. A ninth update, unbalanced transition or failed restoration
terminates the disposable process. Version 2 evidence records the update count
and requires exactly one more application and restoration than update pairs.
The guarded callback also terminates on the tested C++ exception and dynamically
bound `_longjmp` paths. This does not establish coverage of all possible escape
mechanisms or exclusion of unrelated threads reading the temporary fields.

On `8b086f1`, the first revised attempt stopped before the UI experiment because
the host's child-fork prerequisite observed another running thread. The unchanged
bundle's separate repeat completed the guest callback: two update pairs, three
applications and three restorations, with native and Lua receipts agreeing.
Both processes then completed 180 controller frames. All 184 WORLD rows,
8,657 selected leaf rows, eight raw domains and 720 normalized body rows matched.
The callback observer closed with balanced entries/exits and no counted anomalies.

The independent audit matched all 113 bundle hashes and 67 source receipts.
Six guest getter calls outside the marked probe returned P2 during tick 9;
their caller was outside the pinned executable, so these calls are not attributed
to a particular stock HUD component. Cleanup left no test processes and Steam
state was unchanged. Peak scope memory was 9.440 GiB, with no swap or memory
events. The earlier prerequisite failure remains preserved separately.

This is native acceptance of one disposable callback, with subsequent bounded
simulation comparison. The source remains default off. Persistent guest cockpit
presentation, general reader exclusion, normal session resume and cross-platform
gameplay remain unproven.

The follow-up on `b3ec884` completed eight consecutive guarded callbacks at
ticks 9–16 and returned its summary at tick 17. The first callback executed two
update pairs; the other seven executed one each. Every pause and final return
observed P1, and every overlay and resume observed P2. Both clients completed
180 physics frames, with the same 184 WORLD rows, 8,657 selected leaves, eight
raw domains and 720 body rows. All 113 inventory hashes and 67 source receipts
matched in the independent audit. Peak memory was 9.410 GiB, with no memory
events or swap; cleanup left no test processes and Steam was unchanged.

The 27 non-probe P2 getter results in this window still have an unclassified
caller outside the pinned executable. They do not prove that the cockpit or
HUD displayed the guest correctly. The next default-off diagnostic extends the
same guarded lease to 160 callbacks (ticks 9–168, summary 169) and captures the
disposable guest window. The runner requires at least 165 independent physics
frames. Its retained references remain owned by the disposable physics path;
this is not an in-process teardown or normal-session resume claim.


The 160-callback run at `1d3223f` subsequently passed independent checks. Both
clients completed 180 physics frames; all 184 WORLD rows, 8,657 selected leaves,
eight raw domains and 720 normalized body rows matched. The guest emitted
exactly 160 ordered callback receipts for ticks 9–168 and its summary at 169.
Peak memory was 9.364 GiB, with no memory events or swap. An earlier attempt
never reached the gate because both X11 windows remained unmapped. The passing
repeat required an explicitly recorded window-manager bypass for the two
owned disposable windows. This is a test-environment qualification, not a
normal Linux launcher acceptance. Both runs cleaned up their owned processes;
the failed startup and its errors remain retained.

A read-only follow-up at `e4e9865` sampled the UI state before, during and after
the first overlay. All three observations reported selector 1, first-person
flag 0 and current plan `cockpit`. The candidate camera, mode 0 and external
target ID 442691 were unchanged. The target pointer matches the canonical P1
actor in the retained presentation receipt, rather than an occupied ship. A
different ID from P2's ship therefore does not by itself identify a camera
error. These fields do not establish which camera produced the rendered view. That
run independently passed the same bounded simulation and callback comparisons, with all 114 inventory hashes and 68 source
receipts matching. It peaked at 9.671 GiB without memory events or swap, and
cleanup left Steam unchanged and no test processes. Both attempted window
captures failed in the separate capture helper; there is no visual acceptance.

The explicit plan-re-emission diagnostic at `6651ef3` subsequently passed its
bounded native comparison. It called the stock first-person handler and then
the cockpit handler once inside the first guest overlay. The observed plan
changed as requested; the sampled RNG word, context census and candidate camera
fields were unchanged. The strict collector checks full native receipt syntax,
presentation identities, input-file hashes and the order between the surrounding
VIEWSTATE observations and first overlay frame. The host did not perform a kick.

An independent audit verified 118 bundle hashes and 71 source receipts, all
160 overlays, 180 physics frames and the same world/body comparisons. Peak scope
memory was 9.373 GiB, with no swap or memory events. Owned-process cleanup was
complete and Steam state was unchanged. The recorded owned-window startup
workaround was still required.

The capture helper retained 49 images of the authenticated guest window only.
The last image contains a faint cockpit and HUD, with maximum RGB values of 14.
The DirectColor visual was not colour-calibrated, and these asynchronous captures
do not establish a correct P2 camera or an image drawn inside a particular
identity scope. This result supports the bounded handler sequence, not a playable
persistent guest. The next investigation reads controller and candidate local-pose
identities before attempting a Linux seat-eye correction. Normal session resume
and camera/render scope remain unproven.


The read-only seat-pose census at `4eeeb002` then collected all six requested
observations. P1 had the secondary FirstPersonController vtable `0x3b5edf8`
and a finite, rigid published local transform with translation
`(0, -0.038251698, -0.14931345)`. This pose stayed unchanged while the overlay
changed P1's parent to the guest room. P2 had no movement controller or published
local transform in any sample. Worker view 1 and selected bank 1 were stable.
The collector therefore reports complete observations but incomplete pose
coverage; it does not invent an identity transform for a null pointer.

The pinned isolated snapshot identifies P1 as an Argon scout and the retained
P2 target as an Argon fighter. P1's existing seat does not establish the correct
seat for the other model. Static Linux analysis corroborates the published
transform field, and distinguishes the primary FPC vtable from its secondary
interface; copying Windows vtable slot numbers would use the wrong ABI.

The independent audit verified 121 bundle hashes and 73 source receipts,
all prior bounded simulation comparisons, and 47 owned-window image hashes.
The sampled DirectColor lookup table was exactly linear; the sample was not
atomic with image capture and is not a calibration claim. Peak scope memory was
9.426 GiB without swap or memory events. Cleanup left no test processes and
Steam state was unchanged. No live seat-query call or pose correction was
included in this run.

The explicit parent-worker seat query at `ffa3281d` completed both stock
calls before the first guest overlay. Separate temporary references protected
P1's ship and room, and were released in reverse order. Both calls returned a
finite rigid transform. The scout seat was `(0, -0.038251698, -0.14931345)`;
the fighter seat was `(0.005983999, -0.070649743, 0.238356113)`, with identity
bases in both. Their origins differ by approximately 0.389067 metres.

An independent audit verified all 125 bundle hashes and 76 source receipts,
160 UI callbacks, 180 physics frames, 184 matching world rows, 8657 matching
leaves, eight raw domains and 720 body rows. The sampled RNG and 13-node input
context census were unchanged. All 52 owned-window capture hashes matched.
Peak scope memory was 9.363 GiB, without scope swap or memory events. Cleanup
left no X4 processes and Steam was unchanged. The startup helper observed both
test windows already viewable in this run.

These returned transforms remained scratch data: P2 still had no movement
controller or published local pose. The stock helper may populate internal
caches; this comparison does not prove that it is read-only. It also does not
establish a corrected camera, a persistent guest or safe session resumption.
The top-level run result remains false because this diagnostic deliberately
discards its processes; the bounded physics comparison is recorded separately.

The engine-free helper at `88f60a2` computes the relative seat transform and
preserves each canonical pose's four metadata words. Its synthetic tests cover
distinct rotated seats, a displaced pose, matching seats, signed-zero metadata
and invalid or overflowing input. It is not activated in the native gate.

### Passive live-pose observation (12 November, `62b847da`)

The disposable `ui-live-pose-180` run records genuine owner-worker calls through
the pinned FPC entry. The resident entry hook forwards each call once without
changing its arguments or output. It records only a pointer-filtered observation;
it does not retain the FPC object or exclude pointer reuse and other readers.

The independent audit verified 131 bundle hashes and 81 source receipts. Both
peers completed 180 physics frames, and the guest completed 160 UI callbacks.
All 184 world rows, 8,657 selected leaves, eight raw domains and 720 body rows
matched. The hook observed 43,264 completed calls: 2,030 during UI Move, none
during the observed UI Render callback, and 41,234 outside those TLS brackets.
Every call used mode 1. The first 32 samples returned the queried P1 seat pose;
43,232 later calls were counted but their output was not retained.

The sampled return addresses `0x1314317` and `0x1314407` belong to two branches
of the same Player transform function. They do not identify the higher caller
or establish active-camera use. UI Move also includes canonical Lua pauses,
so this result does not yet identify calls made while the P2 overlay is open.

Cleanup left no owned processes or X4 instance, and the recorded Steam state
was unchanged. The run retained 51 guest-window images and peaked at 9.356 GiB,
with no scope swap or memory-limit events. Exit 223 and the overall false
session result remain intentional: this is not persistent-session acceptance.
The next observation distinguishes open overlay, paused and outside calls
before applying any coupled live/published seat-pose correction.

The follow-up `ui-pose-scope-180` run at `fc5d0236` distinguishes completed open,
paused and outside states from the interval while fields are changing. Its
independent audit verified 133 bundle hashes and 82 source receipts. All 2,030
Move calls occurred in the open overlay; 41,234 calls occurred outside it,
and no calls were observed while paused or changing. There were 321 successful
opens, 161 pauses and 160 final closes. Four representative rows cover three
previously retained calls and one additional call; they are not four extra
invocations or complete per-call data.

The same 180-frame physics and world/body comparisons passed. Cleanup was
clear, Steam state was unchanged, 51 images were retained, and peak memory was
9.366 GiB with no scope swap or memory-limit events. This establishes a bounded
owner-thread interval for the next correction experiment, not active-camera
selection or exclusion of other readers. The engine-free published-pose lease
at `c9af1f7` has focused synthetic coverage but remains unused by these runs.

### Coupled seat correction: first native failure (12 November, `fdff09e`)

The default-off `--player-ui-seat-correction` experiment applies the queried
seat difference to both live FPC output and the published local pose during
the guest's UI Move interval. It restores the published pointer before the
identity overlay. Each publication uses a distinct resident scratch block;
the original pose bytes remain untouched. The live path accepts only the
observed mode-1 call from `0x1314317` into the authenticated worker stack.
Passive receipts continue to contain raw stock output; separate receipts
describe corrected output. This is a disposable experiment, not a session
default or a claim of object lifetime and reader exclusion.

Root validation passed three focused native test wrappers, thirteen collector
tests and the Linux gate checker. The first collector invocation used module
imports that could not resolve a sibling fixture; standard unittest discovery
passed. The original import failure remains in the private evidence.

The first native run, `ui-seat-correction-180`, **failed**. Tick 9 completed
three apply/restore pairs. Independent raw analysis verifies that the first
published P1 pose became the queried P2 seat pose, with metadata preserved,
and that the published pointer was restored exactly. At tick 10, after physics
upkeep, the next overlay apply refused and terminated the guest. No completed
160-callback interval, correction aggregate or terminal physics receipt exists.
The generic apply failure does not identify which admission predicate failed.

The independent audit verified all 139 inventory hashes and 87 source receipts.
The nine common world rows and 22 common body rows match; the unequal partial
row counts are retained, not treated as a completed comparison. Cleanup left
no owned processes or X4 instance, with Steam state unchanged. Memory peaked
at 9.250 GiB without scope swap or memory-limit events. The next step is a
failure-specific diagnostic; the admission checks remain enforced.

The follow-up diagnostic at `7e25551` identified the refusal: at tick 10 the
worker, entered phase, object ownership and view were valid, but X4 selected
bank 0 while the correction still expected the initially captured bank 1.
The current identity transaction had touched no fields; the preceding
published lease was closed after three apply/restore pairs. The independent
audit verified all 139 inventory hashes and 87 source receipts and clear
cleanup. This is evidence for binding the selected bank per Move; the failed
run remains a failure and does not establish a working bank-switch correction.

### Seat correction across presentation banks (12 November, `55ac5bd9`)

The follow-up captures the active presentation bank at each admitted UI Move.
Both the identity overlay and the published pose use that captured bank until
restoration completes. The original fork request remains unchanged. The first
Move must still match the initial seat query; a bank change during a lease or
an unexpected change to the original pose causes a refusal. Synthetic tests
cover alternating banks, an initial mismatch and a change during a lease.
Three focused test wrappers and the Linux gate checker passed.

The disposable native run `ui-seat-bank-180` completed 180 physics frames on
each peer and all 160 guest UI callbacks. The unchanged strict collectors
accepted the overlay, seat queries, live-pose observations, pose scopes and
coupled correction. The correction closed with 321 applies and 321 restores,
2,030 live calls and two retained output samples. Its terminal state was
outside the overlay, with the published pointer restored.

Supplemental `SEAT_BANK` receipts record the first closed Move in each bank
and totals of 80 Moves per bank. These receipts are separate from the strict
collector. Independent analysis verified each recorded slot against the P1
actor and selected bank, the restored pointer, and ordering before the matching
frame. This does not provide a continuous per-frame bank trace. The original
failed runs remain unchanged.

The independent audit verified all 139 inventory hashes and 87 source receipts
and reproduced the strict collector results. All 184 world rows, 8,657 selected
leaf rows, eight raw domains and 720 body rows matched between peers. The first
published pose and two retained live outputs matched the queried P2 seat, with
metadata preserved. The audit retained hashes for 61 captured images.

Both peers deliberately exited with code 223 at the diagnostic boundary;
the overall session result remains false because this does not prove a safe
return to ordinary gameplay. Cleanup reports no remaining owned process or
X4 instance and unchanged Steam state. The recorded memory peak was 9.323 GiB,
without scope swap or memory-limit events. A captured final image contains a
dark cockpit and HUD; this alone does not establish correct guest rendering.
Persistent guest control, object lifetime and exclusion of other readers
remain unproven.
