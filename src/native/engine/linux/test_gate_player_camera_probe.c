#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef struct { const char *name; uintptr_t address; size_t size; const char *hash; } lx_site;
static struct { const char *exe; } gate_env;
#define LX_FILE_SIZE 1
static int image_ok = 1, rejected_site = -1, site_index, hold_ok = 1, resolve_ok = 1;
static struct { struct { uintptr_t target; uint64_t target_runtime; } identity; } player_target_hold_resident;
static int gate_file_sha256(const char *path, size_t size, unsigned char *output) {
    (void)path; (void)size; memset(output, 0, 32); return image_ok;
}
static void hex32(const unsigned char *digest, char *output) {
    (void)digest;
    strcpy(output, image_ok == 2 ? "wrong" : "113f73d74570084f6ce18a659b365009eccf579fb66dbabb5dad2b1718045f20");
}
static int site_ok(const lx_site *site) {
    assert(site->name && site->size && strlen(site->hash) == 64);
    return site_index++ != rejected_site;
}
static int player_physics_identity(void) { return hold_ok; }
static int player_seat_preflight_resolve(void *context, uint64_t saved, uintptr_t *target, uint64_t *runtime) {
    (void)context;
    assert(saved == UINT64_C(0x175310));
    *target = 0x1234; *runtime = 42;
    return resolve_ok;
}
static uint64_t fake_current(void) { return 42; }
#define LX(address) ((void)(address), (void *)fake_current)
#define PLAYER_CAMERA_TEST_PIN_HELPERS_ONLY
#include "gate_player_camera_probe.h"

int main(void) {
    assert(player_camera_native_pins() && site_index == 4);
    for (int index = 0; index < 4; ++index) {
        site_index = 0; rejected_site = index;
        assert(!player_camera_native_pins());
    }
    rejected_site = -1; site_index = 0; image_ok = 0;
    assert(!player_camera_native_pins() && site_index == 0);
    image_ok = 2;
    assert(!player_camera_native_pins() && site_index == 0);
    player_camera_target target = {0};
    player_target_hold_resident.identity.target = 0x1234;
    player_target_hold_resident.identity.target_runtime = 42;
    assert(player_camera_native_target(&target) && target.runtime == 42);
    hold_ok = 0;
    assert(!player_camera_native_target(&target));
    hold_ok = 1; resolve_ok = 0;
    assert(!player_camera_native_target(&target));
    resolve_ok = 1; player_target_hold_resident.identity.target_runtime = 43;
    assert(!player_camera_native_target(&target));
    assert(player_camera_native_current() == 42);
    return 0;
}
