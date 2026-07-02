#include <cassert>
#include "agent_tui/context/ContextManager.hpp"

using namespace agent_tui;

int main() {
    ContextManager cm(10);

    assert(cm.should_compact(9) == true);
    assert(cm.should_compact(5) == false);
    assert(cm.should_compact(10) == true);

    return 0;
}