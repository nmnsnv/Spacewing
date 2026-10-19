#pragma once
#include <array>

namespace spacewing::workspace {
enum class Action : unsigned { Left, Right, DragLeft, DragRight };
inline constexpr std::array<const char *, 4> actionNames = {"left", "right", "drag_left",
                                                            "drag_right"};
inline bool carries(Action action) {
    return action == Action::DragLeft || action == Action::DragRight;
}
inline int direction(Action action) {
    return action == Action::Left || action == Action::DragLeft ? -1 : 1;
}

struct Request {
    Action action = Action::Left;
    bool verify = false;
    bool dryRun = false;
};
} // namespace spacewing::workspace
