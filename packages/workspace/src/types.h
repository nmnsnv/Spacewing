#pragma once
#include <array>

namespace spacewing::workspace {
enum class Action : unsigned { Left, Right, DragLeft, DragRight };
struct ActionDefinition {
    const char *setting;
    const char *label;
    const char *defaultChord;
};
inline constexpr std::array actionDefinitions{
    ActionDefinition{"left", "Switch left", "ctrl+shift+option+left"},
    ActionDefinition{"right", "Switch right", "ctrl+shift+option+right"},
    ActionDefinition{"drag_left", "Carry window left", "ctrl+shift+cmd+left"},
    ActionDefinition{"drag_right", "Carry window right", "ctrl+shift+cmd+right"},
};
inline constexpr auto actionNames = [] {
    std::array<const char *, actionDefinitions.size()> names{};
    for (std::size_t i = 0; i < names.size(); ++i)
        names[i] = actionDefinitions[i].setting;
    return names;
}();
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
