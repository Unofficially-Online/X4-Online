# Target semaphore observer

This is a default-off, bounded diagnostic. It does not admit player ownership,
control a semaphore, authorize a frame, or establish that a native wake belongs
to a particular generation. It must not be used as the persistent-session owner.

`build_linux_semaphore_observer.py` builds a pinned binary and prints its SHA-256
and size. A future frozen runner must verify the full source/binary closure,
pre-open a private append-only regular-file receipt descriptor and provide the same expected digest and
size to the gate using `X4MP_SEMAPHORE_OBSERVER_SHA256` and
`X4MP_SEMAPHORE_OBSERVER_SIZE`. These values come from that verified launcher
receipt, never from provider information. The observer requires explicit
`X4MP_SEMAPHORE_OBSERVER=1` and `X4MP_SEMAPHORE_OBSERVER_FD`; it refuses other
executables and installs before main only while the process is single-threaded.
It checks the full pinned image and four mapped function ranges before modifying
any entry. Partial installation is terminal. The four stubs and pages remain
resident for the process lifetime.

Only semaphore `0x3e59e20` is logged, across constructor, shared post, wait and
timed-wait providers. Rows identify kind, invocation token, thread, caller,
argument, raw return and monotonic timestamp. The existing movement wait hook
continues to forward once; the provider records the resulting shared wait once.
That wrapper sets a thread-local `movement=1` marker only around its wait; the
marker is diagnostic and does not establish a frame ticket. No additional
movement callsite detour is installed.
Original calls on other semaphores are forwarded without rows. Nothing holds a
monitor lock across the original wait.

A capacity overflow writes an INCOMPLETE marker; failed or short writes also set
the incomplete flag. The exported status reports counters and constructor
state. At the final authenticated worker tail, with main parked, the optional
checkpoint closes admission and waits at most one second for already-entered
target calls. Its STATUS is complete only when none remain, every entered call
has a return row, no receipt failure occurred, and the constructor returned.
The lifecycle checkpoint records the same tick and owner TID. New target calls
after the cutoff are outside the ledger, so this is not global quiescence.
A killed process, missing pairs, missing constructor, unobserved
status, overflow, or failed writes cannot support an absence claim. Provider
entry monitoring does not cover inline semaphore writes or branches into the
middle of native functions. The loader ordering must be verified in a separate
controlled run; a missing constructor record means the ledger is incomplete.

The gate adapter validates the independently supplied provider file hash and
size, compares all executable provider segments with that file before calling
its information export, checks all four exported wrapper addresses and every
live entry jump, verifies all full anonymous RX stub pages and relocated
instructions, and finally checks the complete original native range hash. With
the observer disabled it performs the existing original hash check directly.
No ordinary session or game launch is performed by these tools.
