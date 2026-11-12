/* Bounded native-adapter guards exercised with synthetic private mappings. */
#define _GNU_SOURCE
#include <assert.h>
#include <stdarg.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <unistd.h>

/* Verify the actual promoted variadic argument words before forwarding them
 * to the kernel. In the child, the target must be its own PID. */
static unsigned checked_read_calls, checked_write_calls;
static long fixture_presentation_syscall(long number, ...) {
    va_list args;
    va_start(args, number);
    long pid = va_arg(args, long);
    struct iovec *local = va_arg(args, struct iovec *);
    unsigned long local_count = va_arg(args, unsigned long);
    struct iovec *remote = va_arg(args, struct iovec *);
    unsigned long remote_count = va_arg(args, unsigned long);
    unsigned long flags = va_arg(args, unsigned long);
    va_end(args);
    assert(pid == syscall(SYS_getpid));
    assert(local_count == 1UL && remote_count == 1UL && flags == 0UL);
    assert(number == SYS_process_vm_readv || number == SYS_process_vm_writev);
    if (number == SYS_process_vm_readv) ++checked_read_calls;
    else ++checked_write_calls;
    return syscall(number, pid, local, local_count, remote, remote_count, flags);
}
#define PRESENTATION_FORK_SYSCALL fixture_presentation_syscall
#define GATE_TLS_TEST
#include "gate_linux.c"

static __thread unsigned char fixture_tls[0x2800] __attribute__((aligned(16)));
static unsigned char *gate_tls_test_base(void) { return fixture_tls + sizeof fixture_tls; }

static void *private_page(size_t size) {
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(p != MAP_FAILED);
    return p;
}

static void put_pointer(void *base, size_t offset, uintptr_t value) {
    memcpy((unsigned char *)base + offset, &value, sizeof value);
}

static void pin_diagnostic_fixture(const char *path) {
    static const uintptr_t code[][2] = {
        {0xa44e40, 0x53}, {0x169af50, 0x8a}, {0x1699f20, 0x6b}, {0x189d8f0, 0x37},
        {0x189f8d0, 0xae}, {0xa14540, 0x25}, {0x136c820, 68}, {0xa389f0, 48},
        {0xa398e0, 48}, {0xa39930, 48}, {0x2c822bc, 4}, {0x2c81d04, 4},
        {0x2c81d1c, 4}, {0x2c81ce4, 4}, {0x2c81cfc, 4}
    };
    static const uintptr_t slots[][2] = {
        {0x3b61480, 0x136c820}, {0x3d22938, 0xa389f0},
        {0x3cb6078, 0xa398e0}, {0x3ca71b0, 0xa39930}
    };
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    assert(fd >= 0);
    gate_env.exe = path;
    for (unsigned i = 0; i < sizeof code / sizeof code[0]; ++i)
        assert(pread(fd, LX(code[i][0]), code[i][1], code[i][0] - 0x400000) == (ssize_t)code[i][1]);
    close(fd);
    for (unsigned i = 0; i < sizeof slots / sizeof slots[0]; ++i)
        put_pointer(LX(slots[i][0]), 0, (uintptr_t)LX(slots[i][1]));
    uint64_t (*occupied)(void) = NULL;
    assert(presentation_fork_pins(&occupied));
    assert(occupied == (uint64_t (*)(void))LX(0xa44e40));
    *(unsigned char *)LX(0x136c820) ^= 1;
    assert(!presentation_fork_pins(&occupied));
    *(unsigned char *)LX(0x136c820) ^= 1;
    put_pointer(LX(0x3d22938), 0, 0);
    assert(!presentation_fork_pins(&occupied));
    put_pointer(LX(0x3d22938), 0, (uintptr_t)LX(0xa389f0));
    *(unsigned char *)LX(0x2c81d1c) ^= 1;
    assert(!presentation_fork_pins(&occupied));
    *(unsigned char *)LX(0x2c81d1c) ^= 1;
    assert(presentation_fork_pins(&occupied));
}

int main(int argc, char **argv) {
    /* Reservation is made on the same worker allowed to arm a save. */
    stats.active = 1;
    stats.issued = 7;
    ui_thread = GetCurrentThreadId();
    *(int32_t *)(gate_tls_self() + GATE_TLS_LINUX_VIEW) = 1;
    fs.state = FS_OFF;
    atomic_store(&presentation_fork_reservation, 1);
    assert(!fork_save_arm(8, "join", "/tmp", 50, 5));
    assert(fs.state == FS_OFF);
    atomic_store(&presentation_fork_reservation, 0);
    assert(fork_save_arm(8, "join", "/tmp", 50, 5));
    assert(fs.state == FS_ARMED);
    fs.state = FS_OFF;

    image = private_page(0x4000000);
    if (argc == 2) pin_diagnostic_fixture(argv[1]);
    void *player = private_page(0x7000);
    void *room = private_page(0x1000);
    void *ship = private_page(0x1000);
    uintptr_t *vector = private_page(0x1000);
    vector[0] = (uintptr_t)ship;
    player_presentation_vector controlled = {(uintptr_t)vector, (uintptr_t)(vector + 1), (uintptr_t)(vector + 2)};
    put_pointer(room, 0, (uintptr_t)LX(SEAT_PREFLIGHT_COCKPIT_VTABLE));
    put_pointer(room, 0x70, (uintptr_t)ship);
    put_pointer(ship, 0, (uintptr_t)LX(SEAT_PREFLIGHT_SHIP_VTABLE));
    uint64_t runtime = 12345;
    memcpy((unsigned char *)ship + 8, &runtime, 8);
    put_pointer(player, 0x70, (uintptr_t)room);
    put_pointer(player, 0xe0, (uintptr_t)room);
    memcpy((unsigned char *)player + 0x308, &controlled, sizeof controlled);
    int fd = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
    assert(fd >= 0);
    static presentation_fork_maps maps;
    assert(presentation_fork_maps_read(fd, &maps));
    close(fd);
    native_presentation_fork probe = {0};
    probe.request.bank = 0;
    assert(presentation_fork_chain(&probe, &maps, (uintptr_t)player, (uintptr_t)room, (uintptr_t)ship, runtime, 1));
    put_pointer(player, 0xe0, 0);
    assert(!presentation_fork_chain(&probe, &maps, (uintptr_t)player, (uintptr_t)room, (uintptr_t)ship, runtime, 1));
    /* Unpublished P2 cache is allowed: the child overlays P1's cache itself. */
    assert(presentation_fork_chain(&probe, &maps, (uintptr_t)player, (uintptr_t)room, (uintptr_t)ship, runtime, 0));
    vector[1] = (uintptr_t)room;
    controlled.end = (uintptr_t)(vector + 2);
    memcpy((unsigned char *)player + 0x308, &controlled, sizeof controlled);
    assert(!presentation_fork_chain(&probe, &maps, (uintptr_t)player, (uintptr_t)room, (uintptr_t)ship, runtime, 0));
    /* Verify a write targets the fork child, never the original parent page. */
    int child_value = 71;
    pid_t child = fork();
    assert(child >= 0);
    if (!child) {
        int replacement = 99, observed = 0;
        assert(presentation_fork_write(NULL, (uintptr_t)&child_value, &replacement, sizeof replacement));
        assert(presentation_fork_read(NULL, (uintptr_t)&child_value, &observed, sizeof observed));
        _exit(observed == 99 && checked_write_calls == 1 ? 0 : 1);
    }
    int child_status = 0;
    assert(waitpid(child, &child_status, 0) == child);
    assert(WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0 && child_value == 71);
    assert(checked_read_calls && !checked_write_calls);
    /* The owner check runs both before and after the child. A retained object
     * or controller backlink changing must refuse without touching P1. */
    void *player2 = private_page(0x7000);
    void *universe = private_page(0x1000);
    void *controller = private_page(0x1000);
    put_pointer(LX(FACTORY_PROBE_UNIVERSE_VA), 0, (uintptr_t)universe);
    put_pointer(universe, 0x238, (uintptr_t)player);
    put_pointer(player, 0, (uintptr_t)LX(SEAT_PREFLIGHT_PLAYER_VTABLE));
    put_pointer(player2, 0, (uintptr_t)LX(SEAT_PREFLIGHT_PLAYER_VTABLE));
    put_pointer(ship, 0x200, (uintptr_t)controller);
    put_pointer(controller, 0x20, (uintptr_t)ship);
    put_pointer(controller, 0, (uintptr_t)&player_physics_routes.routes.table[2]);
    uint32_t refs = 3, marker = 2;
    memcpy((unsigned char *)ship + 0x40, &refs, 4);
    memcpy((unsigned char *)ship + 0x44, &marker, 4);
    memcpy((unsigned char *)room + 0x40, &refs, 4);
    memcpy((unsigned char *)room + 0x44, &marker, 4);
    probe.request.player1 = (uintptr_t)player;
    probe.request.player2 = (uintptr_t)player2;
    probe.request.room = (uintptr_t)room;
    probe.universe = (uintptr_t)universe;
    probe.p2_ship = (uintptr_t)ship;
    probe.controller = (uintptr_t)controller;
    factory_probe_state.player2_owned = 1;
    factory_probe_state.player2 = (uintptr_t)player2;
    atomic_store(&player_physics_ready, 1);
    player_target_hold_resident.target_owned = 1;
    player_target_hold_resident.room_owned = 1;
    player_target_hold_resident.target_reference = (uintptr_t)ship;
    player_target_hold_resident.room_reference = (uintptr_t)room;
    player_target_hold_resident.identity.target = (uintptr_t)ship;
    player_target_hold_resident.identity.room = (uintptr_t)room;
    assert(presentation_fork_owner(&probe, (uintptr_t)player, (uintptr_t)player2, (uintptr_t)room, 0));
    put_pointer(controller, 0x20, 0);
    assert(!presentation_fork_owner(&probe, (uintptr_t)player, (uintptr_t)player2, (uintptr_t)room, 0));
    put_pointer(controller, 0x20, (uintptr_t)ship);
    marker = 1; memcpy((unsigned char *)room + 0x44, &marker, 4);
    assert(!presentation_fork_owner(&probe, (uintptr_t)player, (uintptr_t)player2, (uintptr_t)room, 0));

    puts("presentation fork native reservation and graph PASS");
    return 0;
}
