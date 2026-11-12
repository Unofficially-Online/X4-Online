"""Compile the real Update probe with stubs; no game or native image is loaded."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--header', type=Path, default=Path(__file__).resolve().parents[2] /
                        'src/native/engine/linux/gate_player_lifecycle_update.h')
    args = parser.parse_args()
    header = args.header.resolve(strict=True)
    compiler = shutil.which('cc')
    if not compiler:
        raise SystemExit('C compiler required; no test skipped')
    source = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <setjmp.h>
typedef struct { const char *name; unsigned va, length; const char *digest; } lx_site;
static unsigned char ship[0x300], controller[0x200], tls[64];
static uintptr_t lifecycle_discard_ship, lifecycle_discard_controller;
static struct { uintptr_t player2; } factory_probe_state;
static struct { unsigned long long issued; } stats = {5};
static uintptr_t singleton = 77, native_zone = 100, global_zone = 100;
static unsigned char in_progress;
static int pins = 1, worker = 1, getter_wrong, update_mode = 2, update_calls, passed;
static const char *failure;
static jmp_buf refused;
#define PLAYER_ZONE_GLOBAL_VA 0x3e4cdd8
#define GATE_TLS_LINUX_PREVIOUS_TIME 8
static int site_ok(const lx_site *site) { (void)site; return pins; }
static int factory_probe_singleton(uintptr_t player) { return player == singleton; }
static void lifecycle_discard_require(int condition, const char *reason) {
    if (!condition) { failure = reason; longjmp(refused, 1); }
}
static void lifecycle_discard_read(uintptr_t address, void *out, size_t length) {
    memcpy(out, (void *)address, length);
}
static uintptr_t lifecycle_discard_pointer(uintptr_t address) {
    uintptr_t value; memcpy(&value, (void *)address, sizeof(value)); return value;
}
static unsigned char *gate_tls_self(void) { return tls; }
static int gate_tls_worker(unsigned char *pointer) { return pointer == tls && worker; }
static double *gate_tls_time(unsigned char *pointer) { return (double *)pointer; }
static void lifecycle_discard_write(const char *text) {
    if (strstr(text, " update=pass ")) passed++;
}
static void *controller_getter(void) {
    return controller + (getter_wrong ? 8 : 0);
}
static void *zone_getter(void) { return (void *)native_zone; }
static void fake_update(void *object, double now) {
    if (object != controller) abort();
    update_calls++;
    double previous; memcpy(&previous, tls + 8, 8);
    if (update_mode == 1) memcpy(controller + 0xe8, &previous, 8);
    if (update_mode >= 2) memcpy(controller + 0xe8, &now, 8);
    if (update_mode == 3) singleton = 88;
}
static void *fixture_lx(unsigned va) {
    switch (va) {
    case 0x1dd19d0: return (void *)controller_getter;
    case 0x1dd0270: return (void *)zone_getter;
    case 0x166d020: return (void *)fake_update;
    case PLAYER_ZONE_GLOBAL_VA: return &global_zone;
    case 0x3ee9b40: return &in_progress;
    default: abort();
    }
}
#define LX(va) fixture_lx(va)
''' + '#include ' + json.dumps(str(header)) + r'''
int main(int argc, char **argv) {
    if (argc != 2) return 90;
    int test = atoi(argv[1]);
    double now = 10, previous = 9, last = 8;
    lifecycle_discard_ship = (uintptr_t)ship;
    lifecycle_discard_controller = (uintptr_t)controller;
    factory_probe_state.player2 = 77;
    uintptr_t head = (uintptr_t)controller;
    switch (test) {
    case 0: pins = 0; break;
    case 1: singleton = 88; break;
    case 2: head += 8; break;
    case 3: getter_wrong = 1; break;
    case 4: native_zone = 0; break;
    case 5: global_zone = 101; break;
    case 6: worker = 0; break;
    case 7: now = NAN; break;
    case 8: previous = INFINITY; break;
    case 9: previous = now; break;
    case 10: last = NAN; break;
    case 11: last = now; break;
    case 12: in_progress = 1; break;
    case 13: update_mode = 0; break;
    case 14: update_mode = 1; break;
    case 15: update_mode = 3; break;
    case 16: break;
    default: return 91;
    }
    memcpy(ship + 0x200, &head, 8);
    memcpy(tls, &now, 8); memcpy(tls + 8, &previous, 8);
    memcpy(controller + 0xe8, &last, 8);
    int rejected = setjmp(refused);
    if (!rejected) lifecycle_discard_update_once(0);
    int expected_calls = test >= 13;
    int expected_pass = test == 16;
    if (update_calls != expected_calls || passed != expected_pass ||
        rejected != !expected_pass || (!expected_pass && !failure)) return 92;
    if (test >= 13 && test <= 15 && strcmp(failure, "update_progress_or_identity")) return 93;
    return 0;
}
'''
    cases = ('bad pins', 'wrong P2', 'wrong head', 'wrong getter', 'null zone', 'wrong zone',
             'nonworker', 'nonfinite current', 'nonfinite previous', 'equal times',
             'nonfinite last', 'already updated', 'in progress', 'no late write',
             'initializer only', 'post identity changed', 'late write with same identity')
    with tempfile.TemporaryDirectory(prefix='x4-update-fixture-') as directory:
        root = Path(directory)
        cfile, binary = root / 'fixture.c', root / 'fixture'
        cfile.write_text(source)
        subprocess.run([compiler, '-std=c11', '-O0', str(cfile), '-o', str(binary)],
                       check=True, capture_output=True, timeout=30)
        for index, name in enumerate(cases):
            result = subprocess.run([str(binary), str(index)], capture_output=True, timeout=3)
            if result.returncode:
                raise AssertionError((name, result.returncode, result.stderr.decode(errors='replace')))
    print(f'PASS: {len(cases)} real-header stub cases; early return and initializer cannot pass; no X4')


if __name__ == '__main__':
    main()
