#include <cassert>
#include "agent_tui/trace/TraceAdapter.hpp"

using namespace agent_tui;

int main() {
    TraceAdapter t;

    SessionEvent e = SessionEvent::tool_call(ToolCall{"1", "list_dir", {}});
    t.on_event(e);

    assert(t.items().size() == 1);
    assert(t.items()[0].type == "tool_call");

    return 0;
}