#pragma once

#include "shortcut.h"
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace spacewing::switcher {
enum class Scope { CurrentDesktop, AllDesktops };
enum class Layout { List, Grid };
struct Options {
    bool enabled = true;
    Scope scope = Scope::CurrentDesktop;
    Shortcuts shortcuts = defaultShortcuts();
    Layout layout = Layout::Grid;
    auto operator<=>(const Options &) const = default;
};
struct WindowID {
    int pid = 0;
    uint32_t number = 0;
    auto operator<=>(const WindowID &) const = default;
};
struct Candidate {
    WindowID id;
    std::string application;
    std::string title;
};
enum class Direction { Left, Right, Up, Down };

// A frozen ordering for one modifier hold. No platform or desktop-manager objects.
class Session {
public:
    void begin(std::vector<Candidate> windows, std::optional<WindowID> focused);
    void step(bool reverse);
    void move(Direction direction, size_t columns);
    void cancel();
    std::optional<WindowID> commit();
    const std::vector<Candidate> &windows() const { return windows_; }
    size_t selected() const { return selected_; }
    bool active() const { return !windows_.empty(); }

private:
    std::vector<Candidate> windows_;
    size_t selected_ = 0;
};

enum class InputType { Down, Up, Modifiers };
enum class Intent { None, Forward, Backward, Left, Right, Up, Down, Commit, Cancel };
struct Input {
    InputType type;
    Key key = Key::Other;
    unsigned modifiers = 0;
    bool repeat = false;
};
struct Decision {
    bool consume = false;
    Intent intent = Intent::None;
    Mode mode = Mode::AllWindows;
};
// Runs synchronously in the event tap; native work runs outside the callback.
class Keyboard {
public:
    Decision handle(Input event, bool blocked);
    void configure(const Shortcuts &shortcuts, Layout layout = Layout::Grid);
    void cancel() { active_ = false; }
    void reset();
    bool active() const { return active_; }

private:
    bool active_ = false;
    Layout layout_ = Layout::Grid;
    // No implicit bindings: only configure() can enable a shortcut.
    Shortcuts shortcuts_{};
    Shortcut activeShortcut_;
    Mode mode_ = Mode::AllWindows;
    std::array<bool, 128> held_{};
};
} // namespace spacewing::switcher
