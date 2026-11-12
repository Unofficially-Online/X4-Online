# Recurring main/worker rendezvous

This default-unused, engine-free helper is separate from the existing one-shot
rendezvous. It does not install hooks, write banks, call engine functions, wait,
or resume simulation. Initialize zeroed resident storage once, before sharing;
main and worker IDs must be distinct, stable, and from the same identity domain.
The caller supplies actual thread IDs and the actual game generation. Registrations
and storage remain resident and unchanged for the helper's lifetime.

The atomic state packs the generation and transition flags. Generation 5 is the
first legal arm; every subsequent arm must be exactly its completed predecessor
plus one, up to `FWRR_MAX_GENERATION`. Main arms, main parks, worker arrives,
worker enters once with a token, worker releases once, and main authorizes once.
Main and worker arrivals may happen in either order. Tokens identify the helper,
generation and registered worker thread. No stale token can release a later
frame. Reentry, a wrong thread or generation, an invalid token, and cancellation
poison the helper permanently. Failure and timeout handling must end or discard
the owning experiment; no reset or automatic fallback is provided.
Worker release publishes completion; it does not prove the worker has returned
from its hook. The integration must perform no further protected work after
release and must arrange the next frame so it cannot overlap unfinished work
from the preceding worker hook.

The state CAS establishes the order between a cancellation and authorization:
a successful authorization that linearized first cannot be revoked. Callers
must prevent cancellation from racing with continued simulation. Poison is
published before the diagnostic failure code, so a concurrent observer may see
poison before the code is available; the code is diagnostic, not permission.

The caller owns all deadlines, scheduling, and proof that main is actually
parked and worker is at the intended joined hook. This protocol does not pause
renderer, physics, I/O, or any other native thread, does not prove that every
bank/context reader or writer is excluded, and does not establish that FrameTick
can return while the worker waits. A timeout bounds a deadlock; it does not
prove the schedule live. The integration must preserve original semaphore and
hook behavior, and must not open player scopes on these flags alone.

`test_frame_worker_recurring.sh` uses two real pthreads over 4,000 consecutive
generations, alternates which arrives first, checks cross-thread publication,
and exercises invalid generations, token ownership, reentry, early rearm and
cancellation. It does not launch X4 or validate any native scheduling.
