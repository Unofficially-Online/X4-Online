# Worker dispatch contract (engine-free)

This default-unused contract supplements `frame_worker_recurring.h`. It cannot
prove a native frame owns a particular semaphore wake, and it does not enable any
hook. Use in a native session remains blocked until the bootstrap and all relevant
semaphore users are authenticated.

A future adapter must compose with the existing `lmt_worker_wait` at the pinned
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
