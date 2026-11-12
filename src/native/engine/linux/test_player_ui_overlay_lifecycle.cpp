#include <cassert>
#include <csetjmp>
#include <stdexcept>
#include "player_ui_overlay_lifecycle.h"

struct fixture {
    uint64_t tid = 42;
    int value = 10;
    int fatal_count = 0;
    bool fail_apply = false;
    bool fail_restore = false;
};
static uint64_t current_tid(void *p) { return static_cast<fixture *>(p)->tid; }
static int apply(void *p) {
    fixture *f = static_cast<fixture *>(p);
    if (f->fail_apply || f->value != 10) return 0;
    f->value = 20;
    return 1;
}
static int restore(void *p) {
    fixture *f = static_cast<fixture *>(p);
    if (f->fail_restore || f->value != 20) return 0;
    f->value = 10;
    return 1;
}
static void fatal(void *p, const char *) { ++static_cast<fixture *>(p)->fatal_count; }
static void setup(player_ui_overlay_lifecycle &scope, fixture &f) {
    player_ui_overlay_ops ops = {&f, current_tid, apply, restore, fatal};
    assert(player_ui_overlay_init(&scope, &ops, 42, 0x1234));
}
static void throwing_callback(player_ui_overlay_lifecycle &scope) {
    assert(player_ui_overlay_enter(&scope, 0x1234));
    throw std::runtime_error("callback escaped");
}
static std::jmp_buf jump_target;
static void jumping_callback(player_ui_overlay_lifecycle &scope) {
    assert(player_ui_overlay_enter(&scope, 0x1234));
    std::longjmp(jump_target, 1);
}
int main() {
    {
        fixture f; player_ui_overlay_lifecycle scope; setup(scope, f);
        assert(player_ui_overlay_enter(&scope, 0x1234) && f.value == 20);
        assert(player_ui_overlay_pause(&scope, 0x1234) && f.value == 10);
        assert(player_ui_overlay_resume(&scope, 0x1234) && f.value == 20);
        assert(player_ui_overlay_return(&scope, 0x1234) && f.value == 10);
        assert(player_ui_overlay_frame_interlock(&scope) && f.fatal_count == 0);
        assert(!player_ui_overlay_enter(&scope, 0x1234) && f.fatal_count == 1);
    }
    {
        fixture f; player_ui_overlay_lifecycle scope; setup(scope, f);
        try { throwing_callback(scope); assert(false); } catch (const std::runtime_error &) {}
        assert(f.value == 20 && !player_ui_overlay_frame_interlock(&scope) && f.fatal_count == 1);
    }
    {
        fixture *f = new fixture;
        player_ui_overlay_lifecycle *scope = new player_ui_overlay_lifecycle;
        setup(*scope, *f);
        if (setjmp(jump_target) == 0) jumping_callback(*scope);
        assert(f->value == 20 && !player_ui_overlay_frame_interlock(scope) && f->fatal_count == 1);
        delete scope;
        delete f;
    }
    {
        fixture f; player_ui_overlay_lifecycle scope; setup(scope, f);
        assert(player_ui_overlay_enter(&scope, 0x1234));
        assert(player_ui_overlay_pause(&scope, 0x1234) && f.value == 10);
        assert(!player_ui_overlay_frame_interlock(&scope) && f.fatal_count == 1);
    }
    {
        fixture f; player_ui_overlay_lifecycle scope; setup(scope, f);
        assert(!player_ui_overlay_frame_interlock(&scope) && f.fatal_count == 1);
    }
    {
        fixture f; player_ui_overlay_lifecycle scope; setup(scope, f);
        assert(!player_ui_overlay_enter(&scope, 0x9999) && f.fatal_count == 1 && f.value == 10);
    }
    {
        fixture f; player_ui_overlay_lifecycle scope; setup(scope, f);
        assert(player_ui_overlay_enter(&scope, 0x1234));
        f.tid = 43;
        assert(!player_ui_overlay_pause(&scope, 0x1234) && f.fatal_count == 1 && f.value == 20);
    }
    {
        fixture f; player_ui_overlay_lifecycle scope; setup(scope, f);
        assert(player_ui_overlay_enter(&scope, 0x1234));
        assert(!player_ui_overlay_enter(&scope, 0x1234) && f.fatal_count == 1);
    }
    {
        fixture f; player_ui_overlay_lifecycle scope; setup(scope, f);
        f.fail_apply = true;
        assert(!player_ui_overlay_enter(&scope, 0x1234) && f.fatal_count == 1 && f.value == 10);
        assert(!player_ui_overlay_frame_interlock(&scope));
    }
    {
        fixture f; player_ui_overlay_lifecycle scope; setup(scope, f);
        assert(player_ui_overlay_enter(&scope, 0x1234));
        f.fail_restore = true;
        assert(!player_ui_overlay_return(&scope, 0x1234) && f.fatal_count == 1);
        assert(!player_ui_overlay_frame_interlock(&scope));
    }
}
