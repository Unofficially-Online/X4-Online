#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned rows, headers;
static char first[400], last[400];
static void lifecycle_discard_write(const char *line) {
    if (!headers++) snprintf(first, sizeof first, "%s", line);
    snprintf(last, sizeof last, "%s", line);
    ++rows;
}
#include "gate_player_context_diagnostic.h"

int main(void) {
    static unsigned char payloads[INPUT_CONTEXT_CENSUS_MAX_NODES][INPUT_CONTEXT_CENSUS_PAYLOAD_BYTES];
    for (unsigned index = 0; index < INPUT_CONTEXT_CENSUS_MAX_NODES; ++index) {
        payloads[index][0] = (unsigned char)index;
        payloads[index][1] = (unsigned char)(index >> 8);
        payloads[index][4] = 6;
        payloads[index][15] = 0xab;
    }
    player_context_diagnostic("observed", 417, 123, 0x1234, 0, 0, 1, 2, 3,
                              "missing_target", payloads, 2);
    assert(rows == 3 && strstr(first, "manager=1234 collected=0 complete=0 tree_valid=1 nodes=2 targets=3 count=2 emitted=2 full=1 reason=missing_target"));
    assert(strstr(last, "index=1 context=00000001 requester=00000006 payload=010000000600000000000000000000ab"));
    rows = headers = 0;
    player_context_diagnostic("observed", 418, 123, 0x1234, 0, 0, 0, 0, 0,
                              "node_read", payloads, INPUT_CONTEXT_CENSUS_MAX_NODES + 3);
    assert(rows == 1 + INPUT_CONTEXT_CENSUS_MAX_NODES);
    assert(strstr(first, "count=515 emitted=512 full=0 reason=node_read"));
    assert(strstr(last, "index=511 context=000001ff requester=00000006"));
    puts("failure context diagnostics retain bounded complete and partial payloads PASS");
}
