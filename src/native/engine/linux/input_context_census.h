#ifndef X4_LINUX_INPUT_CONTEXT_CENSUS_H
#define X4_LINUX_INPUT_CONTEXT_CENSUS_H

/* Read-only structural audit of the pinned Linux native input request tree.
 * No engine calls, registration changes, hook installation or lifetime proof.
 * Adapter must anchor manager from fresh InputOwner+0x10 and prove exclusion
 * and readable object lifetimes throughout ALL reads, then revalidate identity.
 * Double-reading a header does not establish concurrent access safety or ABA
 * protection. All addresses/payloads are observations, never retained handles.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* M=InputOwner+0x10; H=M+0x4c50. H+8 root, +0x10 free list,
 * +0x20 leftmost, +0x28 live count, +0x30 capacity, +0x38 pool chunks.
 * Node0x30: parent+0, left+8, right+0x10, colorDWORD+0x18,
 * payload+0x1c = signed(context,requester,priority), bool byte, opaque3 bytes.
 * Native ordering compares context then requester as signed32. Keys unique.
 * Root parent is H. Color0 red,1 black; null leaves have black height1.
 * Pools/free lists are not traversed or interpreted as live registrations.
 */
#define INPUT_CONTEXT_CENSUS_MAX_NODES 512u
#define INPUT_CONTEXT_CENSUS_PAYLOAD_BYTES 16u
#define INPUT_CONTEXT_CENSUS_TARGETS 3u

typedef struct {
    void *context;
    int (*read)(void *context, uintptr_t address, void *output, size_t length);
} input_context_census_operations;

typedef struct {
    unsigned complete;
    unsigned tree_valid;
    unsigned target_present;
    unsigned node_count;
    const char *reason;
    uintptr_t manager;
    uintptr_t target_nodes[INPUT_CONTEXT_CENSUS_TARGETS];
    unsigned char payloads[INPUT_CONTEXT_CENSUS_TARGETS][INPUT_CONTEXT_CENSUS_PAYLOAD_BYTES];
} input_context_census_sample;

/* Optional caller-owned storage. Only publish when sample.tree_valid is true;
 * a failed audit may leave partial observations in this buffer. */
typedef struct {
    unsigned count;
    unsigned char payloads[INPUT_CONTEXT_CENSUS_MAX_NODES][INPUT_CONTEXT_CENSUS_PAYLOAD_BYTES];
} input_context_census_entries;

typedef struct {
    uintptr_t address;
    uintptr_t parent;
    uint64_t lower;
    uint64_t upper;
    unsigned has_lower;
    unsigned has_upper;
} input_context_census_pending;

typedef struct {
    uintptr_t address;
    uintptr_t left;
    uintptr_t right;
    unsigned color;
    unsigned black_height;
} input_context_census_node;

static inline uint64_t input_context_census_u64(const unsigned char *bytes) {
    uint64_t value = 0;
    for (unsigned byte_index = 0; byte_index < 8; ++byte_index) {
        value |= (uint64_t)bytes[byte_index] << (8 * byte_index);
    }
    return value;
}

static inline uint32_t input_context_census_u32(const unsigned char *bytes) {
    uint32_t value = 0;
    for (unsigned byte_index = 0; byte_index < 4; ++byte_index) {
        value |= (uint32_t)bytes[byte_index] << (8 * byte_index);
    }
    return value;
}

static inline int input_context_census_pointer(uintptr_t address, size_t length,
                                               uintptr_t alignment) {
    /* Pinned x86-64 user pointers, not arbitrary uintptr_t fixture arithmetic. */
    return address >= 4096 && address < UINT64_C(0x800000000000) &&
        !(address & (alignment - 1)) && length <= UINT64_C(0x800000000000) - address;
}

static inline int input_context_census_fail(input_context_census_sample *sample,
                                            const char *reason) {
    sample->reason = reason;
    return 0;
}

static inline unsigned input_context_census_child_height(
    const input_context_census_node *nodes, unsigned count, uintptr_t address) {
    if (!address) {
        return 1;
    }
    for (unsigned node_index = 0; node_index < count; ++node_index) {
        if (nodes[node_index].address == address) {
            return nodes[node_index].black_height;
        }
    }
    return 0;
}

/* Returns1 only for a valid complete tree with all three target keys present.
 * Payloads are exact observations (context, requester, priority, enabled, opaque
 * tail), not policy approval: caller must check expected priority/enabled/tail
 * before considering any later interception. On failure complete stays0;
 * partial observations are diagnostic only, never publication permission. */
static inline int input_context_census_collect_entries(
    const input_context_census_operations *operations, uintptr_t manager,
    input_context_census_sample *sample, input_context_census_entries *entries) {
    if (!sample) {
        return 0;
    }
    memset(sample, 0, sizeof(*sample));
    if (entries) entries->count = 0;
    sample->manager = manager;
    sample->reason = "arguments";
    if (!operations || !operations->read ||
        !input_context_census_pointer(manager, 0x4c90, 8)) {
        return 0;
    }
    uintptr_t header = manager + 0x4c50;
    unsigned char original_header[0x40];
    if (!operations->read(operations->context, header, original_header, sizeof(original_header))) {
        return input_context_census_fail(sample, "header_read");
    }
    uintptr_t root = input_context_census_u64(original_header + 8);
    uintptr_t leftmost = input_context_census_u64(original_header + 0x20);
    uint64_t count = input_context_census_u64(original_header + 0x28);
    uint64_t capacity = input_context_census_u64(original_header + 0x30);
    if (count > INPUT_CONTEXT_CENSUS_MAX_NODES || capacity < count ||
        ((!root) != (!count)) || (!count && leftmost && leftmost != header)) {
        return input_context_census_fail(sample, "header_bounds");
    }
    input_context_census_pending pending[INPUT_CONTEXT_CENSUS_MAX_NODES];
    input_context_census_node nodes[INPUT_CONTEXT_CENSUS_MAX_NODES];
    unsigned pending_count = 0;
    unsigned node_count = 0;
    uintptr_t observed_leftmost = 0;
    uint64_t smallest_key = UINT64_MAX;
    if (root) {
        pending[pending_count++] = (input_context_census_pending){.address = root, .parent = header};
    }
    while (pending_count) {
        input_context_census_pending current = pending[--pending_count];
        if (node_count >= count || !input_context_census_pointer(current.address, 0x30, 16) ||
            current.address == header) {
            return input_context_census_fail(sample, "node_bounds");
        }
        for (unsigned previous = 0; previous < node_count; ++previous) {
            if (nodes[previous].address == current.address) {
                return input_context_census_fail(sample, "cycle_or_shared_node");
            }
        }
        unsigned char bytes[0x30];
        if (!operations->read(operations->context, current.address, bytes, sizeof(bytes))) {
            return input_context_census_fail(sample, "node_read");
        }
        uintptr_t parent = input_context_census_u64(bytes);
        uintptr_t left = input_context_census_u64(bytes + 8);
        uintptr_t right = input_context_census_u64(bytes + 0x10);
        unsigned color = input_context_census_u32(bytes + 0x18);
        uint32_t context = input_context_census_u32(bytes + 0x1c);
        uint32_t requester = input_context_census_u32(bytes + 0x20);
        uint64_t key = ((uint64_t)(context ^ UINT32_C(0x80000000)) << 32) |
            (requester ^ UINT32_C(0x80000000));
        if (parent != current.parent || color > 1 ||
            (!node_count && color != 1) ||
            (current.has_lower && key <= current.lower) ||
            (current.has_upper && key >= current.upper)) {
            return input_context_census_fail(sample, "parent_color_or_order");
        }
        if (entries) {
            unsigned position = entries->count;
            while (position) {
                const unsigned char *prior = entries->payloads[position - 1];
                uint64_t prior_key = ((uint64_t)(input_context_census_u32(prior) ^ UINT32_C(0x80000000)) << 32) |
                    (input_context_census_u32(prior + 4) ^ UINT32_C(0x80000000));
                if (prior_key < key) break;
                --position;
            }
            memmove(entries->payloads[position + 1], entries->payloads[position],
                    (entries->count - position) * INPUT_CONTEXT_CENSUS_PAYLOAD_BYTES);
            memcpy(entries->payloads[position], bytes + 0x1c, INPUT_CONTEXT_CENSUS_PAYLOAD_BYTES);
            ++entries->count;
        }
        nodes[node_count++] = (input_context_census_node){current.address, left, right, color, 0};
        sample->node_count = node_count;
        if (!observed_leftmost || key < smallest_key) {
            observed_leftmost = current.address;
            smallest_key = key;
        }
        static const uint32_t target_contexts[INPUT_CONTEXT_CENSUS_TARGETS] = {8, 15, 16};
        for (unsigned target = 0; target < INPUT_CONTEXT_CENSUS_TARGETS; ++target) {
            if (context == target_contexts[target] && requester == 6) {
                if (sample->target_present & (1u << target)) {
                    return input_context_census_fail(sample, "duplicate_target");
                }
                sample->target_present |= 1u << target;
                sample->target_nodes[target] = current.address;
                memcpy(sample->payloads[target], bytes + 0x1c, INPUT_CONTEXT_CENSUS_PAYLOAD_BYTES);
            }
        }
        if (pending_count + !!left + !!right > INPUT_CONTEXT_CENSUS_MAX_NODES) {
            return input_context_census_fail(sample, "traversal_limit");
        }
        if (right) {
            pending[pending_count++] = (input_context_census_pending){
                right, current.address, key, current.upper, 1, current.has_upper};
        }
        if (left) {
            pending[pending_count++] = (input_context_census_pending){
                left, current.address, current.lower, key, current.has_lower, 1};
        }
    }
    if (node_count != count || (count && observed_leftmost != leftmost)) {
        return input_context_census_fail(sample, "count_or_leftmost");
    }
    /* DFS records parents before children; reverse order checks RB invariants. */
    for (unsigned remaining = node_count; remaining; --remaining) {
        input_context_census_node *node = &nodes[remaining - 1];
        unsigned left_height = input_context_census_child_height(nodes, node_count, node->left);
        unsigned right_height = input_context_census_child_height(nodes, node_count, node->right);
        if (!left_height || left_height != right_height) {
            return input_context_census_fail(sample, "black_height");
        }
        if (!node->color) {
            for (unsigned child = 0; child < node_count; ++child) {
                if ((nodes[child].address == node->left || nodes[child].address == node->right) &&
                    !nodes[child].color) {
                    return input_context_census_fail(sample, "red_child");
                }
            }
        }
        node->black_height = left_height + node->color;
    }
    unsigned char verified_header[0x40];
    if (!operations->read(operations->context, header, verified_header, sizeof(verified_header)) ||
        memcmp(original_header, verified_header, sizeof(original_header))) {
        return input_context_census_fail(sample, "header_changed");
    }
    sample->tree_valid = 1;
    if (sample->target_present != 7) {
        return input_context_census_fail(sample, "missing_target");
    }
    sample->complete = 1;
    sample->reason = "complete";
    return 1;
}

static inline int input_context_census_collect(
    const input_context_census_operations *operations, uintptr_t manager,
    input_context_census_sample *sample) {
    return input_context_census_collect_entries(operations, manager, sample, NULL);
}
#endif
