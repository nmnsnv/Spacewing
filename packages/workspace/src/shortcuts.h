#pragma once
#include "types.h"
#include <compare>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace spacewing::workspace {
enum Modifier : unsigned { Control = 1, Shift = 2, Option = 4, CommandKey = 8 };
struct Shortcut {
    unsigned modifiers = 0;
    unsigned key = 0; // macOS virtual key code; 123 = left, 124 = right.
    bool enabled() const { return key != 0; }
    auto operator<=>(const Shortcut &) const = default;
    std::string text() const;
};
Shortcut parseShortcut(const std::string &text);

using Shortcuts = std::array<Shortcut, actionNames.size()>;

// Reuse existing registrations, including when actions swap chords. Roll back only new refs.
class BindingSet {
public:
    using Register = std::function<uint64_t(Shortcut)>;
    using Unregister = std::function<void(uint64_t)>;
    BindingSet(Register add, Unregister remove);
    ~BindingSet();
    void apply(const Shortcuts &shortcuts, const std::function<void()> &commit = [] {});
    std::optional<Action> pressed(uint64_t id);
    void released(uint64_t id);
    void resetPressed();
    std::optional<Shortcut> chord(uint64_t id) const;

private:
    struct Binding {
        Shortcut shortcut;
        uint64_t id;
        Action action;
        bool held = false;
    };
    Register add_;
    Unregister remove_;
    std::vector<Binding> bindings_;
};
} // namespace spacewing::workspace
