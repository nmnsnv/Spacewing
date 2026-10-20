#pragma once

#include <compare>
#include <string>

namespace spacewing::switcher {
enum Modifier : unsigned { Control = 1, Shift = 2, Option = 4, CommandKey = 8 };
// Values are macOS virtual key codes, including zero for A.
enum class Key : unsigned { Tab = 48, Escape = 53, Down = 125, Up = 126, Other = 0xffff };
struct Shortcut {
    unsigned modifiers = Option;
    Key key = Key::Tab;
    auto operator<=>(const Shortcut &) const = default;
    std::string text() const;
};
// Requires a modifier and a supported key. Escape and Up/Down belong to the picker.
Shortcut parseShortcut(const std::string &text);
} // namespace spacewing::switcher
