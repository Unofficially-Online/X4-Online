#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stock_keyboard_observer.h"

typedef struct {
    uintptr_t parent;
    uintptr_t left;
    uintptr_t right;
    uintptr_t color;
    int key;
    int pad;
    uintptr_t handler;
} test_node;

typedef struct {
    unsigned char owner[0x6500];
    uintptr_t owner_pointer;
    test_node nodes[SKO_TREE_LIMIT + 1];
    unsigned calls;
    unsigned original_calls;
    unsigned consumer_calls;
    int consume_response;
    int original_entry_errno;
    sko_event consumer_events[8];
    unsigned fail_at;
    unsigned unknown_at;
    unsigned grow_tree_at;
    int fail_read;
    int fail_read_after_cas;
    int pins_valid;
    int last_id;
    int last_state;
} fixture;

static void rebuild_states(fixture *test_fixture, int include_extra, int changed_selected);

static int valid_memory(fixture *test_fixture, uintptr_t address, size_t size) {
    uintptr_t owner_address = (uintptr_t)test_fixture->owner;
    uintptr_t nodes_address = (uintptr_t)test_fixture->nodes;
    uintptr_t owner_pointer_address = (uintptr_t)&test_fixture->owner_pointer;
    return (address >= owner_address && address - owner_address <= sizeof(test_fixture->owner) &&
            size <= sizeof(test_fixture->owner) - (address - owner_address)) ||
           (address >= nodes_address && address - nodes_address <= sizeof(test_fixture->nodes) &&
            size <= sizeof(test_fixture->nodes) - (address - nodes_address)) ||
           (address == owner_pointer_address && size == sizeof(test_fixture->owner_pointer));
}

static int fake_read(void *context, uintptr_t address, void *out, size_t size) {
    fixture *test_fixture = context;
    if (test_fixture->fail_read) {
        test_fixture->fail_read = 0;
        return 0;
    }
    if (!valid_memory(test_fixture, address, size)) {
        return 0;
    }
    memcpy(out, (void *)address, size);
    return 1;
}

static sko_cas_result fake_cas(void *context, uintptr_t address, uintptr_t expected,
                               uintptr_t replacement) {
    fixture *test_fixture = context;
    ++test_fixture->calls;
    if (!valid_memory(test_fixture, address, sizeof(uintptr_t))) {
        return SKO_CAS_REFUSED;
    }
    if (test_fixture->calls == test_fixture->fail_at) {
        return SKO_CAS_REFUSED;
    }
    uintptr_t current;
    memcpy(&current, (void *)address, sizeof(current));
    if (current != expected) {
        return SKO_CAS_CHANGED;
    }
    memcpy((void *)address, &replacement, sizeof(replacement));
    if (test_fixture->calls == test_fixture->grow_tree_at) {
        test_fixture->nodes[15] = (test_node){.key = 10, .handler = 0x4440};
        rebuild_states(test_fixture, 1, 0);
    }
    if (test_fixture->fail_read_after_cas) {
        test_fixture->fail_read_after_cas = 0;
        test_fixture->fail_read = 1;
    }
    return test_fixture->calls == test_fixture->unknown_at ? SKO_CAS_UNKNOWN : SKO_CAS_APPLIED;
}

static int fake_pins(void *context) {
    return ((fixture *)context)->pins_valid;
}

static uint64_t fake_thread(void *context) {
    (void)context;
    return 42;
}

static void fake_action(void *context, uintptr_t function, int id) {
    fixture *test_fixture = context;
    assert(function == 0x1110);
    test_fixture->original_entry_errno = errno;
    ++test_fixture->original_calls;
    test_fixture->last_id = id;
    errno = 23;
}

static void fake_state(void *context, uintptr_t function, int id, unsigned char state) {
    fixture *test_fixture = context;
    assert(function == 0x2220);
    test_fixture->original_entry_errno = errno;
    ++test_fixture->original_calls;
    test_fixture->last_id = id;
    test_fixture->last_state = state;
    errno = 24;
}

static int fake_consume(void *context, const sko_event *event) {
    fixture *test_fixture = context;
    assert(test_fixture->consumer_calls < 8);
    assert(atomic_load(&sko_session.wrapper_depth) == 1);
    test_fixture->consumer_events[test_fixture->consumer_calls++] = *event;
    errno = 99; /* Consumer bookkeeping must not leak into the native call. */
    return test_fixture->consume_response;
}

/* Callback dispatch is independent of whether observer recording is active. */
static void exercise_consumer_pair(fixture *test_fixture, int callback_enabled,
                                   int consumed, int recording) {
    unsigned original_calls = test_fixture->original_calls;
    unsigned consumer_calls = test_fixture->consumer_calls;
    test_fixture->consume_response = consumed;
    assert((sko_session.consume_event != NULL) == callback_enabled);
    errno = 71;
    sko_state_wrapper(19, 0xa5);
    assert(test_fixture->original_calls == original_calls + (consumed ? 0u : 1u));
    assert(errno == (consumed ? 71 : 24));
    if (!consumed) {
        assert(test_fixture->last_id == 19 && test_fixture->last_state == 0xa5);
        assert(test_fixture->original_entry_errno == 71);
    }
    errno = 72;
    sko_action_wrapper(0x7b);
    assert(test_fixture->original_calls == original_calls + (consumed ? 0u : 2u));
    assert(errno == (consumed ? 72 : 23));
    if (!consumed) {
        assert(test_fixture->last_id == 0x7b);
        assert(test_fixture->original_entry_errno == 72);
    }
    assert(atomic_load(&sko_session.wrapper_depth) == 0);
    assert(test_fixture->consumer_calls == consumer_calls + (callback_enabled ? 2u : 0u));
    if (callback_enabled) {
        const sko_event *state = &test_fixture->consumer_events[consumer_calls];
        const sko_event *action = &test_fixture->consumer_events[consumer_calls + 1];
        assert(state->kind == 1 && state->id == 19 && state->state == 0xa5);
        assert(action->kind == 0 && action->id == 0x7b && action->state == 0);
        assert(state->thread == 42 && action->thread == 42);
        assert(state->caller && action->caller);
    }
    sko_event recorded[2];
    assert(sko_drain(recorded, 2, 1) == (recording ? 2u : 0u));
    if (recording) {
        assert(recorded[0].kind == 1 && recorded[0].id == 19 && recorded[0].state == 0xa5);
        assert(recorded[1].kind == 0 && recorded[1].id == 0x7b && recorded[1].state == 0);
        for (unsigned event_index = 0; event_index < 2; ++event_index) {
            assert(recorded[event_index].thread == 42 && recorded[event_index].caller);
            if (callback_enabled) {
                const sko_event *observed = &test_fixture->consumer_events[consumer_calls + event_index];
                assert(recorded[event_index].caller == observed->caller);
                assert(recorded[event_index].thread == observed->thread);
            }
        }
    }
}

static void setup(fixture *test_fixture) {
    memset(test_fixture, 0, sizeof(*test_fixture));
    test_fixture->pins_valid = 1;
    test_fixture->owner_pointer = (uintptr_t)test_fixture->owner;
    uintptr_t action_header = (uintptr_t)test_fixture->owner + 0x63f0;
    uintptr_t state_header = action_header + 0x40;
    uintptr_t action_root = (uintptr_t)&test_fixture->nodes[0];
    uintptr_t state_root = (uintptr_t)&test_fixture->nodes[1];
    memcpy((void *)(action_header + 8), &action_root, 8);
    memcpy((void *)(state_header + 8), &state_root, 8);
    test_fixture->nodes[0] = (test_node){.parent = action_header, .key = 0x7b, .handler = 0x1110};
    for (unsigned node_index = 1; node_index < 15; ++node_index) {
        test_fixture->nodes[node_index].parent =
            node_index == 1 ? state_header : (uintptr_t)&test_fixture->nodes[node_index - 1];
        test_fixture->nodes[node_index].right =
            node_index == 14 ? 0 : (uintptr_t)&test_fixture->nodes[node_index + 1];
        test_fixture->nodes[node_index].key = sko_state_ids[node_index - 1];
        test_fixture->nodes[node_index].handler = 0x2220;
    }
}

/* Rebuild a valid state tree while preserving each supplied node's address. */
static uintptr_t balanced_nodes(test_node **nodes, unsigned count, uintptr_t parent) {
    if (!count) {
        return 0;
    }
    unsigned middle = count / 2;
    test_node *root = nodes[middle];
    root->parent = parent;
    root->left = balanced_nodes(nodes, middle, (uintptr_t)root);
    root->right = balanced_nodes(nodes + middle + 1, count - middle - 1, (uintptr_t)root);
    return (uintptr_t)root;
}

static void rebuild_states(fixture *test_fixture, int include_extra, int changed_selected) {
    test_node *nodes[16];
    unsigned count = 0;
    for (unsigned node_index = 1; node_index < 15; ++node_index) {
        if (changed_selected == 1 && node_index == 5) {
            continue;
        }
        nodes[count++] = changed_selected == 2 && node_index == 5 ?
                         &test_fixture->nodes[16] : &test_fixture->nodes[node_index];
    }
    if (include_extra) {
        nodes[count++] = &test_fixture->nodes[15];
    }
    if (changed_selected == 3) {
        nodes[count++] = &test_fixture->nodes[16];
    }
    /* Insertion sort also permits equal keys, as the native multimap does. */
    for (unsigned node_index = 1; node_index < count; ++node_index) {
        test_node *node = nodes[node_index];
        unsigned position = node_index;
        while (position && nodes[position - 1]->key > node->key) {
            nodes[position] = nodes[position - 1];
            --position;
        }
        nodes[position] = node;
    }
    uintptr_t header = (uintptr_t)test_fixture->owner + 0x6430;
    uintptr_t root = balanced_nodes(nodes, count, header);
    memcpy((void *)(header + 8), &root, sizeof(root));
}

static void add_range_over_limit(fixture *test_fixture) {
    uintptr_t header = (uintptr_t)test_fixture->owner + 0x6470;
    uintptr_t root = (uintptr_t)&test_fixture->nodes[15];
    memcpy((void *)(header + 8), &root, sizeof(root));
    for (unsigned node_index = 15; node_index <= SKO_TREE_LIMIT; ++node_index) {
        test_fixture->nodes[node_index] = (test_node){
            .parent = node_index == 15 ? header : (uintptr_t)&test_fixture->nodes[node_index - 1],
            .right = node_index == SKO_TREE_LIMIT ? 0 :
                     (uintptr_t)&test_fixture->nodes[node_index + 1],
            .key = (int)node_index,
            .handler = 0x4440,
        };
    }
}

int main(int argc, char **argv) {
    assert(argc == 2);
    fixture test_fixture;
    setup(&test_fixture);
    sko_platform platform = {&test_fixture, fake_read,   fake_cas,  fake_pins,
                             fake_thread,   fake_action, fake_state};
    if (!strcmp(argv[1], "duplicate")) {
        test_fixture.nodes[2].key = test_fixture.nodes[1].key;
    }
    if (!strcmp(argv[1], "unselected_remove")) {
        test_fixture.nodes[15] = (test_node){.key = 10, .handler = 0x4440};
        rebuild_states(&test_fixture, 1, 0);
    }
    int prepared = sko_begin(&platform, (uintptr_t)&test_fixture.owner_pointer, 0x1110, 0x2220, 1);
    if (!strcmp(argv[1], "duplicate")) {
        assert(!prepared);
        assert(!sko_install(1));
        return 0;
    }
    assert(prepared);
    if (!strcmp(argv[1], "rollback") || !strcmp(argv[1], "rollback_growth")) {
        test_fixture.grow_tree_at = !strcmp(argv[1], "rollback_growth") ? 2 : 0;
        test_fixture.fail_at = 5;
        assert(!sko_install(1));
        assert(sko_restored());
        for (unsigned node_index = 0; node_index < 15; ++node_index) {
            assert(test_fixture.nodes[node_index].handler == (node_index ? 0x2220u : 0x1110u));
        }
        return 0;
    }
    if (!strcmp(argv[1], "unknown")) {
        test_fixture.unknown_at = 4;
        test_fixture.fail_at = 5;
        assert(!sko_install(1));
        assert(!sko_restored());
        test_fixture.fail_at = 0;
        test_fixture.unknown_at = 0;
        assert(sko_restore(1));
        return 0;
    }
    if (!strcmp(argv[1], "install_growth")) {
        test_fixture.grow_tree_at = 5;
    }
    assert(!sko_install(0));
    assert(sko_install(1));
    assert(sko_validate(1));
    assert(sko_get_status().active);
    sko_state_wrapper(2, 7);
    assert(test_fixture.original_calls == 1 && test_fixture.last_id == 2 &&
           test_fixture.last_state == 7 && errno == 24);
    sko_action_wrapper(0x7b);
    assert(test_fixture.original_calls == 2 && errno == 23);
    sko_event events[2];
    assert(sko_drain(events, 2, 1) == 2);
    assert(events[0].kind == 1 && events[0].state == 7 && events[0].thread == 42 && events[0].caller);
    assert(events[1].kind == 0 && events[1].id == 0x7b);
    if (!strncmp(argv[1], "consumer_", 9)) {
        assert(sko_session.consume_event == NULL);
        assert(test_fixture.consumer_calls == 0);
        int enabled = strcmp(argv[1], "consumer_default") != 0;
        if (enabled) {
            sko_session.consume_event = fake_consume;
        }
        int consumed = enabled && strcmp(argv[1], "consumer_passthrough") != 0;
        exercise_consumer_pair(&test_fixture, enabled, consumed, 1);
        if (!strcmp(argv[1], "consumer_failed_cleanup")) {
            test_fixture.fail_at = test_fixture.calls + 1;
            assert(!sko_restore(1) && !sko_restored());
            assert(!sko_get_status().active);
            assert(sko_get_status().owned == (1u << SKO_SELECTED) - 1);
            exercise_consumer_pair(&test_fixture, 1, 1, 0);
            exercise_consumer_pair(&test_fixture, 1, 0, 0);
            test_fixture.fail_at = 0;
        }
        assert(sko_restore(1) && sko_restored());
        assert(!sko_get_status().active);
        if (!strcmp(argv[1], "consumer_stopped")) {
            /* A late consumer makes its own consume/forward choice after STOP. */
            exercise_consumer_pair(&test_fixture, 1, 1, 0);
            exercise_consumer_pair(&test_fixture, 1, 0, 0);
        }
        if (!enabled) {
            exercise_consumer_pair(&test_fixture, 0, 0, 0);
            assert(test_fixture.consumer_calls == 0);
        }
        return 0;
    }
    if (!strcmp(argv[1], "unselected_growth") || !strcmp(argv[1], "unselected_remove") ||
        !strcmp(argv[1], "unselected_rebalance")) {
        unsigned initial_count = sko_session.count;
        test_fixture.nodes[15] = (test_node){.key = 10, .handler = 0x5550};
        int include_extra = !strcmp(argv[1], "unselected_growth");
        rebuild_states(&test_fixture, include_extra, 0);
        assert(sko_validate(1));
        uintptr_t address = 0;
        assert(sko_refresh(5, &address));
        assert(address == (uintptr_t)&test_fixture.nodes[5].handler);
        assert(sko_session.count == initial_count); /* Initial diagnostics retained. */
        assert(sko_restore(1) && sko_restored());
        for (unsigned node_index = 0; node_index < 15; ++node_index) {
            assert(test_fixture.nodes[node_index].handler == (node_index ? 0x2220u : 0x1110u));
        }
        assert(test_fixture.nodes[15].handler == 0x5550);
        assert(test_fixture.calls == 2 * SKO_SELECTED);
        return 0;
    }
    if (!strcmp(argv[1], "selected_remove") || !strcmp(argv[1], "selected_replace") ||
        !strcmp(argv[1], "selected_duplicate")) {
        test_fixture.nodes[16] = test_fixture.nodes[5];
        int change = !strcmp(argv[1], "selected_remove") ? 1 :
                     !strcmp(argv[1], "selected_replace") ? 2 : 3;
        rebuild_states(&test_fixture, 0, change);
        unsigned calls_before = test_fixture.calls;
        uintptr_t address = 0xdead;
        assert(!sko_refresh(0, &address));
        assert(address == 0xdead); /* No partial address escapes a failed census. */
        assert(!sko_validate(1));
        assert(!sko_restore(1) && !sko_restored());
        assert(test_fixture.calls == calls_before);
        assert(sko_get_status().owned == (1u << SKO_SELECTED) - 1);
        return 0;
    }
    if (!strncmp(argv[1], "invalid_unselected_", 19)) {
        test_fixture.nodes[15] = (test_node){.key = 10, .handler = 0x4440};
        rebuild_states(&test_fixture, 1, 0);
        assert(sko_validate(1));
        if (!strcmp(argv[1], "invalid_unselected_parent")) {
            test_fixture.nodes[15].parent = 0;
        } else if (!strcmp(argv[1], "invalid_unselected_order")) {
            test_fixture.nodes[15].key = -1;
        } else if (!strcmp(argv[1], "invalid_unselected_cycle")) {
            test_fixture.nodes[15].left = (uintptr_t)&test_fixture.nodes[15];
        } else if (!strcmp(argv[1], "invalid_unselected_read")) {
            test_fixture.nodes[15].left = 8;
        } else {
            assert(!strcmp(argv[1], "invalid_unselected_limit"));
            add_range_over_limit(&test_fixture);
        }
        unsigned calls_before = test_fixture.calls;
        assert(!sko_validate(1));
        assert(!sko_restore(1) && !sko_restored());
        assert(test_fixture.calls == calls_before);
        assert(sko_get_status().owned == (1u << SKO_SELECTED) - 1);
        return 0;
    }
    if (!strcmp(argv[1], "unselected_wrapper")) {
        test_fixture.nodes[15] = (test_node){.key = 10, .handler = sko_wrapper(1)};
        rebuild_states(&test_fixture, 1, 0);
        unsigned calls_before = test_fixture.calls;
        assert(!sko_validate(1));
        assert(!sko_restore(1) && !sko_restored());
        assert(test_fixture.calls == calls_before);
        assert(test_fixture.nodes[15].handler == sko_wrapper(1));
        assert(sko_get_status().owned == (1u << SKO_SELECTED) - 1);
        return 0;
    }
    if (!strcmp(argv[1], "topology")) {
        test_fixture.nodes[3].right = 0;
        assert(!sko_validate(1));
        assert(!sko_restore(1) && !sko_restored());
        sko_action_wrapper(0x7b);
        assert(test_fixture.original_calls == 3);
        return 0;
    }
    if (!strcmp(argv[1], "foreign")) {
        test_fixture.nodes[5].handler = 0x3330;
        assert(!sko_validate(1));
        assert(!sko_restore(1) && !sko_restored());
        assert(test_fixture.nodes[5].handler == 0x3330);
        return 0;
    }
    if (!strcmp(argv[1], "owner")) {
        test_fixture.owner_pointer = 0;
        assert(!sko_validate(1));
        assert(!sko_restore(1) && !sko_restored());
        return 0;
    }
    if (!strcmp(argv[1], "overflow")) {
        for (unsigned event_index = 0; event_index < SKO_EVENT_LIMIT + 1; ++event_index) {
            sko_state_wrapper(2, 1);
        }
        assert(atomic_load(&sko_session.overflow) == 1);
    }
    if (!strcmp(argv[1], "readback") || !strcmp(argv[1], "restore_growth_readback")) {
        test_fixture.grow_tree_at = !strcmp(argv[1], "restore_growth_readback") ?
                                    test_fixture.calls + 1 : 0;
        test_fixture.fail_read_after_cas = 1;
        assert(!sko_restore(1));
        assert(!sko_restored() && sko_get_status().pending_readback == 1);
        assert(sko_get_status().owned == ((1u << SKO_SELECTED) - 1) - 1);
        assert(sko_restore(1) && sko_restored());
        assert(test_fixture.calls == 2 * SKO_SELECTED);
        return 0;
    }
    if (!strcmp(argv[1], "pins")) {
        test_fixture.pins_valid = 0;
        assert(!sko_validate(1));
        assert(!sko_restore(1) && !sko_restored());
        test_fixture.pins_valid = 1;
        assert(sko_restore(1) && sko_restored());
        return 0;
    }
    atomic_store(&sko_session.wrapper_depth, 1);
    assert(!sko_restore(1));
    atomic_store(&sko_session.wrapper_depth, 0);
    assert(sko_restore(1) && sko_restored());
    assert(!sko_get_status().active);
    sko_action_wrapper(0x7b);
    assert(test_fixture.last_id == 0x7b);
    return 0;
}
