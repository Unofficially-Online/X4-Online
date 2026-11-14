#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <sys/mman.h>
#include "player_view_pin_map_batch.h"

int main(void) {
    const char maps[] = "1000-2000 r-xp 00000000 00:01 10 /first\n"
                        "3000-4000 rw-p 00000000 00:00 0 \n";
    player_view_map_entry index[2];
    size_t count = 0;
    char permissions[5], path[PATH_MAX], line[PATH_MAX + 128];
    assert(player_view_map_index(maps, sizeof(maps) - 1, index, &count) && count == 2);
    assert(player_view_map_lookup(maps, index, count, 0x1000, permissions, path, line));
    assert(!strcmp(permissions, "r-xp") && !strcmp(path, "/first"));
    assert(player_view_map_lookup(maps, index, count, 0x3fff, permissions, path, line));
    assert(!strcmp(permissions, "rw-p") && !path[0]);
    assert(!player_view_map_lookup(maps, index, count, 0x2000, permissions, path, line));
    const char malformed[] = "2000-3000 rw-p 0 00:00 0\n1000-2000 rw-p 0 00:00 0\n";
    assert(!player_view_map_index(malformed, sizeof(malformed) - 1, index, &count));

    void *page = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert(page != MAP_FAILED);
    player_view_map_batch batch;
    assert(player_view_map_begin(&batch));
    assert(player_view_map_find(&batch, (uintptr_t)page, permissions, path, sizeof path));
    assert(player_view_map_end(&batch));
    assert(player_view_map_begin(&batch));
    assert(player_view_map_find(&batch, (uintptr_t)page, permissions, path, sizeof path));
    assert(!mprotect(page, 4096, PROT_READ));
    assert(!player_view_map_end(&batch));
    assert(!munmap(page, 4096));
    puts("view map batch PASS");
    return 0;
}
