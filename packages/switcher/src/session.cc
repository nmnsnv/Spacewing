#include "session.h"
#include <algorithm>
#include <set>

namespace spacewing::switcher {
void Session::begin(std::vector<Candidate> windows, std::optional<WindowID> focused) {
    cancel();
    std::set<WindowID> seen;
    for (auto &window : windows)
        if (window.id.pid > 0 && window.id.number && seen.insert(window.id).second)
            windows_.push_back(std::move(window));
    if (!active())
        return;
    const auto current = std::find_if(windows_.begin(), windows_.end(),
                                      [&](const auto &window) { return window.id == focused; });
    if (current != windows_.end())
        std::rotate(windows_.begin(), current, current + 1);
    selected_ = windows_.size() > 1 ? 1 : 0;
}
void Session::step(bool reverse) {
    if (active())
        selected_ = (selected_ + (reverse ? windows_.size() - 1 : 1)) % windows_.size();
}
void Session::move(Direction direction, size_t columns) {
    if (!active() || !columns)
        return;
    const auto column = selected_ % columns;
    const auto rowStart = selected_ - column;
    const auto rowCount = std::min(columns, windows_.size() - rowStart);
    switch (direction) {
    case Direction::Left:
        selected_ = column ? selected_ - 1 : rowStart + rowCount - 1;
        break;
    case Direction::Right:
        selected_ = column + 1 < rowCount ? selected_ + 1 : rowStart;
        break;
    case Direction::Up:
        selected_ = selected_ >= columns
                        ? selected_ - columns
                        : column + (windows_.size() - 1 - column) / columns * columns;
        break;
    case Direction::Down:
        selected_ = columns < windows_.size() - selected_ ? selected_ + columns : column;
        break;
    }
}
void Session::cancel() {
    windows_.clear();
    selected_ = 0;
}
std::optional<WindowID> Session::commit() {
    std::optional<WindowID> result;
    if (active())
        result = windows_[selected_].id;
    cancel();
    return result;
}
Decision Keyboard::handle(Input event, bool blocked) {
    const auto code = static_cast<unsigned>(event.key);
    const bool owned = code < held_.size() && held_[code];
    if (event.type == InputType::Up) {
        if (owned) {
            held_[code] = false;
            return {true, Intent::None};
        }
        return {};
    }
    const bool ownedRepeat = event.type == InputType::Down && event.repeat && owned;
    if (active_ && blocked) {
        active_ = false;
        return {ownedRepeat, Intent::Cancel};
    }
    // A swallowed key-down owns its repeats even after modifier release/cancel.
    const bool arrow =
        event.key == Key::Up || event.key == Key::Down ||
        (layout_ == Layout::Grid && (event.key == Key::Left || event.key == Key::Right));
    if (ownedRepeat && (!active_ || !arrow))
        return {true, Intent::None};
    const bool heldModifiers =
        (event.modifiers & activeShortcut_.modifiers) == activeShortcut_.modifiers;
    const bool extraModifier = (event.modifiers & ~(activeShortcut_.modifiers | Shift)) != 0;
    if (event.type == InputType::Modifiers) {
        if (active_ && extraModifier) {
            active_ = false;
            return {false, Intent::Cancel};
        }
        if (active_ && !heldModifiers) {
            active_ = false;
            return {false, Intent::Commit, mode_};
        }
        return {};
    }
    if (event.key == Key::Escape && (active_ || owned)) {
        held_[code] = true;
        active_ = false;
        return {true, Intent::Cancel};
    }
    for (size_t i = 0; i < shortcuts_.size(); ++i) {
        if (blocked || !matchesShortcut(shortcuts_[i], event.key, event.modifiers))
            continue;
        if (event.repeat)
            return {owned, Intent::None};
        held_[code] = true;
        active_ = true;
        activeShortcut_ = shortcuts_[i];
        mode_ = shortcutActions[i].mode;
        return {true,
                (event.modifiers & Shift) && !(activeShortcut_.modifiers & Shift) ? Intent::Backward
                                                                                  : Intent::Forward,
                mode_};
    }
    if (active_ && heldModifiers && !extraModifier && arrow && (!event.repeat || owned)) {
        held_[code] = true;
        const auto intent = event.key == Key::Up     ? Intent::Up
                            : event.key == Key::Down ? Intent::Down
                            : event.key == Key::Left ? Intent::Left
                                                     : Intent::Right;
        return {true, intent, mode_};
    }
    if (active_) {
        active_ = false;
        return {false, Intent::Cancel};
    }
    return {};
}
void Keyboard::configure(const Shortcuts &shortcuts, Layout layout) {
    validateShortcuts(shortcuts);
    shortcuts_ = shortcuts;
    layout_ = layout;
    cancel();
}
void Keyboard::reset() {
    active_ = false;
    held_.fill(false);
}
} // namespace spacewing::switcher
