#pragma once

#include <array>
#include <compare>
#include <string>

namespace spacewing::switcher {
enum Modifier : unsigned { Control = 1, Shift = 2, Option = 4, CommandKey = 8 };
// Values are macOS virtual key codes, including zero for A.
enum class Key : unsigned {
    Tab = 48,
    Grave = 50,
    Escape = 53,
    Down = 125,
    Up = 126,
    Other = 0xffff
};
struct Shortcut {
    unsigned modifiers = 0;
    Key key = Key::Other;
    bool enabled() const { return key != Key::Other; }
    auto operator<=>(const Shortcut &) const = default;
    std::string text() const;
};
// Empty disables a binding. Otherwise requires a modifier and a supported key.
// Escape and Up/Down belong to the picker.
Shortcut parseShortcut(const std::string &text);

enum class Mode { AllWindows, ApplicationWindows };
struct ShortcutAction {
    const char *setting;
    const char *label;
    const char *defaultChord;
    const char *cliOption;
    Mode mode;
};
// The binding schema is shared by configuration, Settings, CLI and dispatch.
// Adding an action here automatically exposes its binding in every host surface.
inline constexpr std::array shortcutActions{
    ShortcutAction{"shortcut", "All windows shortcut", "option+tab", "--shortcut",
                   Mode::AllWindows},
    ShortcutAction{"application_shortcut", "Current app shortcut", "option+grave",
                   "--application-shortcut", Mode::ApplicationWindows},
};
using Shortcuts = std::array<Shortcut, shortcutActions.size()>;
Shortcuts defaultShortcuts();
void validateShortcuts(const Shortcuts &shortcuts);
bool matchesShortcut(Shortcut shortcut, Key key, unsigned modifiers);
} // namespace spacewing::switcher
