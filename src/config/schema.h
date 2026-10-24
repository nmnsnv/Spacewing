#pragma once

#include "config.h"
#include <span>
#include <variant>

namespace spacewing {
// Each supported file value has an editor type. Consumers use exhaustive visitors:
// adding a new type cannot compile until parsing, saving, and the UI support it.
struct ShortcutSetting {
    enum class Kind { Workspace, Switcher };
    Kind kind;
    std::string defaultValue;
    std::string missingValue;
    std::function<std::string(const Config &)> read;
    std::function<void(Config &, const std::string &)> write;
};
struct ToggleSetting {
    bool defaultValue;
    std::function<bool(const Config &)> read;
    std::function<void(Config &, bool)> write;
};
struct SettingChoice {
    std::string value;
    std::string label;
};
struct ChoiceSetting {
    std::vector<SettingChoice> choices;
    std::string defaultValue;
    std::function<std::string(const Config &)> read;
    std::function<void(Config &, const std::string &)> write;
};
using SettingEditor = std::variant<ShortcutSetting, ToggleSetting, ChoiceSetting>;
using SettingValue = std::variant<std::string, bool>;
struct SettingDefinition {
    std::string key;
    std::string label;
    SettingEditor editor;
    SettingDefinition(std::string key, std::string label, SettingEditor editor);
};
struct SettingsSection {
    std::string key;
    std::string title;
    std::string description;
    bool required;
    std::vector<SettingDefinition> settings;
};

template <class... T> struct Visit : T... {
    using T::operator()...;
};
template <class... T> Visit(T...) -> Visit<T...>;

// The sole registry of accepted TOML settings and Settings window controls.
std::span<const SettingsSection> settingsSchema();
SettingValue settingValue(const SettingDefinition &setting, const Config &config);
SettingValue settingDefault(const SettingDefinition &setting, bool omitted = false);
void setSettingValue(const SettingDefinition &setting, Config &config, const SettingValue &value);
} // namespace spacewing
