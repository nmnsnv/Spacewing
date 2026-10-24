#include "shortcut.h"
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string_view>

namespace spacewing::switcher {
namespace {
struct NamedKey {
    unsigned code;
    std::string_view name;
};
constexpr NamedKey keys[] = {
    {0, "a"},      {1, "s"},          {2, "d"},
    {3, "f"},      {4, "h"},          {5, "g"},
    {6, "z"},      {7, "x"},          {8, "c"},
    {9, "v"},      {11, "b"},         {12, "q"},
    {13, "w"},     {14, "e"},         {15, "r"},
    {16, "y"},     {17, "t"},         {18, "1"},
    {19, "2"},     {20, "3"},         {21, "4"},
    {22, "6"},     {23, "5"},         {24, "equal"},
    {25, "9"},     {26, "7"},         {27, "minus"},
    {28, "8"},     {29, "0"},         {30, "rightbracket"},
    {31, "o"},     {32, "u"},         {33, "leftbracket"},
    {34, "i"},     {35, "p"},         {36, "return"},
    {37, "l"},     {38, "j"},         {39, "quote"},
    {40, "k"},     {41, "semicolon"}, {42, "backslash"},
    {43, "comma"}, {44, "slash"},     {45, "n"},
    {46, "m"},     {47, "period"},    {48, "tab"},
    {49, "space"}, {50, "grave"},     {51, "delete"},
    {96, "f5"},    {97, "f6"},        {98, "f7"},
    {99, "f3"},    {100, "f8"},       {101, "f9"},
    {103, "f11"},  {109, "f10"},      {111, "f12"},
    {118, "f4"},   {120, "f2"},       {122, "f1"},
    {123, "left"}, {124, "right"},
};
std::string trim(const std::string &value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    return start == std::string::npos
               ? ""
               : value.substr(start, value.find_last_not_of(" \t\r\n") - start + 1);
}
} // namespace

std::string Shortcut::text() const {
    const auto named = std::find_if(std::begin(keys), std::end(keys),
                                    [&](const auto &entry) { return entry.code == unsigned(key); });
    if (named == std::end(keys))
        return {};
    std::string result;
    if (modifiers & Control)
        result += "ctrl+";
    if (modifiers & Shift)
        result += "shift+";
    if (modifiers & Option)
        result += "option+";
    if (modifiers & CommandKey)
        result += "cmd+";
    return result + std::string(named->name);
}

Shortcut parseShortcut(const std::string &text) {
    auto value = trim(text);
    if (value.empty())
        return {};
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    Shortcut result{0, Key::Other};
    size_t begin = 0;
    while (begin <= value.size()) {
        const auto end = value.find('+', begin);
        const auto part = trim(value.substr(begin, end == std::string::npos ? end : end - begin));
        unsigned modifier = 0;
        if (part == "ctrl" || part == "control")
            modifier = Control;
        else if (part == "shift")
            modifier = Shift;
        else if (part == "alt" || part == "option")
            modifier = Option;
        else if (part == "cmd" || part == "command")
            modifier = CommandKey;
        else {
            const auto named = std::find_if(std::begin(keys), std::end(keys),
                                            [&](const auto &entry) { return entry.name == part; });
            if (named == std::end(keys) || result.key != Key::Other)
                throw std::invalid_argument("Invalid window switcher shortcut: " + text);
            result.key = static_cast<Key>(named->code);
        }
        if (result.modifiers & modifier)
            throw std::invalid_argument("Repeated modifier in window switcher shortcut: " + text);
        result.modifiers |= modifier;
        if (end == std::string::npos)
            break;
        begin = end + 1;
    }
    if (!result.modifiers || result.key == Key::Other)
        throw std::invalid_argument("Use a key with at least one modifier for the window switcher");
    return result;
}
Shortcuts defaultShortcuts() {
    Shortcuts result;
    for (size_t i = 0; i < shortcutActions.size(); ++i)
        result[i] = parseShortcut(shortcutActions[i].defaultChord);
    return result;
}
bool matchesShortcut(Shortcut shortcut, Key key, unsigned modifiers) {
    return shortcut.enabled() && key == shortcut.key &&
           (modifiers & shortcut.modifiers) == shortcut.modifiers &&
           (modifiers & ~(shortcut.modifiers | Shift)) == 0;
}
void validateShortcuts(const Shortcuts &shortcuts) {
    for (size_t i = 0; i < shortcuts.size(); ++i) {
        const auto shortcut = shortcuts[i];
        if (parseShortcut(shortcut.text()) != shortcut)
            throw std::invalid_argument(std::string("Invalid switcher.") +
                                        shortcutActions[i].setting);
        for (size_t j = 0; j < i; ++j)
            if (shortcut.enabled() && shortcuts[j].enabled() && shortcut.key == shortcuts[j].key &&
                (shortcut.modifiers & ~unsigned(Shift)) ==
                    (shortcuts[j].modifiers & ~unsigned(Shift)))
                throw std::invalid_argument(
                    std::string("Conflicting switcher shortcuts: ") + shortcutActions[j].setting +
                    " and " + shortcutActions[i].setting + " (including Shift reverse)");
    }
}
} // namespace spacewing::switcher
