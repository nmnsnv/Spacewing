#include "schema.h"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace spacewing {
namespace {
template <typename T> struct EnumChoice {
    T value;
    const char *key;
    const char *label;
};
template <typename T, size_t N>
ChoiceSetting choice(T switcher::Options::*member, std::array<EnumChoice<T>, N> choices) {
    auto read = [member, choices](const Config &config) -> std::string {
        for (const auto &item : choices)
            if (config.switcher.*member == item.value)
                return item.key;
        throw std::logic_error("Setting value has no declared choice");
    };
    ChoiceSetting result{
        {}, read(Config{}), read, [member, choices](Config &config, const std::string &value) {
            for (const auto &item : choices)
                if (value == item.key) {
                    config.switcher.*member = item.value;
                    return;
                }
            throw std::logic_error("Undeclared setting choice");
        }};
    for (const auto &item : choices)
        result.choices.push_back({item.key, item.label});
    return result;
}
} // namespace

SettingDefinition::SettingDefinition(std::string key, std::string label, SettingEditor editor)
    : key(std::move(key)), label(std::move(label)), editor(std::move(editor)) {
    if (this->key.empty() || this->label.empty())
        throw std::logic_error("Every setting requires a file key and a Settings label");
    std::visit(
        [](const auto &binding) {
            if (!binding.read || !binding.write)
                throw std::logic_error("Every setting requires both read and write bindings");
        },
        this->editor);
    if (auto choice = std::get_if<ChoiceSetting>(&this->editor)) {
        std::set<std::string> values;
        for (const auto &item : choice->choices)
            if (item.value.empty() || item.label.empty() || !values.insert(item.value).second)
                throw std::logic_error("Setting choices require unique values and Settings labels");
        if (!values.contains(choice->defaultValue))
            throw std::logic_error("Setting default must be a declared choice");
    }
}

std::span<const SettingsSection> settingsSchema() {
    static const auto sections = [] {
        SettingsSection workspace{
            "shortcuts",
            "Workspace shortcuts",
            "Shortcuts follow the focused window’s display. One move per key press.",
            true,
            {}};
        for (size_t i = 0; i < workspace::actionDefinitions.size(); ++i) {
            const auto &action = workspace::actionDefinitions[i];
            workspace.settings.push_back(
                {action.setting, action.label,
                 ShortcutSetting{ShortcutSetting::Kind::Workspace, action.defaultChord, "",
                                 [i](const Config &config) { return config.shortcuts[i].text(); },
                                 [i](Config &config, const std::string &value) {
                                     config.shortcuts[i] = workspace::parseShortcut(value);
                                 }}});
        }
        SettingsSection switcher{
            "switcher",
            "Window switching",
            "Choose a window layout and scope. Release the shortcut modifier to switch.",
            false,
            {}};
        switcher.settings.push_back(
            {"enabled", "Enable window switcher",
             ToggleSetting{switcher::Options{}.enabled,
                           [](const Config &config) { return config.switcher.enabled; },
                           [](Config &config, bool value) { config.switcher.enabled = value; }}});
        for (size_t i = 0; i < switcher::shortcutActions.size(); ++i) {
            const auto &action = switcher::shortcutActions[i];
            switcher.settings.push_back(
                {action.setting, action.label,
                 ShortcutSetting{
                     ShortcutSetting::Kind::Switcher, action.defaultChord, action.defaultChord,
                     [i](const Config &config) { return config.switcher.shortcuts[i].text(); },
                     [i](Config &config, const std::string &value) {
                         config.switcher.shortcuts[i] = switcher::parseShortcut(value);
                     }}});
        }
        using switcher::Scope;
        using switcher::Layout;
        switcher.settings.push_back(
            {"scope", "Window scope",
             choice(
                 &switcher::Options::scope,
                 std::array{EnumChoice{Scope::CurrentDesktop, "current-desktop", "Current desktop"},
                            EnumChoice{Scope::AllDesktops, "all-desktops", "All desktops"}})});
        switcher.settings.push_back(
            {"layout", "Window layout",
             choice(&switcher::Options::layout,
                    std::array{EnumChoice{Layout::List, "list", "List — icons and titles"},
                               EnumChoice{Layout::Grid, "grid", "Grid — live previews"}})});
        auto result = std::array{std::move(workspace), std::move(switcher)};
        std::set<std::string> sections;
        for (const auto &section : result) {
            if (section.key.empty() || section.title.empty() ||
                !sections.insert(section.key).second)
                throw std::logic_error("Settings sections require unique keys and window headings");
            std::set<std::string> keys;
            for (const auto &setting : section.settings)
                if (!keys.insert(setting.key).second)
                    throw std::logic_error("Duplicate setting: " + section.key + "." + setting.key);
        }
        return result;
    }();
    return sections;
}

SettingValue settingValue(const SettingDefinition &setting, const Config &config) {
    return std::visit([&](const auto &editor) -> SettingValue { return editor.read(config); },
                      setting.editor);
}
SettingValue settingDefault(const SettingDefinition &setting, bool omitted) {
    return std::visit(
        Visit{[&](const ShortcutSetting &editor) -> SettingValue {
                  return omitted ? editor.missingValue : editor.defaultValue;
              },
              [](const ToggleSetting &editor) -> SettingValue { return editor.defaultValue; },
              [](const ChoiceSetting &editor) -> SettingValue { return editor.defaultValue; }},
        setting.editor);
}
void setSettingValue(const SettingDefinition &setting, Config &config, const SettingValue &value) {
    const auto string = [&]() -> const std::string & {
        if (auto text = std::get_if<std::string>(&value))
            return *text;
        throw Error(setting.key + " must be a string", 78);
    };
    try {
        std::visit(
            Visit{[&](const ShortcutSetting &editor) { editor.write(config, string()); },
                  [&](const ToggleSetting &editor) {
                      if (auto enabled = std::get_if<bool>(&value))
                          editor.write(config, *enabled);
                      else
                          throw Error(setting.key + " must be a boolean", 78);
                  },
                  [&](const ChoiceSetting &editor) {
                      const auto &text = string();
                      if (std::none_of(editor.choices.begin(), editor.choices.end(),
                                       [&](const auto &item) { return item.value == text; })) {
                          std::string message = setting.key + " must be one of: ";
                          for (const auto &item : editor.choices)
                              message += item.value + ", ";
                          throw Error(message.substr(0, message.size() - 2), 78);
                      }
                      editor.write(config, text);
                  }},
            setting.editor);
    } catch (const std::invalid_argument &error) {
        throw Error(error.what(), 78);
    }
}
} // namespace spacewing
