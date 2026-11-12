#ifndef X4_PLAYER_CONTROLLER_CALLBACK_SCOPE_H
#define X4_PLAYER_CONTROLLER_CALLBACK_SCOPE_H

/* Engine-free, default-unused callback policy. The integration supplies a
 * resident policy, proves the callback ABI/pins and owns all scope effects,
 * object lifetimes, reader exclusion and fatal failure handling. The operation
 * callbacks identify the exact owning scope; open/close report success only
 * for completed, verified transitions and retain all uncertainty themselves.
 * Initialize zeroed storage once, then publish it only after initialization
 * using external release/acquire synchronization. The initialized field is a
 * one-time claim, not a publication barrier for the policy's other fields.
 * The report callback must stop/discard the owning process: a void native vtable callback
 * cannot return a failure to X4. Every wrapper must also treat a false dispatch
 * result as fatal even if the report callback unexpectedly returns.
 *
 * Slot 40: void(controller, double); 41: void(controller, void *, void *);
 * 43: void(controller, void *, void *, unsigned char). No engine calls or
 * installation are performed by this file. */
#include <math.h>
#include <stdatomic.h>
#include <stdint.h>

#define PCC_SLOT_UPKEEP 40u
#define PCC_SLOT_ENVIRONMENT 41u
#define PCC_SLOT_CHANGE 43u

typedef enum {
    PCC_SCOPE_ERROR = -1,
    PCC_SCOPE_OUTSIDE = 0,
    PCC_SCOPE_OWN = 1
} pcc_scope_state;

typedef struct {
    void *context;
    uint64_t (*thread)(void *context);
    int (*identity)(void *context, void *controller);
    pcc_scope_state (*scope)(void *context, void *controller);
    int (*open)(void *context, void *controller);
    int (*close)(void *context, void *controller);
    void (*report)(void *context, const char *reason);
} pcc_operations;

typedef struct {
    void (*upkeep)(void *controller, double now);
    void (*environment)(void *controller, void *first, void *second);
    void (*change)(void *controller, void *first, void *second, unsigned char mode);
} pcc_originals;

typedef struct {
    pcc_operations operations;
    pcc_originals originals;
    void *controller;
    uint64_t owner_thread;
    _Atomic unsigned initialized;
    _Atomic unsigned failure;
    _Atomic uint64_t calls[3];
} pcc_policy;

typedef struct pcc_frame {
    pcc_policy *policy;
    struct pcc_frame *parent;
    unsigned slot;
    unsigned opened;
    unsigned ready;
} pcc_frame;
static _Thread_local pcc_frame *pcc_active;

static inline int pcc_initialize(pcc_policy *policy, const pcc_operations *operations,
                                 const pcc_originals *originals, void *controller,
                                 uint64_t owner_thread) {
    if (!policy || !operations || !originals || !controller || !owner_thread ||
        !operations->thread || !operations->identity || !operations->scope ||
        !operations->open || !operations->close || !operations->report ||
        !originals->upkeep || !originals->environment || !originals->change ||
        operations->thread(operations->context) != owner_thread ||
        !operations->identity(operations->context, controller)) return 0;
    /* Policy storage must initially be zero and remain resident forever. */
    unsigned empty = 0;
    if (!atomic_compare_exchange_strong(&policy->initialized, &empty, 1)) return 0;
    policy->operations = *operations;
    policy->originals = *originals;
    policy->controller = controller;
    policy->owner_thread = owner_thread;
    atomic_init(&policy->failure, 0);
    for (unsigned i = 0; i < 3; ++i) atomic_init(&policy->calls[i], 0);
    return 1;
}

static inline int pcc_fail(pcc_policy *policy, const char *reason) {
    if (policy && atomic_load_explicit(&policy->initialized, memory_order_acquire)) {
        atomic_store_explicit(&policy->failure, 1, memory_order_release);
        policy->operations.report(policy->operations.context, reason);
    }
    return 0;
}

static inline int pcc_enter(pcc_policy *policy, void *controller, unsigned slot,
                            pcc_frame *frame) {
    if (!policy || !frame ||
        !atomic_load_explicit(&policy->initialized, memory_order_acquire)) return 0;
    pcc_operations *ops = &policy->operations;
    if (atomic_load_explicit(&policy->failure, memory_order_acquire))
        return pcc_fail(policy, "already_failed");
    if (ops->thread(ops->context) != policy->owner_thread)
        return pcc_fail(policy, "wrong_thread");
    if (!controller || controller != policy->controller || !ops->identity(ops->context, controller))
        return pcc_fail(policy, "wrong_controller");
    frame->policy = policy;
    frame->parent = pcc_active;
    frame->slot = slot;
    frame->opened = frame->ready = 0;
    if (frame->parent && (frame->parent->policy != policy || !frame->parent->ready ||
                          frame->parent->slot != PCC_SLOT_CHANGE || slot != PCC_SLOT_ENVIRONMENT))
        return pcc_fail(policy, "nested_callback");
    pcc_active = frame; /* install before scope operations, to reject reentry */
    pcc_scope_state current = ops->scope(ops->context, controller);
    if (current == PCC_SCOPE_OUTSIDE && !frame->parent) {
        if (!ops->open(ops->context, controller)) {
            pcc_active = frame->parent;
            return pcc_fail(policy, "scope_open");
        }
        frame->opened = 1;
        current = ops->scope(ops->context, controller);
    }
    if (current != PCC_SCOPE_OWN || atomic_load_explicit(&policy->failure, memory_order_acquire)) {
        if (frame->opened) (void)ops->close(ops->context, controller);
        pcc_active = frame->parent;
        return pcc_fail(policy, "scope_not_owned");
    }
    frame->ready = 1;
    unsigned index = slot == PCC_SLOT_UPKEEP ? 0 : slot == PCC_SLOT_ENVIRONMENT ? 1 : 2;
    uint64_t count = atomic_load_explicit(&policy->calls[index], memory_order_relaxed);
    do {
        if (count == UINT64_MAX) {
            if (frame->opened) (void)ops->close(ops->context, controller);
            pcc_active = frame->parent;
            return pcc_fail(policy, "counter_overflow");
        }
    } while (!atomic_compare_exchange_weak_explicit(&policy->calls[index], &count, count + 1,
                                                     memory_order_relaxed, memory_order_relaxed));
    return 1;
}

static inline int pcc_leave(pcc_frame *frame) {
    pcc_policy *policy = frame->policy;
    pcc_operations *ops = &policy->operations;
    if (ops->thread(ops->context) != policy->owner_thread ||
        !ops->identity(ops->context, policy->controller)) {
        /* The backend retains any open-scope obligation. No cleanup call may
         * dereference a controller whose identity is no longer established. */
        frame->ready = 0;
        pcc_active = frame->parent;
        return pcc_fail(policy, "post_callback_identity");
    }
    int ok = pcc_active == frame && frame->ready &&
             ops->scope(ops->context, policy->controller) == PCC_SCOPE_OWN;
    frame->ready = 0;
    if (frame->opened) {
        if (!ops->close(ops->context, policy->controller) ||
            ops->scope(ops->context, policy->controller) != PCC_SCOPE_OUTSIDE) ok = 0;
    }
    pcc_active = frame->parent;
    if (!ok) return pcc_fail(policy, "scope_close");
    if (atomic_load_explicit(&policy->failure, memory_order_acquire)) return 0;
    return 1;
}

static inline int pcc_upkeep(pcc_policy *policy, void *controller, double now) {
    if (!policy) return 0;
    if (!isfinite(now)) return pcc_fail(policy, "invalid_time");
    pcc_frame frame;
    if (!pcc_enter(policy, controller, PCC_SLOT_UPKEEP, &frame)) return 0;
    policy->originals.upkeep(controller, now);
    return pcc_leave(&frame);
}
static inline int pcc_environment(pcc_policy *policy, void *controller, void *first, void *second) {
    pcc_frame frame;
    if (!pcc_enter(policy, controller, PCC_SLOT_ENVIRONMENT, &frame)) return 0;
    policy->originals.environment(controller, first, second);
    return pcc_leave(&frame);
}
static inline int pcc_change(pcc_policy *policy, void *controller, void *first, void *second,
                             unsigned char mode) {
    pcc_frame frame;
    if (!pcc_enter(policy, controller, PCC_SLOT_CHANGE, &frame)) return 0;
    policy->originals.change(controller, first, second, mode);
    return pcc_leave(&frame);
}
#endif
