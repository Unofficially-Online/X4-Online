#ifndef X4_LINUX_PLAYER_CONTROL_BANKS_H
#define X4_LINUX_PLAYER_CONTROL_BANKS_H

/* Engine-free keyboard bank transaction, not a native adapter or a player port.
 * Exactly one resident scope may manage a bank domain. The adapter must prove
 * pinned image/identity, admitted worker/thread, joined workers and exclusive
 * bank access for every operation, including all read/CAS calls and cleanup.
 * Native Update may change values only between successful enter() and exit().
 * The adapter must reject autopilot/non-keyboard state and prove lifetimes of
 * any opaque embedded state; copying bytes does not acquire component references.
 * Initialize scope and slots from zero once; never reset pending obligations.
 * Shared guards (reentry3ee9b40, input locks3e5b298..29a, buffer index3d8e254)
 * and the zone pointer3e4cdd8 are NEVER part of a value read/write descriptor.
 * boundary() proves phase/exclusion; guards() checks shared guard policy.
 * Neither callback is replaced by a caller-supplied thread number alone.
 *
 * ZONE CONTRACT: context owns a persistent external reference transaction keyed
 * by the supplied slot address. Zone callbacks may change references/the live
 * zone pointer only, never the value regions below.
 * Before zone_enter it owns the slot's retained zone. zone_enter must preserve
 * an independently owned canonical-zone reference BEFORE any replacement and
 * record every uncertain reference effect. ACTIVE means the live zone is the
 * slot's and both canonical/slot ownership obligations remain represented.
 * UNCERTAIN means that external ledger is pending; INACTIVE guarantees no effect.
 * zone_restore is retryable ONLY through that ledger. It returns1 only after
 * canonical live ownership is restored, updated slot ownership is retained and
 * all temporary reference obligations are resolved. This core never retains,
 * releases, copies or infers ownership of a component pointer. The external
 * ledger/context must outlive pending cleanup; no dummy adapter is safe.
 *
 * CAS callback changes exactly one byte iff it equals expected. CHANGED
 * guarantees that replacement completed normally. NOT_CHANGED
 * guarantees no write; UNKNOWN permits only that byte's possible replacement.
 * readback resolves a possible byte effect under the exclusive boundary. A
 * different foreign byte is never overwritten. Equal-value ABA cannot be
 * detected by bytes: preventing it is part of adapter exclusion, not a promise
 * made by this core. Do not reinitialize/reuse a pending scope or its slot.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define PCB_MAIN_BYTES 0x278u
#define PCB_VALUE_BYTES 0x3e4u
#define PCB_REGION_COUNT 5u

typedef enum { PCB_SIM, PCB_DEVICE, PCB_LOCAL } pcb_class;
typedef struct {
    unsigned offset;
    unsigned length;
    pcb_class classification;
} pcb_main_slice;

/* Exact native reset footprint. Cutpoints/classes are Windows-paired with
 * Linux access evidence; opaque/padding bytes are not individually typed.
 * In particular DEVICE classification does NOT authorize blind memset. */
static const pcb_main_slice pcb_main_slices[] = {
    {0x000, 0x030, PCB_DEVICE},
    {0x030, 0x020, PCB_SIM},
    {0x050, 0x014, PCB_DEVICE},
    {0x064, 0x001, PCB_SIM},
    {0x065, 0x001, PCB_DEVICE},
    {0x066, 0x001, PCB_SIM},
    {0x067, 0x001, PCB_DEVICE},
    {0x068, 0x00c, PCB_SIM},
    {0x074, 0x03c, PCB_DEVICE},
    {0x0b0, 0x010, PCB_SIM},
    {0x0c0, 0x008, PCB_LOCAL},
    {0x0c8, 0x040, PCB_DEVICE},
    {0x108, 0x001, PCB_SIM},
    {0x109, 0x001, PCB_LOCAL},
    {0x10a, 0x16e, PCB_SIM},
};

typedef struct {
    unsigned va;                     /* Metadata only; never dereferenced here. */
    unsigned offset;
    unsigned length;
    const char *name;
} pcb_region;

static const pcb_region pcb_regions[PCB_REGION_COUNT] = {
    {0x3d8d9a0, 0x000, 0x278, "main"},
    /* Whole native output object, including Windows-labelled CONFIG tables:
     * helper1e04550 writes0x140 bytes, not just the SIM prefix0x54.
     * Remaining bytes' SIM/config invariance is not yet established. */
    {0x3e5afa0, 0x278, 0x140, "coefficient_output"},
    {0x3e5b0e0, 0x3b8, 0x00c, "bind"},
    {0x3d8e370, 0x3c4, 0x008, "hold_timer"},
    /* Known flags0..2 and floats4..13; opaque padding preserved. Zone separate. */
    {0x3e4cdc0, 0x3cc, 0x018, "environment"},
};

typedef struct pcb_slot pcb_slot;

typedef enum { PCB_NOT_CHANGED, PCB_CHANGED, PCB_UNKNOWN } pcb_write_result;
typedef enum { PCB_ZONE_INACTIVE, PCB_ZONE_ACTIVE, PCB_ZONE_UNCERTAIN } pcb_zone_result;
typedef struct {
    void *context;
    uint64_t (*thread)(void *context);
    int (*boundary)(void *context);
    int (*guards)(void *context);
    int (*read)(void *context, unsigned region, unsigned offset,
                unsigned char *bytes, unsigned length);
    pcb_write_result (*replace_byte)(void *context, unsigned region, unsigned offset,
                                     unsigned char expected, unsigned char replacement);
    pcb_zone_result (*zone_enter)(void *context, const pcb_slot *slot);
    int (*zone_restore)(void *context, const pcb_slot *slot);
} pcb_operations;

struct pcb_slot {
    unsigned initialized;
    unsigned char bytes[PCB_VALUE_BYTES];
};

typedef struct {
    /* Explicit reviewed DEVICE bytes to initialize to zero. Unmarked bytes,
     * including opaque padding/timers, retain canonical bytes. This policy is
     * not proof that unsupported held inputs are neutral: adapter must validate
     * its entire restricted keyboard context before allowing native Update. */
    unsigned char neutral_device[PCB_MAIN_BYTES];
    unsigned char enabled;
    unsigned char contexts;
    uint32_t deactivation_count;
} pcb_initial_policy;

typedef enum { PCB_IDLE, PCB_ENTERING, PCB_ACTIVE, PCB_RESTORING } pcb_phase;
typedef enum { PCB_CLEAR, PCB_OWNED, PCB_UNCERTAIN, PCB_FOREIGN } pcb_byte_state;
typedef struct {
    unsigned initialized;
    pcb_phase phase;
    uint64_t owner_thread;
    pcb_operations operations;
    pcb_slot *slot;
    unsigned zone_pending;
    const char *reason;
    const char *failure_reason;       /* First failure in the current transaction. */
    unsigned char canonical[PCB_VALUE_BYTES];
    unsigned char expected[PCB_VALUE_BYTES];
    unsigned char pending[PCB_VALUE_BYTES];
} pcb_scope;

static inline int pcb_device_byte(unsigned offset) {
    for (unsigned slice_index = 0;
         slice_index < sizeof(pcb_main_slices) / sizeof(pcb_main_slices[0]);
         ++slice_index) {
        const pcb_main_slice *slice = &pcb_main_slices[slice_index];
        if (offset >= slice->offset && offset - slice->offset < slice->length) {
            return slice->classification == PCB_DEVICE;
        }
    }
    return 0;
}

/* Pure initialization: SIM and opaque bytes copied, reviewed DEVICE fields
 * neutral, LOCAL fields explicit; no engine references are acquired here.
 * Caller supplies a fresh zero-initialized slot and a complete canonical read.
 * A slot must not be shared by two scopes or changed while a scope owns it. */
static inline int pcb_slot_initialize(pcb_slot *slot,
                                      const unsigned char canonical[PCB_VALUE_BYTES],
                                      const pcb_initial_policy *policy) {
    if (!slot || slot->initialized || !canonical || !policy ||
        policy->enabled > 1 || policy->contexts > 1) {
        return 0;
    }
    for (unsigned byte_index = 0; byte_index < PCB_MAIN_BYTES; ++byte_index) {
        if (policy->neutral_device[byte_index] > 1 ||
            (policy->neutral_device[byte_index] && !pcb_device_byte(byte_index))) {
            return 0;
        }
    }
    memcpy(slot->bytes, canonical, PCB_VALUE_BYTES);
    for (unsigned byte_index = 0; byte_index < PCB_MAIN_BYTES; ++byte_index) {
        if (policy->neutral_device[byte_index]) {
            slot->bytes[byte_index] = 0;
        }
    }
    slot->bytes[0xc0] = policy->enabled;
    /* Pin is x86 little endian; host-independent fixture initialization. */
    for (unsigned byte_index = 0; byte_index < 4; ++byte_index) {
        slot->bytes[0xc4 + byte_index] =
            (unsigned char)(policy->deactivation_count >> (8 * byte_index));
    }
    slot->bytes[0x109] = policy->contexts;
    /* Separately evidenced DEVICE double; canonical opaque bytes stay copied. */
    memset(slot->bytes + 0x3c4, 0, 8);
    slot->initialized = 1;
    return 1;
}

static inline int pcb_scope_initialize(pcb_scope *scope, const pcb_operations *operations,
                                       uint64_t owner_thread) {
    if (!scope || scope->initialized || !operations || !owner_thread ||
        !operations->thread || !operations->boundary || !operations->guards ||
        !operations->read || !operations->replace_byte || !operations->zone_enter ||
        !operations->zone_restore) {
        return 0;
    }
    scope->operations = *operations;
    scope->owner_thread = owner_thread;
    scope->initialized = 1;
    scope->reason = "idle";
    return 1;
}

static inline int pcb_boundary(pcb_scope *scope) {
    pcb_operations *operations = &scope->operations;
    if (operations->thread(operations->context) != scope->owner_thread) {
        scope->reason = "wrong_thread";
        return 0;
    }
    if (!operations->boundary(operations->context)) {
        scope->reason = "boundary";
        return 0;
    }
    if (!operations->guards(operations->context)) {
        scope->reason = "shared_guard";
        return 0;
    }
    return 1;
}

static inline int pcb_read_values(pcb_scope *scope, unsigned char *bytes) {
    for (unsigned region_index = 0; region_index < PCB_REGION_COUNT; ++region_index) {
        const pcb_region *region = &pcb_regions[region_index];
        if (!scope->operations.read(scope->operations.context, region_index, 0,
                                    bytes + region->offset, region->length)) {
            scope->reason = "read";
            return 0;
        }
    }
    return 1;
}

/* Retry cleanup only; never captures a new slot or treats foreign bytes as ours.
 * UNKNOWN byte effects are resolved by exact readback before any further CAS.
 * A foreign byte stays foreign until its original canonical value is observed.
 * Return1 means every canonical value byte AND external zone are verified clean.
 * Caller must stop simulation while any obligation remains; do not resume a
 * partly restored bank, nor run new slot operations after a failed boundary. */
static inline int pcb_restore(pcb_scope *scope) {
    if (!scope || !scope->initialized || scope->phase != PCB_RESTORING ||
        !pcb_boundary(scope)) {
        return 0;
    }
    int clean = 1;
    for (unsigned region_index = 0; region_index < PCB_REGION_COUNT; ++region_index) {
        const pcb_region *region = &pcb_regions[region_index];
        for (unsigned byte_index = 0; byte_index < region->length; ++byte_index) {
            unsigned flat_index = region->offset + byte_index;
            unsigned char current;
            if (!scope->operations.read(scope->operations.context, region_index,
                                         byte_index, &current, 1)) {
                clean = 0;
                continue;
            }
            if (current == scope->canonical[flat_index]) {
                scope->pending[flat_index] = PCB_CLEAR;
                continue;
            }
            if ((scope->pending[flat_index] != PCB_OWNED &&
                 scope->pending[flat_index] != PCB_UNCERTAIN) ||
                current != scope->expected[flat_index]) {
                scope->pending[flat_index] = PCB_FOREIGN;
                clean = 0;
                continue;
            }
            scope->pending[flat_index] = PCB_UNCERTAIN;
            pcb_write_result result = scope->operations.replace_byte(
                scope->operations.context, region_index, byte_index,
                scope->expected[flat_index], scope->canonical[flat_index]);
            if (result == PCB_NOT_CHANGED) {
                /* Expected bytes may have been replaced by a foreign writer.
                 * Do not infer an effect or discard the prior obligation. */
                scope->pending[flat_index] = PCB_OWNED;
            }
            if (!scope->operations.read(scope->operations.context, region_index,
                                         byte_index, &current, 1)) {
                clean = 0;
                continue;
            }
            if (current == scope->canonical[flat_index]) {
                scope->pending[flat_index] = PCB_CLEAR;
            } else {
                if (current != scope->expected[flat_index]) {
                    scope->pending[flat_index] = PCB_FOREIGN;
                }
                clean = 0;
            }
        }
    }
    if (scope->zone_pending) {
        if (scope->operations.zone_restore(scope->operations.context, scope->slot)) {
            scope->zone_pending = 0;
        } else {
            clean = 0;
        }
    }
    /* Audit again after all writes and the external zone transaction. A later
     * callback must not let an earlier byte escape the canonical check. */
    unsigned char verified[PCB_VALUE_BYTES];
    if (!pcb_read_values(scope, verified)) {
        clean = 0;
    } else {
        for (unsigned byte_index = 0; byte_index < PCB_VALUE_BYTES; ++byte_index) {
            if (verified[byte_index] != scope->canonical[byte_index]) {
                if (scope->pending[byte_index] == PCB_CLEAR) {
                    scope->pending[byte_index] = PCB_FOREIGN;
                }
                clean = 0;
            }
        }
    }
    if (!clean) {
        if (!scope->failure_reason) {
            scope->failure_reason = "restore_pending";
        }
        scope->reason = "restore_pending";
        return 0;
    }
    scope->phase = PCB_IDLE;
    scope->slot = NULL;
    scope->reason = "restored";
    return 1;
}

static inline int pcb_enter(pcb_scope *scope, pcb_slot *slot) {
    if (!scope || !scope->initialized || scope->phase != PCB_IDLE ||
        !slot || !slot->initialized || !pcb_boundary(scope)) {
        return 0;
    }
    if (!pcb_read_values(scope, scope->canonical)) {
        return 0;
    }
    scope->failure_reason = NULL;
    memcpy(scope->expected, slot->bytes, PCB_VALUE_BYTES);
    memset(scope->pending, PCB_CLEAR, sizeof(scope->pending));
    scope->slot = slot;
    scope->phase = PCB_ENTERING;
    /* Set before external entry: uncertain reference effects must survive. */
    scope->zone_pending = 1;
    pcb_zone_result zone = scope->operations.zone_enter(scope->operations.context, slot);
    if (zone != PCB_ZONE_ACTIVE) {
        scope->failure_reason = zone == PCB_ZONE_INACTIVE ? "zone_refused" : "zone_uncertain";
        if (zone == PCB_ZONE_INACTIVE) {
            scope->zone_pending = 0;
        }
        scope->phase = PCB_RESTORING;
        pcb_restore(scope);
        return 0;
    }
    for (unsigned region_index = 0; region_index < PCB_REGION_COUNT; ++region_index) {
        const pcb_region *region = &pcb_regions[region_index];
        for (unsigned byte_index = 0; byte_index < region->length; ++byte_index) {
            unsigned flat_index = region->offset + byte_index;
            if (scope->canonical[flat_index] == scope->expected[flat_index]) {
                continue;
            }
            scope->pending[flat_index] = PCB_UNCERTAIN;
            pcb_write_result result = scope->operations.replace_byte(
                scope->operations.context, region_index, byte_index,
                scope->canonical[flat_index], scope->expected[flat_index]);
            if (result != PCB_CHANGED) {
                scope->failure_reason = result == PCB_NOT_CHANGED ? "write_refused" : "write_uncertain";
                if (result == PCB_NOT_CHANGED) {
                    scope->pending[flat_index] = PCB_FOREIGN;
                }
                scope->phase = PCB_RESTORING;
                pcb_restore(scope);
                return 0;
            }
            scope->pending[flat_index] = PCB_OWNED;
        }
    }
    scope->phase = PCB_ACTIVE;
    scope->reason = "active";
    return 1;
}

/* Capture is all-or-nothing. A failed read leaves ACTIVE with the original slot
 * untouched and requires exit retry at a valid boundary, not direct restore:
 * native Update is allowed to have changed every byte while ACTIVE. */
static inline int pcb_exit(pcb_scope *scope) {
    if (!scope || !scope->initialized || scope->phase != PCB_ACTIVE ||
        !pcb_boundary(scope)) {
        return 0;
    }
    unsigned char captured[PCB_VALUE_BYTES];
    if (!pcb_read_values(scope, captured)) {
        if (!scope->failure_reason) {
            scope->failure_reason = "capture_read";
        }
        return 0;
    }
    memcpy(scope->slot->bytes, captured, PCB_VALUE_BYTES);
    memcpy(scope->expected, captured, PCB_VALUE_BYTES);
    memset(scope->pending, PCB_OWNED, sizeof(scope->pending));
    scope->phase = PCB_RESTORING;
    return pcb_restore(scope);
}

#endif
