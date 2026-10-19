#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace spacewing::switcher {
enum class Scope { CurrentDesktop, AllDesktops };
struct Options {
    bool enabled = true;
    Scope scope = Scope::CurrentDesktop;
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

// A frozen ordering for one modifier hold. No platform or desktop-manager objects.
class Session {
public:
    void begin(std::vector<Candidate> windows, std::optional<WindowID> focused, bool reverse);
    void step(bool reverse);
    void cancel();
    std::optional<WindowID> commit();
    const std::vector<Candidate> &windows() const { return windows_; }
    size_t selected() const { return selected_; }
    bool active() const { return !windows_.empty(); }

private:
    std::vector<Candidate> windows_;
    size_t selected_ = 0;
};

enum class Key { Tab, Escape, Other };
enum class InputType { Down, Up, Modifiers };
enum class Intent { None, Forward, Backward, Commit, Cancel };
struct Input {
    InputType type;
    Key key = Key::Other;
    bool option = false;
    bool shift = false;
    bool otherModifier = false;
    bool repeat = false;
};
struct Decision {
    bool consume = false;
    Intent intent = Intent::None;
};
// Runs synchronously in the event tap; native work runs outside the callback.
class Keyboard {
public:
    Decision handle(Input event, bool blocked);
    void cancel() { active_ = false; }
    void reset();
    bool active() const { return active_; }

private:
    bool active_ = false;
    bool tabDown_ = false;
    bool escapeDown_ = false;
};
} // namespace spacewing::switcher
