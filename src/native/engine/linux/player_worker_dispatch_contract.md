# Worker dispatch contract (engine-free)

This default-off contract supplements `frame_worker_recurring.h`. It cannot
prove a native frame owns a particular semaphore wake, and it does not enable any
hook. Use in a native session remains blocked until the bootstrap and all relevant
semaphore users are authenticated.

The opt-in adapter must compose with the existing `lmt_worker_wait` at the pinned
movement wait, and with the admitted main FrameTick post sites. It must retain
original arguments, invoke each original exactly once, and preserve the full raw
return and incoming/outgoing `errno`. The post-begin mark precedes the original
post, because a waiting worker can wake before that call returns. The existing
post-join boundary must wait, with a deadline, for post-return and the matching
movement wait before allowing `fwrr_worker_arrive`; the existing FWRR token and
tail release then own the rest of the generation. Any contract failure must stop
before native simulation resumes. The adapter must authenticate the semaphore,
threads, pinned sites, and the native return semantics independently.

Bootstrap must rule out a queued permit and an already awakened old pass, not
merely observe a zero count. The other known wait site and all post paths must be
accounted for during the active scope; an unexpected consumer, post, or shutdown
fails closed. The static reference audit does not exclude dynamically computed
aliases. No test in this fixture proves those native facts, general writer
exclusion, disconnect handling, or a persistent session.

The resident implementation is in `player_worker_dispatch_adapter.h` and
`gate_player_worker_dispatch.h`. The finite runner enables it explicitly with
`--worker-dispatch --semaphore-observer`; it sets `X4_DEV_WORKER_DISPATCH=1`
only from the frozen manifest. The callback remains active after observation
closure. Bootstrap requests at take-1, synchronizes that native startup post,
forwards any pending old wait unchanged, then parks at an empty next prewait.
Every admitted dispatch carries its own worker TLS ticket; its next prewait,
after native tail work, retires the generation. No permit is drained or rewritten.

Synthetic threaded tests and successful compilation do not establish native
progress or complete access coverage. Keep the finite discard boundary until
bounded native testing establishes those facts for the pinned image.

Session stop is terminate-only once takeover registration is requested. The
independent stop latch in `gate_player_dispatch_stop.h` arbitrates registration
against `finish` before any legacy cleanup; it survives `stats.active` changes.
Requested/published takeover exits 225 with a terminal reason, preserving resident
callbacks until process death. A legacy stop keeps its prior cleanup behavior,
but later takeover in that process is refused. Neither hook restoration nor
callback unregistration is supported for resident dispatch. Finite completion
still uses exit 223 and retains its existing budgets and deadlines.
