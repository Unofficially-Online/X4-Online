# Player-controller callback scope policy

This unused, engine-free helper specifies the scope decision for one authenticated
P2 controller. It installs no vtable, hook, scheduler, bank exchange, or references.
The embedding keeps the policy and callback storage resident, initializes the zeroed
policy only once, publishes it after initialization with external release/acquire
synchronization, and supplies the exact worker
thread, controller identity and native function pointers. The operation callbacks
must identify the same owned scope and retain any uncertain open/close effects.
A successful close means complete verified cleanup. Every report is fatal to the
experiment: native virtuals return void, so the embedding must not return to X4 if
report is called or a dispatch returns zero. A synthetic report that returns exists
only to test the failure state.

An already-owned scope is reused. Otherwise, on the authorized thread, the helper
opens once and closes once. The one allowed nested callback is slot 43 to slot 41
for the same policy and controller; it reuses the existing scope and does not
exchange globals a second time. Different controllers, threads, or nesting fail;
there is no deferred use of caller-owned pointers. The helper calls an admitted
native callback once and counts it. Failure is sticky, including a failed close.
Counter overflow and a non-finite slot-40 time are refused.
After the native callback, the helper rechecks the owner thread and controller
identity. If either is lost, it leaves any open-scope obligation to the backend
and reports a fatal error without calling cleanup through an untrusted identity.
A borrowed scope must still be owned. For a scope opened here, successful close
must additionally be followed by an OUTSIDE scope observation. Failure remains
sticky even if a callback reports a successful close but leaves the scope open.
The policy's `initialized` field only claims initialization; seeing it set does
not publish the remaining fields to another thread.

For Linux X4 9.00 ELF SHA256
`113f73d74570084f6ce18a659b365009eccf579fb66dbabb5dad2b1718045f20`, the checked
mappings are:

| Slot | Native address | FDE size | SHA256 | ABI |
| --- | --- | --- | --- | --- |
| 40 | `0x1672370` | 7095 | `8d672ea7553c9bb4939fb173d8c86494f8eeb92068aa6dd14984e357d9d0c8d9` | `void(controller, double)` |
| 41 | `0x1670b30` | 5631 | `4b281d0f777cc0e58a4433eb4349e798291e2f205318a6b6e1254822deececf5` | `void(controller, void *, void *)` |
| 43 | `0x1653840` | 103 | `b03e61281173d463aecda5490564192b324c075be6e270e0df3392b5c6d558df` | `void(controller, void *, void *, unsigned char)` |

Slot 43 calls its controller's slot 41 at `0x1653893` on the nonzero-mode
branch. The controller at ship `+0x200` is dispatched through slot 43 at
`0x131a095`; this establishes a possible path, not its execution thread. Other
polymorphic callers and the thread provenance of slots 40 and 41 are not
established by these pins. Slot 40 accesses the shared input bank; slot 41 sets
and later clears the global reentry byte; slot 43 can change environment fields
and the reference-counted global zone. A successful helper test does not prove
that unrelated native readers or writers are excluded, that a bank scope is
safe, or that any Linux game frame can persist after the experiment.

Run `src/native/engine/linux/test_player_controller_callback_scope.sh` for the
engine-free checks. The test uses stand-in callbacks and thread/scope operations;
it does not launch X4 or establish native gameplay behavior.
