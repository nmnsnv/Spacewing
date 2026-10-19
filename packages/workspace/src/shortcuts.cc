#include "shortcuts.h"
#include "result.h"
#include <algorithm>
#include <cctype>

namespace spacewing::workspace {
namespace {
std::string trim(std::string value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return {};
    return value.substr(start, value.find_last_not_of(" \t\r\n") - start + 1);
}
} // namespace
std::string Shortcut::text() const {
    if (!enabled())
        return "";
    std::string result;
    if (modifiers & Control)
        result += "ctrl+";
    if (modifiers & Shift)
        result += "shift+";
    if (modifiers & Option)
        result += "option+";
    if (modifiers & CommandKey)
        result += "cmd+";
    return result + (key == 123 ? "left" : "right");
}

Shortcut parseShortcut(const std::string &text) {
    auto value = trim(text);
    if (value.empty())
        return {};
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    Shortcut shortcut;
    size_t begin = 0;
    while (begin <= value.size()) {
        size_t end = value.find('+', begin);
        std::string part = trim(value.substr(begin, end == std::string::npos ? end : end - begin));
        unsigned modifier = 0;
        if (part == "ctrl" || part == "control")
            modifier = Control;
        else if (part == "shift")
            modifier = Shift;
        else if (part == "alt" || part == "option")
            modifier = Option;
        else if (part == "cmd" || part == "command")
            modifier = CommandKey;
        else if (part == "left" || part == "right") {
            if (shortcut.key)
                throw Error("A shortcut needs exactly one arrow key: " + text, 78);
            shortcut.key = part == "left" ? 123 : 124;
        } else
            throw Error("Unknown shortcut component: " + part, 78);
        if (modifier && (shortcut.modifiers & modifier))
            throw Error("Repeated modifier in shortcut: " + text, 78);
        shortcut.modifiers |= modifier;
        if (end == std::string::npos)
            break;
        begin = end + 1;
    }
    if (!shortcut.key || !shortcut.modifiers)
        throw Error("Use Left or Right with at least one modifier: " + text, 78);
    return shortcut;
}

BindingSet::BindingSet(Register add, Unregister remove)
    : add_(std::move(add)), remove_(std::move(remove)) {}
BindingSet::~BindingSet() {
    for (const auto &binding : bindings_)
        remove_(binding.id);
}
void BindingSet::apply(const Shortcuts &shortcuts, const std::function<void()> &commit) {
    std::vector<Binding> next;
    std::vector<uint64_t> created;
    try {
        for (size_t i = 0; i < shortcuts.size(); ++i) {
            const auto shortcut = shortcuts[i];
            if (!shortcut.enabled())
                continue;
            auto old = std::find_if(bindings_.begin(), bindings_.end(),
                                    [&](const auto &b) { return b.shortcut == shortcut; });
            if (old != bindings_.end())
                next.push_back({shortcut, old->id, static_cast<Action>(i), old->held});
            else {
                auto id = add_(shortcut);
                created.push_back(id);
                next.push_back({shortcut, id, static_cast<Action>(i)});
            }
        }
        commit();
    } catch (...) {
        for (auto id : created)
            remove_(id);
        throw;
    }
    for (const auto &old : bindings_)
        if (std::none_of(next.begin(), next.end(), [&](const auto &b) { return b.id == old.id; }))
            remove_(old.id);
    bindings_ = std::move(next);
}
std::optional<Action> BindingSet::pressed(uint64_t id) {
    for (auto &b : bindings_)
        if (b.id == id) {
            if (b.held)
                return {};
            b.held = true;
            return b.action;
        }
    return {};
}
void BindingSet::released(uint64_t id) {
    for (auto &b : bindings_)
        if (b.id == id)
            b.held = false;
}
void BindingSet::resetPressed() {
    for (auto &b : bindings_)
        b.held = false;
}
std::optional<Shortcut> BindingSet::chord(uint64_t id) const {
    for (const auto &b : bindings_)
        if (b.id == id)
            return b.shortcut;
    return {};
}
} // namespace spacewing::workspace
