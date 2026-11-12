#include <cassert>
#include <cstdint>
extern "C" uintptr_t live_pose_throw_original(void *, void *, unsigned, double) {
    throw 7;
}
extern "C" void live_pose_test_throw();
extern "C" int live_pose_test_refuses_after_unwind();
int main() {
    try {
        live_pose_test_throw();
        assert(false);
    } catch (int value) {
        assert(value == 7);
    }
    assert(live_pose_test_refuses_after_unwind());
}
