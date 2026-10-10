#include "test_harness.h"
#include "gbaimp/hooks.h"

using namespace gbaimp;

int main() {
    // Defaults are all unset and safe to use.
    CHECK(!hooks().save_settings);
    CHECK(!hooks().is_input_blocked);
    CHECK(!hooks().frame_counter);
    notify_settings_changed();          // must not crash with no callback
    CHECK(!input_blocked());

    // hooks() returns a stable singleton.
    CHECK(&hooks() == &hooks());

    // Callbacks are honored once set.
    int saved = 0;
    hooks().save_settings = [&] { ++saved; };
    notify_settings_changed();
    notify_settings_changed();
    CHECK_EQ(saved, 2);

    bool blocked = false;
    hooks().is_input_blocked = [&] { return blocked; };
    CHECK(!input_blocked());
    blocked = true;
    CHECK(input_blocked());

    hooks().frame_counter = [] { return uint64_t(77); };
    CHECK_EQ(hooks().frame_counter(), uint64_t(77));

    // Clearing restores the safe defaults.
    hooks() = Hooks{};
    CHECK(!input_blocked());
    notify_settings_changed();
    CHECK_EQ(saved, 2);

    return testh::finish("hooks");
}
