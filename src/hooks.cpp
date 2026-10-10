#include "gbaimp/hooks.h"

namespace gbaimp {

Hooks& hooks() {
    static Hooks s_hooks;
    return s_hooks;
}

} // namespace gbaimp
