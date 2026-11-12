#ifndef X4_GATE_PLAYER_CONTEXT_DIAGNOSTIC_H
#define X4_GATE_PLAYER_CONTEXT_DIAGNOSTIC_H

/* Failure-only observations of already copied census payloads. Partial rows are
 * explicitly marked incomplete; they are not a validated tree or live handles. */
#include "input_context_census.h"

static void player_context_diagnostic(const char *stage, uint64_t tick, uint64_t thread,
        uintptr_t manager, int collected, unsigned complete, unsigned tree_valid,
        unsigned nodes, unsigned targets, const char *reason,
        const unsigned char (*payloads)[INPUT_CONTEXT_CENSUS_PAYLOAD_BYTES], unsigned count) {
    unsigned emitted = count > INPUT_CONTEXT_CENSUS_MAX_NODES ? INPUT_CONTEXT_CENSUS_MAX_NODES : count;
    unsigned full = tree_valid == 1 && count == nodes &&
                    count <= INPUT_CONTEXT_CENSUS_MAX_NODES;
    char message[320];
    snprintf(message, sizeof message,
        "X4LAN CONTEXT_FAILURE stage=%s tick=%llu thread=%llu manager=%llx "
        "collected=%d complete=%u tree_valid=%u nodes=%u targets=%u count=%u emitted=%u full=%u reason=%s\n",
        stage, (unsigned long long)tick, (unsigned long long)thread, (unsigned long long)manager,
        collected, complete, tree_valid, nodes, targets, count, emitted, full, reason ? reason : "unknown");
    lifecycle_discard_write(message);
    if (!payloads) return;
    static const char hex[] = "0123456789abcdef";
    for (unsigned index = 0; index < emitted; ++index) {
        char encoded[INPUT_CONTEXT_CENSUS_PAYLOAD_BYTES * 2 + 1];
        for (unsigned byte = 0; byte < INPUT_CONTEXT_CENSUS_PAYLOAD_BYTES; ++byte) {
            encoded[2 * byte] = hex[payloads[index][byte] >> 4];
            encoded[2 * byte + 1] = hex[payloads[index][byte] & 15];
        }
        encoded[sizeof encoded - 1] = 0;
        snprintf(message, sizeof message,
            "X4LAN CONTEXT_FAILURE_ROW stage=%s tick=%llu index=%u context=%08x requester=%08x payload=%s\n",
            stage, (unsigned long long)tick, index, input_context_census_u32(payloads[index]),
            input_context_census_u32(payloads[index] + 4), encoded);
        lifecycle_discard_write(message);
    }
}
#endif
