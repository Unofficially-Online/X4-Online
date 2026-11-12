#ifndef X4_STOCK_KEYBOARD_OBSERVER_H
#define X4_STOCK_KEYBOARD_OBSERVER_H

/* Selected native handler ownership. The default observer records and forwards
 * each event once. A separately enabled replay experiment may register a
 * consumer before installation; it then owns the decision to suppress an event.
 * The owner must keep this translation unit and the callbacks alive for the
 * process lifetime, including after failed cleanup and delayed wrapper calls. */
#include <errno.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define SKO_TREE_LIMIT 4096u
#define SKO_EVENT_LIMIT 1024u
#define SKO_SELECTED 15u
#define SKO_OWNER_VA 0x3daaaf8u
#define SKO_ACTION_VA 0x166c2f0u
#define SKO_STATE_VA 0x166c6f0u

typedef enum { SKO_CAS_APPLIED, SKO_CAS_CHANGED, SKO_CAS_REFUSED, SKO_CAS_UNKNOWN } sko_cas_result;

typedef struct {
    void *context;
    int (*read)(void *, uintptr_t, void *, size_t);
    sko_cas_result (*compare_exchange)(void *, uintptr_t, uintptr_t, uintptr_t);
    int (*verify_pins)(void *);
    uint64_t (*thread_id)(void *);
    void (*call_action)(void *, uintptr_t, int);
    void (*call_state)(void *, uintptr_t, int, unsigned char);
} sko_platform;

typedef struct {
    uint64_t thread;
    uintptr_t caller;
    int id;
    unsigned char kind;
    unsigned char state;
} sko_event;

typedef struct {
    uintptr_t address;
    uintptr_t parent;
    uintptr_t left;
    uintptr_t right;
    uintptr_t handler;
    int key;
    int64_t lower;
    int64_t upper;
    unsigned char kind;
} sko_node;

typedef struct {
    sko_platform platform;
    uintptr_t owner_pointer;
    uintptr_t owner;
    uintptr_t action_original;
    uintptr_t state_original;
    /* Initial census for diagnostics only; never a refresh topology contract. */
    sko_node baseline[SKO_TREE_LIMIT];
    unsigned count;
    uintptr_t selected[SKO_SELECTED]; /* identity only; never read through this cache */
    unsigned owned;
    unsigned uncertain;
    unsigned pending_readback;
    atomic_int active;
    atomic_flag event_lock;
    atomic_uint overflow;
    atomic_uint wrapper_depth;
    unsigned event_count;
    sko_event events[SKO_EVENT_LIMIT];
    int (*consume_event)(void *context, const sko_event *event);
} sko_observer;

/* One static session: wrappers must remain callable even after STOP. */
static sko_observer sko_session = {.event_lock = ATOMIC_FLAG_INIT};
static const int sko_state_ids[14] = {2, 3, 4, 5, 6, 7, 8, 9, 13, 14, 15, 16, 18, 19};

static int sko_index(unsigned kind, int id) {
    if (kind == 0 && id == 0x7b) {
        return 0;
    }
    if (kind == 1) {
        for (unsigned state_index = 0; state_index < 14; ++state_index) {
            if (sko_state_ids[state_index] == id) {
                return (int)state_index + 1;
            }
        }
    }
    return -1;
}

static void sko_record(unsigned kind, int id, unsigned char state, uintptr_t caller) {
    sko_observer *observer = &sko_session;
    if (!atomic_load_explicit(&observer->active, memory_order_acquire)) {
        return;
    }
    if (atomic_flag_test_and_set_explicit(&observer->event_lock, memory_order_acquire)) {
        atomic_fetch_add(&observer->overflow, 1);
        return;
    }
    if (observer->event_count == SKO_EVENT_LIMIT) {
        atomic_fetch_add(&observer->overflow, 1);
    } else {
        sko_event *event = &observer->events[observer->event_count++];
        event->thread = observer->platform.thread_id(observer->platform.context);
        event->caller = caller;
        event->kind = (unsigned char)kind;
        event->id = id;
        event->state = state;
    }
    atomic_flag_clear_explicit(&observer->event_lock, memory_order_release);
}

static int sko_consume(unsigned kind, int id, unsigned char state, uintptr_t caller) {
    if (!sko_session.consume_event) {
        return 0;
    }
    sko_event event = {
        sko_session.platform.thread_id(sko_session.platform.context),
        caller,
        id,
        (unsigned char)kind,
        state
    };
    return sko_session.consume_event(sko_session.platform.context, &event);
}

static void sko_action_wrapper(int id) {
    atomic_fetch_add(&sko_session.wrapper_depth, 1);
    int saved_errno = errno;
    uintptr_t caller = (uintptr_t)__builtin_return_address(0);
    sko_record(0, id, 0, caller);
    int consumed = sko_consume(0, id, 0, caller);
    errno = saved_errno;
    if (!consumed) {
        sko_session.platform.call_action(sko_session.platform.context, sko_session.action_original, id);
    }
    saved_errno = errno;
    atomic_fetch_sub(&sko_session.wrapper_depth, 1);
    errno = saved_errno;
}

static void sko_state_wrapper(int id, unsigned char state) {
    atomic_fetch_add(&sko_session.wrapper_depth, 1);
    int saved_errno = errno;
    uintptr_t caller = (uintptr_t)__builtin_return_address(0);
    sko_record(1, id, state, caller);
    int consumed = sko_consume(1, id, state, caller);
    errno = saved_errno;
    if (!consumed) {
        sko_session.platform.call_state(sko_session.platform.context, sko_session.state_original, id, state);
    }
    saved_errno = errno;
    atomic_fetch_sub(&sko_session.wrapper_depth, 1);
    errno = saved_errno;
}

static uintptr_t sko_wrapper(unsigned kind) {
    return kind == 0 ? (uintptr_t)&sko_action_wrapper : (uintptr_t)&sko_state_wrapper;
}

static int sko_read(uintptr_t address, void *result, size_t length) {
    return address <= UINTPTR_MAX - length &&
           sko_session.platform.read(sko_session.platform.context, address, result, length);
}

/* Full traversal checks every parent, duplicate/cyclic address, and readable field.
 * Only a newly discovered address may be passed to a later read or CAS. */
static int sko_census(sko_node *nodes, unsigned *count, uintptr_t *owner_out) {
    uintptr_t owner;
    unsigned node_count = 0;
    if (!sko_read(sko_session.owner_pointer, &owner, sizeof(owner)) || !owner ||
        owner > UINTPTR_MAX - 0x64b0) {
        return 0;
    }
    for (unsigned kind = 0; kind < 3; ++kind) {
        uintptr_t header = owner + 0x63f0 + kind * 0x40;
        uintptr_t root;
        if (!sko_read(header + 8, &root, sizeof(root))) {
            return 0;
        }
        if (!root) {
            continue;
        }
        unsigned first_node = node_count;
        if (node_count == SKO_TREE_LIMIT) {
            return 0;
        }
        nodes[node_count++] = (sko_node){.address = root,
                                         .parent = header,
                                         .kind = (unsigned char)kind,
                                         .lower = INT64_MIN,
                                         .upper = INT64_MAX};
        for (unsigned node_index = first_node; node_index < node_count; ++node_index) {
            sko_node *node = &nodes[node_index];
            uintptr_t actual_parent;
            uintptr_t children[2];
            if (node->address > UINTPTR_MAX - 0x30 || (node->address & (sizeof(uintptr_t) - 1)) ||
                node->address == header ||
                !sko_read(node->address, &actual_parent, sizeof(actual_parent)) ||
                actual_parent != node->parent ||
                !sko_read(node->address + 8, children, sizeof(children)) ||
                !sko_read(node->address + 0x20, &node->key, sizeof(node->key)) ||
                !sko_read(node->address + 0x28, &node->handler, sizeof(node->handler)) ||
                node->key < node->lower || node->key > node->upper) {
                return 0;
            }
            node->left = children[0];
            node->right = children[1];
            for (unsigned child_index = 0; child_index < 2; ++child_index) {
                if (!children[child_index]) {
                    continue;
                }
                for (unsigned seen_index = 0; seen_index < node_count; ++seen_index) {
                    if (nodes[seen_index].address == children[child_index]) {
                        return 0;
                    }
                }
                if (node_count == SKO_TREE_LIMIT) {
                    return 0;
                }
                nodes[node_count++] = (sko_node){.address = children[child_index],
                                                 .parent = node->address,
                                                 .kind = (unsigned char)kind,
                                                 .lower = child_index ? node->key : node->lower,
                                                 .upper = child_index ? node->upper : node->key};
            }
        }
    }
    *count = node_count;
    *owner_out = owner;
    return 1;
}

/* Scratch storage is static; callers must serialize all management operations. */
static sko_node sko_scratch[SKO_TREE_LIMIT];
static int sko_initialized;
static int sko_prepared;

/* Unselected registrations may change between serialized management boundaries.
 * Every operation still requires a fresh, complete, structurally valid census.
 * Selected identities must remain unique at their original addresses, with the
 * handlers allowed by our ownership ledger. Cached selected addresses are only
 * identity comparisons: all reads and CAS destinations come from this census.
 * This does not detect ABA reuse of an address with the same identity/handler,
 * nor replace the caller's quiescence and object-lifetime obligations. */
static int sko_refresh(unsigned target, uintptr_t *handler_address) {
    uintptr_t owner;
    unsigned count;
    if (target >= SKO_SELECTED || !sko_census(sko_scratch, &count, &owner) ||
        owner != sko_session.owner) {
        return 0;
    }
    unsigned seen = 0;
    uintptr_t fresh_handler_address = 0;
    for (unsigned node_index = 0; node_index < count; ++node_index) {
        sko_node *current_node = &sko_scratch[node_index];
        int selected_index = sko_index(current_node->kind, current_node->key);
        if (selected_index < 0) {
            /* A reachable wrapper outside our ledger cannot be reported clean. */
            if (current_node->handler == sko_wrapper(0) ||
                current_node->handler == sko_wrapper(1)) {
                return 0;
            }
            continue;
        }
        unsigned selected_bit = 1u << selected_index;
        if ((seen & selected_bit) ||
            current_node->address != sko_session.selected[selected_index]) {
            return 0;
        }
        seen |= selected_bit;
        uintptr_t expected = current_node->kind ? sko_session.state_original :
                                                 sko_session.action_original;
        if (sko_session.owned & selected_bit) {
            expected = sko_wrapper(current_node->kind);
        }
        if (sko_session.uncertain & selected_bit) {
            if (current_node->handler != expected &&
                current_node->handler != sko_wrapper(current_node->kind)) {
                return 0;
            }
        } else if (current_node->handler != expected) {
            return 0;
        }
        if (selected_index == (int)target) {
            fresh_handler_address = current_node->address + 0x28;
        }
    }
    if (seen != (1u << SKO_SELECTED) - 1 || !fresh_handler_address) {
        return 0;
    }
    *handler_address = fresh_handler_address;
    return 1;
}

static int sko_begin(const sko_platform *platform, uintptr_t owner_pointer, uintptr_t action_original,
                     uintptr_t state_original, int quiescent) {
    if (!quiescent || sko_initialized || !platform || !platform->read || !platform->compare_exchange ||
        !platform->verify_pins || !platform->thread_id || !platform->call_action ||
        !platform->call_state || !owner_pointer || !action_original || !state_original ||
        !platform->verify_pins(platform->context)) {
        return 0;
    }
    sko_session.platform = *platform;
    sko_session.owner_pointer = owner_pointer;
    sko_session.action_original = action_original;
    sko_session.state_original = state_original;
    sko_initialized = 1;
    if (!sko_census(sko_session.baseline, &sko_session.count, &sko_session.owner)) {
        return 0;
    }
    unsigned seen = 0;
    for (unsigned node_index = 0; node_index < sko_session.count; ++node_index) {
        sko_node *node = &sko_session.baseline[node_index];
        int selected_index = sko_index(node->kind, node->key);
        if (selected_index < 0) {
            continue;
        }
        unsigned selected_bit = 1u << selected_index;
        uintptr_t expected = node->kind == 0 ? action_original : state_original;
        if ((seen & selected_bit) || node->handler != expected) {
            return 0;
        }
        seen |= selected_bit;
        sko_session.selected[selected_index] = node->address;
    }
    sko_prepared = seen == (1u << SKO_SELECTED) - 1;
    return sko_prepared;
}

/* Stop recording first. A failed or ambiguous restore keeps the corresponding
 * obligation, and wrappers continue forwarding via resident session metadata. */
static int sko_restore(int quiescent) {
    atomic_store_explicit(&sko_session.active, 0, memory_order_release);
    if (!sko_prepared || !quiescent || atomic_load(&sko_session.wrapper_depth) ||
        !sko_session.platform.verify_pins(sko_session.platform.context)) {
        return 0;
    }
    for (unsigned selected_index = 0; selected_index < SKO_SELECTED; ++selected_index) {
        unsigned selected_bit = 1u << selected_index;
        if (!((sko_session.owned | sko_session.uncertain | sko_session.pending_readback) &
              selected_bit)) {
            continue;
        }
        uintptr_t address = 0;
        if (!sko_refresh(selected_index, &address)) {
            return 0;
        }
        uintptr_t current_handler;
        if (!sko_read(address, &current_handler, sizeof(current_handler))) {
            return 0;
        }
        uintptr_t wrapper_handler = sko_wrapper(selected_index ? 1u : 0u);
        uintptr_t original_handler =
            selected_index ? sko_session.state_original : sko_session.action_original;
        if (sko_session.pending_readback & selected_bit) {
            if (current_handler != original_handler) {
                return 0;
            }
            sko_session.pending_readback &= ~selected_bit;
            continue;
        }
        if (current_handler != wrapper_handler) {
            return 0;
        }
        sko_cas_result exchange_result = sko_session.platform.compare_exchange(
            sko_session.platform.context, address, wrapper_handler, original_handler);
        if (exchange_result == SKO_CAS_UNKNOWN) {
            sko_session.uncertain |= selected_bit;
            return 0;
        }
        if (exchange_result != SKO_CAS_APPLIED) {
            return 0;
        }
        sko_session.owned &= ~selected_bit;
        sko_session.uncertain &= ~selected_bit;
        sko_session.pending_readback |= selected_bit;
        address = 0;
        if (!sko_refresh(selected_index, &address) ||
            !sko_read(address, &current_handler, sizeof(current_handler)) ||
            current_handler != original_handler) {
            return 0;
        }
        sko_session.pending_readback &= ~selected_bit;
    }
    return !(sko_session.owned | sko_session.uncertain | sko_session.pending_readback);
}

static int sko_install(int quiescent) {
    if (!sko_prepared || !quiescent || atomic_load(&sko_session.wrapper_depth) || sko_session.owned ||
        sko_session.uncertain || sko_session.pending_readback ||
        !sko_session.platform.verify_pins(sko_session.platform.context)) {
        return 0;
    }
    for (unsigned selected_index = 0; selected_index < SKO_SELECTED; ++selected_index) {
        uintptr_t address = 0;
        if (!sko_refresh(selected_index, &address)) {
            (void)sko_restore(quiescent);
            return 0;
        }
        unsigned selected_bit = 1u << selected_index;
        uintptr_t original_handler =
            selected_index ? sko_session.state_original : sko_session.action_original;
        sko_cas_result exchange_result = sko_session.platform.compare_exchange(
            sko_session.platform.context, address, original_handler,
            sko_wrapper(selected_index ? 1u : 0u));
        if (exchange_result == SKO_CAS_UNKNOWN) {
            sko_session.uncertain |= selected_bit;
        }
        if (exchange_result != SKO_CAS_APPLIED) {
            (void)sko_restore(quiescent);
            return 0;
        }
        sko_session.owned |= selected_bit;
        address = 0;
        if (!sko_refresh(selected_index, &address)) {
            (void)sko_restore(quiescent);
            return 0;
        }
    }
    atomic_store_explicit(&sko_session.active, 1, memory_order_release);
    return 1;
}

/* Call only under the same quiescent boundary before trusting recorded events. */
static int sko_validate(int quiescent) {
    uintptr_t address = 0;
    if (!quiescent || !atomic_load(&sko_session.active) ||
        !sko_session.platform.verify_pins(sko_session.platform.context) || !sko_refresh(0, &address)) {
        atomic_store(&sko_session.active, 0);
        return 0;
    }
    return 1;
}

/* Draining is allowed only at the proven boundary and does not clear overflow. */
static unsigned sko_drain(sko_event *out, unsigned capacity, int quiescent) {
    if (!quiescent || !out || atomic_load(&sko_session.wrapper_depth) ||
        atomic_flag_test_and_set_explicit(&sko_session.event_lock, memory_order_acquire)) {
        return 0;
    }
    unsigned count = sko_session.event_count;
    if (count > capacity) {
        count = capacity;
    }
    memcpy(out, sko_session.events, count * sizeof(*out));
    memmove(sko_session.events, sko_session.events + count,
            (sko_session.event_count - count) * sizeof(*out));
    sko_session.event_count -= count;
    atomic_flag_clear_explicit(&sko_session.event_lock, memory_order_release);
    return count;
}

static int sko_restored(void) {
    return sko_initialized && !sko_session.owned && !sko_session.uncertain &&
           !sko_session.pending_readback;
}

typedef struct {
    unsigned active;
    unsigned owned;
    unsigned uncertain;
    unsigned pending_readback;
    unsigned wrapper_depth;
    unsigned overflow;
    unsigned queued;
} sko_status;

/* Management and status are read at the same serialized boundary. */
static sko_status sko_get_status(void) {
    return (sko_status){(unsigned)atomic_load(&sko_session.active),
                        sko_session.owned,
                        sko_session.uncertain,
                        sko_session.pending_readback,
                        atomic_load(&sko_session.wrapper_depth),
                        atomic_load(&sko_session.overflow),
                        sko_session.event_count};
}
#endif
