/* Engine-free fake image/maps; no native getter, write, reference or hook calls. */
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define PATH_MAX 4096
#define SKO_SELECTED 15u

typedef struct {
    int ready;
    const char *reason;
    uintptr_t universe, player, controller, ship, input_owner, controller_owner;
    uintptr_t player_vtable, controller_vtable, ship_vtable;
    uint64_t player_runtime, player_saved, runtime, saved, autopilot;
    uint32_t count;
    unsigned char context_cf0, requested_space, flight, enabled, ignore, ship_d1, controller_d9;
    unsigned char mouse[3], keys[4];
    float axes[3], cursor[3];
} stock_keyboard_identity_sample;
typedef struct {
    const char *name;
    unsigned va, length;
    const char *sha256;
} lx_site;
typedef struct {
    unsigned active, owned, uncertain, pending_readback, wrapper_depth, overflow;
} sko_status;
typedef struct {
    unsigned nil;
    unsigned number;
    char text[1400];
} lua_State;
static struct {
    unsigned active;
    uint64_t issued;
} stats = {1, 2};
static struct {
    atomic_uint active;
} sko_session;
static sko_status observer;
static unsigned boot_phase, path_installed, stock_observer_post_join_boundary;
static unsigned stock_observer_worker_thread, main_thread, ui_thread, stock_observer_install_tick;
static atomic_uint stock_observer_requested, stock_observer_input_depth,
    stock_observer_thread_failure;
static unsigned stock_observer_failed;
static unsigned worker_thread, worker_tls, slot_valid, request_boundary;
static unsigned pin_valid, identity_valid, identity_calls, identity_change;
static unsigned map_calls, map_fail_va, map_bad_permissions_va, map_bad_path_va;
static unsigned value_pass, value_change, value_fail_va, shared_change, zone_change,
    zone_runtime_change;
static unsigned pin_calls, read_calls, native_flags;
static unsigned char loading, reentry, locks[3];
static uint32_t buffer_index;
static uintptr_t zone;
static uint64_t zone_runtime;
static unsigned zone_read_fail, zone_unknown;
static unsigned char values[0x3e4];
static stock_keyboard_identity_sample identity;
#define LX(va) ((void *)(uintptr_t)(va))
#define STOCK_OBSERVER_NATIVE_FLAGS 0x3daab04u

static unsigned GetCurrentThreadId(void) {
    return worker_thread;
}
static void *gate_tls_self(void) {
    return (void *)(uintptr_t)worker_tls;
}
static unsigned gate_tls_worker(void *pointer) {
    return pointer != NULL;
}
static sko_status sko_get_status(void) {
    return observer;
}
static int stock_observer_slot_valid(void) {
    return slot_valid;
}
static int stock_observer_worker_boundary(void) {
    return request_boundary;
}
static int stock_keyboard_identity_collect(stock_keyboard_identity_sample *sample) {
    ++identity_calls;
    memcpy(sample, &identity, sizeof(*sample));
    if (identity_change && identity_calls == 2) {
        sample->input_owner += 8;
    }
    return identity_valid;
}
static int site_ok(const lx_site *pin) {
    assert(pin->va != 0x119d112);
    assert(pin->length && strlen(pin->sha256) == 64);
    ++pin_calls;
    return pin_valid;
}
static int gate_exe_realpath(char *path) {
    strcpy(path, "/fake/X4");
    return 1;
}
static int gate_maps_find(uintptr_t address, char permissions[5], char *path, size_t capacity) {
    (void)capacity;
    ++map_calls;
    if (address == map_fail_va) {
        return 0;
    }
    strcpy(permissions, address == map_bad_permissions_va ? "rwxp" : "rw-p");
    strcpy(path, address < 0x3da9000 ? "/fake/X4" : "");
    if (address == map_bad_path_va) {
        strcpy(path, "/foreign");
    }
    return 1;
}
static int read_local(const void *address, void *output, size_t length);
static uint64_t world_identity_lookup(uint64_t runtime) {
    return zone_unknown ? 0 : runtime + 0x1000;
}
static void p_lua_pushnil(lua_State *state) {
    state->nil = 1;
}
static void p_lua_pushnumber(lua_State *state, double value) {
    state->number = (unsigned)value;
}
static void p_lua_pushlstring(lua_State *state, const char *text, size_t length) {
    assert(length < sizeof(state->text));
    memcpy(state->text, text, length);
    state->text[length] = 0;
}
#include "gate_player_control_bank_census.h"

static int read_local(const void *address, void *output, size_t length) {
    uintptr_t va = (uintptr_t)address;
    ++read_calls;
    if (va == STOCK_OBSERVER_NATIVE_FLAGS) {
        memcpy(output, &native_flags, length);
        return 1;
    }
    if (va == 0x3daab00) {
        memcpy(output, &loading, length);
        return 1;
    }
    if (va == zone + 8) {
        if (zone_runtime_change && value_pass == 2) {
            ++zone_runtime;
        }
        if (zone_read_fail) {
            return 0;
        }
        memcpy(output, &zone_runtime, length);
        return 1;
    }
    if (va == 0x3e4cdd8) {
        if (zone_change && value_pass == 2) {
            zone += 0x100;
        }
        memcpy(output, &zone, length);
        return 1;
    }
    if (va == 0x3ee9b40) {
        memcpy(output, &reentry, length);
        return 1;
    }
    if (va == 0x3e5b298) {
        if (shared_change && value_pass == 2) {
            locks[0] ^= 1;
        }
        memcpy(output, locks, length);
        return 1;
    }
    if (va == 0x3d8e254) {
        memcpy(output, &buffer_index, length);
        return 1;
    }
    for (unsigned region_index = 0; region_index < PCB_REGION_COUNT; ++region_index) {
        const pcb_region *region = &pcb_regions[region_index];
        if (va == region->va) {
            assert(length == region->length);
            if (va == value_fail_va) {
                return 0;
            }
            if (region_index == 0) {
                ++value_pass;
                if (value_change && value_pass == 2) {
                    values[0x200] ^= 0x40; /* Opaque bytes still stability checked. */
                }
            }
            memcpy(output, values + region->offset, length);
            return 1;
        }
    }
    assert(!"unexpected fake read");
    return 0;
}

static void setup(void) {
    boot_phase = 3;
    path_installed = 1;
    stock_observer_post_join_boundary = 1;
    stock_observer_worker_thread = worker_thread = ui_thread = 22;
    main_thread = 11;
    stock_observer_install_tick = 1;
    stock_observer_requested = 1;
    sko_session.active = 1;
    worker_tls = slot_valid = request_boundary = pin_valid = identity_valid = 1;
    observer = (sko_status){1, (1u << SKO_SELECTED) - 1, 0, 0, 0, 0};
    memset(&identity, 0, sizeof(identity));
    identity.ready = 1;
    identity.reason = "ready";
    identity.universe = 0x8100;
    identity.player = 0x8200;
    identity.controller = 0x8300;
    identity.ship = identity.controller_owner = 0x8400;
    identity.input_owner = 0x8500;
    identity.player_saved = 111;
    identity.saved = 222;
    zone = UINT64_C(0x800000000);
    zone_runtime = 333;
    for (unsigned byte_index = 0; byte_index < PCB_VALUE_BYTES; ++byte_index) {
        values[byte_index] = (unsigned char)(byte_index * 31 + 7);
    }
}

static int fixture_boundary(void *context) {
    (void)context;
    return request_boundary;
}
static pcb_census_operations fixture_operations(void) {
    return (pcb_census_operations){
        NULL,
        fixture_boundary,
        player_bank_census_pins,
        player_bank_census_mapped,
        player_bank_census_read,
        player_bank_census_identity,
        player_bank_census_zone_identity,
        player_bank_census_saved,
    };
}

static void assert_observed(pcb_census_sample *sample) {
    assert(sample->complete && sample->stable && sample->mapped_regions == PCB_REGION_COUNT);
    assert(!memcmp(sample->values, values, PCB_VALUE_BYTES));
    assert(sample->digest_bytes && sample->digest_bytes < PCB_VALUE_BYTES);
    assert(sample->zone_observed == zone && sample->zone_runtime == zone_runtime);
    assert(sample->zone_identity_known && sample->zone_saved == zone_runtime + 0x1000);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    setup();
    const char *scenario = argv[1];
    pcb_census_operations operations = fixture_operations();
    pcb_census_sample sample;
    if (!strcmp(scenario, "off")) {
        assert(player_control_bank_census_after_join());
        assert(!pin_calls && !read_calls && !identity_calls);
        return 0;
    }
    if (!strcmp(scenario, "request_refused")) {
        lua_State state = {0};
        request_boundary = 0;
        lua_player_control_bank_census_request(&state);
        assert(!state.number && !player_bank_census_requested && !read_calls);
        return 0;
    }
    if (!strcmp(scenario, "native_once")) {
        lua_State state = {0};
        lua_player_control_bank_census_request(&state);
        assert(state.number);
        stats.issued = 1;
        assert(player_control_bank_census_after_join());
        assert(!read_calls);
        stats.issued = 2;
        assert(player_control_bank_census_after_join());
        assert_observed(&player_bank_census_snapshot);
        unsigned calls = read_calls;
        assert(player_control_bank_census_after_join() && read_calls == calls);
        lua_player_control_bank_census(&state);
        assert(strstr(state.text, "complete=1 stable=1"));
        assert(strstr(state.text, " captured_tick=2 "));
        assert(!strstr(state.text, " tick="));
        assert(strstr(state.text, "zone_owned=0"));
        assert(strstr(state.text, "digest_kind=fnv_numeric_subset"));
        return 0;
    }
    if (!strncmp(scenario, "boundary_", 9)) {
        player_bank_census_requested = 1;
        player_bank_census_tick = stats.issued;
        assert(player_bank_census_boundary(NULL));
        if (!strcmp(scenario, "boundary_thread")) {
            worker_thread = main_thread;
        } else if (!strcmp(scenario, "boundary_tls")) {
            worker_tls = 0;
        } else if (!strcmp(scenario, "boundary_join")) {
            stock_observer_post_join_boundary = 0;
        } else if (!strcmp(scenario, "boundary_depth")) {
            stock_observer_input_depth = 1;
        } else if (!strcmp(scenario, "boundary_wrapper")) {
            observer.wrapper_depth = 1;
        } else if (!strcmp(scenario, "boundary_owned")) {
            observer.owned ^= 1;
        } else if (!strcmp(scenario, "boundary_pending")) {
            observer.pending_readback = 1;
        } else if (!strcmp(scenario, "boundary_flags")) {
            native_flags = 2;
        } else if (!strcmp(scenario, "boundary_loading")) {
            loading = 1;
        } else if (!strcmp(scenario, "boundary_slot")) {
            slot_valid = 0;
        } else if (!strcmp(scenario, "boundary_tick")) {
            ++stats.issued;
        } else {
            assert(!"unknown boundary scenario");
        }
        assert(!player_bank_census_boundary(NULL));
        return 0;
    }
    if (!strcmp(scenario, "digest_pointer_exclusion")) {
        assert(pcb_census_collect(&sample, &operations));
        uint64_t original = sample.numeric_digest;
        values[0xb8] ^= 0x71;  /* Autopilot qword excluded. */
        values[0x200] ^= 0x55; /* Opaque controller state excluded. */
        values[0x300] ^= 0x33; /* Unresolved coefficient tables excluded. */
        identity.player += 8;
        zone += 0x100;
        identity_calls = value_pass = 0;
        assert(pcb_census_collect(&sample, &operations));
        assert(sample.numeric_digest == original);
        values[0xc8] ^= 1; /* Proven STATE2 byte included. */
        identity_calls = value_pass = 0;
        assert(pcb_census_collect(&sample, &operations));
        assert(sample.numeric_digest != original);
        return 0;
    }
    if (!strcmp(scenario, "neutral_not_required")) {
        identity.ready = 0;
        identity.reason = "controls_disabled";
        locks[0] = 1;
        assert(pcb_census_collect(&sample, &operations));
        assert_observed(&sample);
        assert(!sample.context_ready);
        return 0;
    }
    if (!strcmp(scenario, "zone_unknown")) {
        zone_unknown = 1;
        assert(pcb_census_collect(&sample, &operations));
        assert(sample.complete && !sample.zone_identity_known && !sample.zone_saved);
        return 0;
    }
    if (!strcmp(scenario, "null_zone")) {
        zone = 0;
        assert(pcb_census_collect(&sample, &operations));
        assert(sample.complete && !sample.zone_observed && !sample.zone_identity_known);
        return 0;
    }
    if (!strcmp(scenario, "observe")) {
        assert(pcb_census_collect(&sample, &operations));
        assert_observed(&sample);
        assert(sample.context_ready);
        return 0;
    }
    if (!strcmp(scenario, "mapping_cross_page")) {
        map_fail_va = 0x3e5b000;
    } else if (!strcmp(scenario, "mapping_permissions")) {
        map_bad_permissions_va = 0x3d8d9a0;
    } else if (!strcmp(scenario, "mapping_owner")) {
        map_bad_path_va = 0x3d8d9a0;
    } else if (!strcmp(scenario, "pins")) {
        pin_valid = 0;
    } else if (!strcmp(scenario, "identity")) {
        identity_valid = 0;
    } else if (!strcmp(scenario, "identity_change")) {
        identity_change = 1;
    } else if (!strcmp(scenario, "value_change")) {
        value_change = 1;
    } else if (!strcmp(scenario, "value_read")) {
        value_fail_va = 0x3e5afa0;
    } else if (!strcmp(scenario, "shared_change")) {
        shared_change = 1;
    } else if (!strcmp(scenario, "reentry")) {
        reentry = 1;
    } else if (!strcmp(scenario, "zone_read")) {
        zone_read_fail = 1;
    } else if (!strcmp(scenario, "zone_change")) {
        zone_change = 1;
    } else if (!strcmp(scenario, "zone_runtime_change")) {
        zone_runtime_change = 1;
    } else {
        assert(!"unknown scenario");
    }
    assert(!pcb_census_collect(&sample, &operations));
    assert(!sample.complete && !sample.stable && !sample.numeric_digest);
    return 0;
}
