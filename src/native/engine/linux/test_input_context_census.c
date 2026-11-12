#include <assert.h>
#include <stdio.h>
#include "input_context_census.h"

#define MANAGER UINT64_C(0x10000)
#define HEADER (MANAGER + 0x4c50)
#define NODE_BASE UINT64_C(0x20000)

typedef struct {
    unsigned char header[0x40];
    unsigned char nodes[INPUT_CONTEXT_CENSUS_MAX_NODES][0x30];
    unsigned reads;
    unsigned fail_read;
    unsigned change_header;
} fixture;

static void put64(unsigned char *bytes, uint64_t value) {
    for (unsigned index = 0; index < 8; ++index) {
        bytes[index] = (unsigned char)(value >> (index * 8));
    }
}

static void put32(unsigned char *bytes, uint32_t value) {
    for (unsigned index = 0; index < 4; ++index) {
        bytes[index] = (unsigned char)(value >> (index * 8));
    }
}

static uintptr_t address(unsigned index) {
    return NODE_BASE + index * 0x30;
}

static int read_image(void *context, uintptr_t source, void *output, size_t length) {
    fixture *test = context;
    ++test->reads;
    if (test->reads == test->fail_read) {
        return 0;
    }
    if (source == HEADER && length == sizeof(test->header)) {
        memcpy(output, test->header, length);
        if (test->change_header && test->reads > 1) {
            ((unsigned char *)output)[0x28] ^= 1;
        }
        return 1;
    }
    if (source >= NODE_BASE && source - NODE_BASE < sizeof(test->nodes) &&
        (source - NODE_BASE) % 0x30 == 0 && length == 0x30) {
        memcpy(output, test->nodes[(source - NODE_BASE) / 0x30], length);
        return 1;
    }
    return 0;
}

static void node(fixture *test, unsigned index, uintptr_t parent,
                 uintptr_t left, uintptr_t right, uint32_t context, unsigned color) {
    unsigned char *bytes = test->nodes[index];
    put64(bytes, parent);
    put64(bytes + 8, left);
    put64(bytes + 0x10, right);
    put32(bytes + 0x18, color);
    put32(bytes + 0x1c, context);
    put32(bytes + 0x20, 6);
    put32(bytes + 0x24, 17 + context); /* Observed priority, not silently normalized. */
    bytes[0x28] = 1;
    bytes[0x29] = 0xaa;
    bytes[0x2a] = 0xbb;
    bytes[0x2b] = 0xcc;
}

static void initialize(fixture *test) {
    memset(test, 0, sizeof(*test));
    put64(test->header + 8, address(1));
    put64(test->header + 0x20, address(0));
    put64(test->header + 0x28, 3);
    put64(test->header + 0x30, 3);
    node(test, 1, HEADER, address(0), address(2), 15, 1);
    node(test, 0, address(1), 0, 0, 8, 1);
    node(test, 2, address(1), 0, 0, 16, 1);
}

static int collect(fixture *test, input_context_census_sample *sample) {
    input_context_census_operations operations = {test, read_image};
    return input_context_census_collect(&operations, MANAGER, sample);
}

static void test_valid_and_payloads(void) {
    fixture test;
    input_context_census_sample sample;
    initialize(&test);
    memset(&sample, 0xff, sizeof(sample));
    assert(collect(&test, &sample));
    assert(sample.complete && sample.tree_valid && sample.target_present == 7);
    assert(sample.node_count == 3 && test.reads == 5);
    for (unsigned target = 0; target < INPUT_CONTEXT_CENSUS_TARGETS; ++target) {
        assert(sample.target_nodes[target] == address(target));
        assert(!memcmp(sample.payloads[target], test.nodes[target] + 0x1c, 16));
    }
    /* Valid extra records change topology and leave exact target payloads intact. */
    node(&test, 3, address(0), 0, 0, 7, 0);
    node(&test, 4, address(0), 0, 0, 10, 0);
    put64(test.nodes[0] + 8, address(3));
    put64(test.nodes[0] + 0x10, address(4));
    put64(test.header + 0x20, address(3));
    put64(test.header + 0x28, 5);
    put64(test.header + 0x30, 5);
    assert(collect(&test, &sample));
    assert(sample.node_count == 5);
}

static void test_malformed(unsigned failure) {
    fixture test;
    input_context_census_sample sample;
    initialize(&test);
    switch (failure) {
        case 0:
            put64(test.header + 0x28, 2);
            break;
        case 1:
            put64(test.header + 0x28, 4);
            put64(test.header + 0x30, 4);
            break;
        case 2:
            put64(test.header + 0x28, 513);
            put64(test.header + 0x30, 513);
            break;
        case 3:
            put64(test.header + 0x30, 2);
            break;
        case 4:
            put64(test.nodes[0], HEADER);
            break;
        case 5:
            put32(test.nodes[0] + 0x1c, 20);
            break;
        case 6:
            put32(test.nodes[0] + 0x1c, 15);
            break;
        case 7:
            put64(test.nodes[1] + 0x10, address(0));
            break;
        case 8:
            put64(test.nodes[0] + 8, address(1));
            break;
        case 9:
            put64(test.header + 8, 1);
            break;
        case 10:
            put64(test.header + 8, 0x30000);
            break;
        case 11:
            put64(test.header + 8, address(1) + 1);
            break;
        case 12:
            put64(test.header + 8, 0);
            break;
        case 13:
            put64(test.header + 0x20, address(2));
            break;
        case 14:
            put32(test.nodes[1] + 0x18, 0);
            break;
        case 15:
            put32(test.nodes[0] + 0x18, 2);
            break;
        case 16:
            put32(test.nodes[0] + 0x18, 0);
            break;
        case 17:
            test.fail_read = 1;
            break;
        case 18:
            test.fail_read = 3;
            break;
        case 19:
            test.fail_read = 5;
            break;
        case 20:
            test.change_header = 1;
            break;
        case 21:
            put32(test.nodes[2] + 0x20, 7);
            break;
        case 22:
            put64(test.nodes[0] + 8, UINT64_C(0xffff800000000000));
            break;
        case 23:
            put64(test.nodes[0] + 8, HEADER);
            break;
        case 24:
            put32(test.nodes[0] + 0x1c, UINT32_C(0x80000000));
                 put32(test.nodes[2] + 0x1c, UINT32_C(0x7fffffff)); break;
        case 25:
            node(&test, 3, address(0), 0, 0, 7, 0);
            node(&test, 4, address(0), 0, 0, 10, 0);
            put64(test.nodes[0] + 8, address(3));
            put64(test.nodes[0] + 0x10, address(4));
            put32(test.nodes[0] + 0x18, 0);
            put64(test.header + 0x20, address(3));
            put64(test.header + 0x28, 5);
            put64(test.header + 0x30, 5);
            break;
        default:
            assert(0);
    }
    assert(!collect(&test, &sample));
    assert(!sample.complete && sample.reason);
    assert(test.reads <= INPUT_CONTEXT_CENSUS_MAX_NODES + 2);
    if (failure == 21 || failure == 24) {
        assert(sample.tree_valid);
        assert(!strcmp(sample.reason, "missing_target"));
    } else {
        assert(!sample.tree_valid);
    }
}

static uintptr_t perfect_tree(fixture *test, unsigned first, unsigned count, uintptr_t parent) {
    if (!count) {
        return 0;
    }
    unsigned middle = first + count / 2;
    uintptr_t current = address(middle);
    uintptr_t left = perfect_tree(test, first, count / 2, current);
    uintptr_t right = perfect_tree(test, middle + 1, count / 2, current);
    node(test, middle, parent, left, right, middle, 1);
    return current;
}

static void test_complete_entries(void) {
    fixture test;
    input_context_census_sample sample;
    input_context_census_entries entries;
    initialize(&test);
    input_context_census_operations operations = {&test, read_image};
    assert(input_context_census_collect_entries(&operations, MANAGER, &sample, &entries));
    assert(entries.count == 3 && sample.tree_valid);
    for (unsigned i = 0; i < 3; ++i)
        assert(!memcmp(entries.payloads[i], test.nodes[i] + 0x1c, 16));
    /* An invalid tree must never be mistaken for a complete result. */
    put64(test.header + 0x20, address(2));
    assert(!input_context_census_collect_entries(&operations, MANAGER, &sample, &entries));
    assert(!sample.tree_valid);
    initialize(&test);
    put32(test.nodes[0] + 0x1c, UINT32_C(0x80000000));
    put32(test.nodes[2] + 0x1c, UINT32_C(0x7fffffff));
    assert(!input_context_census_collect_entries(&operations, MANAGER, &sample, &entries));
    assert(sample.tree_valid && entries.count == 3); /* missing target, valid tree */
    assert(input_context_census_u32(entries.payloads[0]) == UINT32_C(0x80000000));
    assert(input_context_census_u32(entries.payloads[1]) == 15);
    assert(input_context_census_u32(entries.payloads[2]) == UINT32_C(0x7fffffff));
}

static void test_traversal_cap(void) {
    fixture test;
    input_context_census_sample sample;
    memset(&test, 0, sizeof(test));
    put64(test.header + 8, perfect_tree(&test, 0, 511, HEADER));
    put64(test.header + 0x20, address(0));
    put64(test.header + 0x28, 512);
    put64(test.header + 0x30, 512);
    node(&test, 511, address(510), 0, 0, 511, 0);
    put64(test.nodes[510] + 0x10, address(511));
    assert(collect(&test, &sample));
    assert(sample.node_count == 512 && test.reads == 514);
    input_context_census_entries entries;
    input_context_census_operations operations = {&test, read_image};
    assert(input_context_census_collect_entries(&operations, MANAGER, &sample, &entries));
    assert(entries.count == 512 && sample.tree_valid);
    for (unsigned index = 0; index < 512; ++index)
        assert(input_context_census_u32(entries.payloads[index]) == index);
}

static void test_arguments_and_empty(void) {
    fixture test;
    input_context_census_sample sample;
    initialize(&test);
    input_context_census_operations operations = {&test, read_image};
    assert(!input_context_census_collect(&operations, MANAGER, NULL));
    assert(!input_context_census_collect(NULL, MANAGER, &sample));
    assert(!input_context_census_collect(&operations, 0, &sample));
    assert(!test.reads);
    memset(test.header, 0, sizeof(test.header));
    put64(test.header + 0x20, HEADER);
    assert(!collect(&test, &sample));
    assert(sample.tree_valid && !sample.target_present && !sample.complete);
}

int main(void) {
    test_valid_and_payloads();
    for (unsigned failure = 0; failure < 26; ++failure) {
        test_malformed(failure);
    }
    test_traversal_cap();
    test_complete_entries();
    test_arguments_and_empty();
    puts("input context census: 29 scenario groups passed");
    return 0;
}
