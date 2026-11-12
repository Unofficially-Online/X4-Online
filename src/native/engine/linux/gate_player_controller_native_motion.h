#ifndef X4_LINUX_PLAYER_CONTROLLER_NATIVE_MOTION_H
#define X4_LINUX_PLAYER_CONTROLLER_NATIVE_MOTION_H
/* Linux x86-64 SysV 9.00, pinned ELF 113f73d74570084f6ce18a659b365009eccf579fb66dbabb5dad2b1718045f20.
 * Adapter 0/1/2: preserve exactly the PlayerController getter prologue, reload
 * time as the compiler did after its predicate call, then jump to the FALSE
 * branch. The three prefixes contain no PC-relative operands. The caller must
 * verify the entire pinned executable plus the six FDE hashes in the companion
 * document and compare relocated vtable entries before publishing any vptr.
 * Only this exact ELF is supported. Buffer must be writable when built, then
 * executable and immutable for the entire lifetime of all possible callers.
 * No memory permissions are changed here. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "gate_player_controller_routes.h"
#define PLAYER_NATIVE_STUB_SIZE 64u
#define PLAYER_NATIVE_IMAGE_SHA256 "113f73d74570084f6ce18a659b365009eccf579fb66dbabb5dad2b1718045f20"
typedef struct {
    uintptr_t va;
    size_t size;
    const char *sha256;
} player_native_pin;
/* FDE extents determined from readelf -wf on the pinned image. */
static const player_native_pin player_native_fde_pins[] = {
    {0x1659010u, 4479, "d1993184f89d0ccf09c4c075110ab8c0dafa2ff8fefd55d6232c00910235125f"},
    {0x165a190u, 1918, "3da96e3a034b9041fe2eaa0f3e5faae9d67da7e6309f0e2ac637988df0db43f3"},
    {0x165a910u, 317, "41f8df09febc157396d5757f894aac09daaf2d0ed00cd7b707b427d933ae815a"},
    {0x141d2c0u, 120, "9703bb7527be562daa65072b23f0f205d1fac35781562a29285d369f5928aeae"},
    {0x144a470u, 10, "4923c6d9fc6b233f49d121dc26708156b163b9f9382d17bd6ee9b7e8d0f3d066"},
    {0x142a600u, 10, "700e4065545e5d870d1231a1f901ff4ca0cb3cdaa4518289f2a209733c4ec5ba"}};
static const unsigned char player_native_head0[] = {
    0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x41, 0x89, 0xd4, 0x55, 0x48, 0x89, 0xf5, 0x53,
    0x48, 0x89, 0xfb, 0x48, 0x81, 0xec, 0x38, 0x01, 0x00, 0x00, 0xf2, 0x0f, 0x11, 0x04, 0x24};
static const unsigned char player_native_head1[] = {0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x41, 0x89, 0xd4,
                                                    0x55, 0x48, 0x89, 0xf5, 0x53, 0x48, 0x89, 0xfb, 0x48,
                                                    0x83, 0xec, 0x50, 0xf2, 0x0f, 0x11, 0x04, 0x24};
static const unsigned char player_native_head2[] = {0x41, 0x54, 0x41, 0x89, 0xd4, 0x55, 0x48, 0x89,
                                                    0xfd, 0x53, 0x48, 0x89, 0xf3, 0x48, 0x83, 0xec,
                                                    0x30, 0xf2, 0x0f, 0x11, 0x44, 0x24, 0x08};
static const unsigned char player_native_reload0[] = {0xf2, 0x0f, 0x10, 0x2c, 0x24};
static const unsigned char player_native_reload1[] = {0xf2, 0x0f, 0x10, 0x04, 0x24};
static const unsigned char player_native_reload2[] = {0xf2, 0x0f, 0x10, 0x44, 0x24, 0x08};
/* Explicit target parameters make the machine-code builder independently
 * testable; in native use they MUST be image+1659041/165a1bc/165a936. */
static int player_native_build_false_branches(unsigned char *buffer, size_t capacity,
                                              const unsigned char *const original[3],
                                              const void *const target[3], player_route_motion adapters[3]) {
    const unsigned char *prologues[] = {player_native_head0, player_native_head1, player_native_head2};
    const unsigned char *reload[] = {player_native_reload0, player_native_reload1, player_native_reload2};
    const size_t prologue_lengths[] = {sizeof player_native_head0, sizeof player_native_head1,
                                       sizeof player_native_head2};
    const size_t reload_lengths[] = {sizeof player_native_reload0, sizeof player_native_reload1,
                                     sizeof player_native_reload2};
    if (!buffer || capacity < 3 * PLAYER_NATIVE_STUB_SIZE || !original || !target || !adapters)
        return 0;
    for (unsigned index = 0; index < 3; index++)
        if (!original[index] || !target[index] ||
            memcmp(original[index], prologues[index], prologue_lengths[index]))
            return 0;
    for (unsigned index = 0; index < 3; index++) {
        unsigned char *stub = buffer + index * PLAYER_NATIVE_STUB_SIZE;
        size_t offset = prologue_lengths[index];
        memset(stub, 0xcc, PLAYER_NATIVE_STUB_SIZE);
        memcpy(stub, prologues[index], offset);
        memcpy(stub + offset, reload[index], reload_lengths[index]);
        offset += reload_lengths[index];
        /* jmp QWORD PTR [rip+0]; followed by full 64-bit address */
        stub[offset++] = 0xff;
        stub[offset++] = 0x25;
        memset(stub + offset, 0, 4);
        offset += 4;
        uintptr_t destination = (uintptr_t)target[index];
        memcpy(stub + offset, &destination, sizeof destination);
        adapters[index] = (player_route_motion)(void *)stub;
    }
    return 1;
}
/* Only call after full-image and FDE validation. bias is runtime mapping base
 * minus the ELF's 0x400000 load address (zero for an unrelocated ET_EXEC). */
static int player_native_bind(uintptr_t bias, unsigned char *buffer, size_t capacity,
                              player_route_motion agents[3], player_route_motion players[3]) {
    if (!agents || !players)
        return 0;
    const unsigned char *original[] = {(const unsigned char *)(bias + 0x1659010u),
                                       (const unsigned char *)(bias + 0x165a190u),
                                       (const unsigned char *)(bias + 0x165a910u)};
    const void *target[] = {(void *)(bias + 0x1659041u), (void *)(bias + 0x165a1bcu),
                            (void *)(bias + 0x165a936u)};
    if (!player_native_build_false_branches(buffer, capacity, original, target, players))
        return 0;
    agents[0] = (player_route_motion)(bias + 0x144a470u);
    agents[1] = (player_route_motion)(bias + 0x142a600u);
    agents[2] = (player_route_motion)(bias + 0x141d2c0u);
    return 1;
}
/* Agent 13/14 are wrappers 144a470/142a600; agent 17 is 141d2c0.
 * The former transfer DL to ECX and zero EDX before tail calls. The latter
 * implements exactly the same null / overwrite / additive branch and selector
 * as PlayerController's true branch 165a998..165aa4d. */
#endif
