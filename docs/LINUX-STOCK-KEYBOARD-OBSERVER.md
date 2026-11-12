# Native Linux stock keyboard observer core

`src/native/engine/linux/stock_keyboard_observer.h` observes selected native
keyboard/state dispatches and forwards every wrapped call to its original
handler exactly once. This is diagnostic instrumentation, not input replay or
permission to steer, and does not establish multiplayer readiness.

## Tree refresh and ownership

Before validation and every handler compare-and-swap, the core takes a fresh
complete census of all three dispatch trees. The existing owner must match.
Readable bounds, parent links, key ordering, repeated node addresses/cycles,
and the 4096-node total limit remain mandatory for the entire census.

Unselected nodes may be inserted, removed, or rebalanced, including changes to
selected nodes' parents or traversal positions. Unselected handlers are not
owned and are left untouched. An unselected node pointing to either observer
wrapper is rejected because that reachable wrapper has no ownership entry.
The initial `baseline` and `count` remain diagnostic data, not refresh acceptance
criteria.

All 15 selected kind/key identities must occur exactly once at their original
selected node addresses. Their handlers must match the original/wrapper state
allowed by the ownership ledger, including uncertain writes and pending
readback. A selected node's removal, replacement, duplicate, or foreign handler
fails closed. The complete census must pass before a handler address is
returned; reads and writes use that fresh census address, never a dereferenced
cached node address.

Failed validation disables capture. Failed cleanup retains outstanding owned,
uncertain, and pending-readback obligations; it cannot report restoration just
because the tree changed. A successful write still needs fresh validation and
readback. Retrying cleanup can finish after a transient read failure without
writing an already restored handler again.

The caller must serialize management operations and establish the required
quiescence and object lifetime. A census and pointer CAS alone do not make
concurrent removal safe. Reuse of the same address with the same selected
identity and handler (ABA) is not detected.

## Focused offline verification

Run `python3 -m unittest tests.test_linux_stock_keyboard_observer -v`. Its C
fixture covers valid unrelated growth/removal/rebalancing, selected identity
changes, structural failures, foreign handlers, rollback, uncertain writes,
readback retry, and forwarding behavior. These synthetic checks are not evidence
that the native engine supplies the required quiescence or lifetime guarantees.
