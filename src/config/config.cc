#include "config.h"
#include "command.h"
#include "toml.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace spacewing {
using namespace workspace;
namespace {
auto document(const std::string &text) {
    std::istringstream input(text);
    try {
        return toml::parse(input, "config");
    } catch (const std::exception &e) {
        throw Error(e.what(), 78);
    }
}
} // namespace

std::string defaultConfig() {
    std::ostringstream text;
    text << "# Spacewing: native macOS Spaces. Empty strings disable shortcuts.\n[shortcuts]\n";
    for (const auto &action : actionDefinitions)
        text << action.setting << " = \"" << action.defaultChord << "\"\n";
    text << "\n[switcher]\nenabled = true\n";
    for (const auto &action : switcher::shortcutActions)
        text << action.setting << " = \"" << action.defaultChord << "\"\n";
    text << "scope = \"current-desktop\"\n";
    return text.str();
}

Config parseConfig(const std::string &text) {
    auto root = document(text);
    for (const auto &[key, value] : root.as_table())
        if (key != "shortcuts" && key != "switcher")
            throw Error("Unknown setting: " + key, 78);
    if (!root.contains("shortcuts") || !root.at("shortcuts").is_table())
        throw Error("Config requires a [shortcuts] table", 78);
    auto &table = root.at("shortcuts").as_table();
    for (const auto &[key, value] : table) {
        if (std::find(actionNames.begin(), actionNames.end(), key) == actionNames.end())
            throw Error("Unknown shortcut action: " + key, 78);
        if (!value.is_string())
            throw Error("Shortcut " + key + " must be a string", 78);
    }
    Config config{};
    if (root.contains("switcher")) {
        const auto &settings = root.at("switcher");
        if (!settings.is_table())
            throw Error("[switcher] must be a table", 78);
        for (const auto &[key, value] : settings.as_table()) {
            const auto action =
                std::find_if(switcher::shortcutActions.begin(), switcher::shortcutActions.end(),
                             [&](const auto &item) { return key == item.setting; });
            if (action != switcher::shortcutActions.end()) {
                if (!value.is_string())
                    throw Error("switcher." + key + " must be a string", 78);
                try {
                    config.switcher.shortcuts[action - switcher::shortcutActions.begin()] =
                        switcher::parseShortcut(value.as_string());
                } catch (const std::invalid_argument &e) {
                    throw Error(e.what(), 78);
                }
            } else if (key == "enabled") {
                if (!value.is_boolean())
                    throw Error("switcher.enabled must be a boolean", 78);
                config.switcher.enabled = value.as_boolean();
            } else if (key == "scope") {
                if (!value.is_string() ||
                    (value.as_string() != "current-desktop" && value.as_string() != "all-desktops"))
                    throw Error("switcher.scope must be current-desktop or all-desktops", 78);
                config.switcher.scope = value.as_string() == "all-desktops"
                                            ? switcher::Scope::AllDesktops
                                            : switcher::Scope::CurrentDesktop;
            } else {
                throw Error("Unknown switcher setting: " + key, 78);
            }
        }
    }
    try {
        switcher::validateShortcuts(config.switcher.shortcuts);
    } catch (const std::invalid_argument &e) {
        throw Error(e.what(), 78);
    }
    std::set<Shortcut> used;
    for (size_t i = 0; i < actionNames.size(); ++i) {
        auto it = table.find(actionNames[i]);
        if (it == table.end())
            continue; // Omitted actions are disabled.
        config.shortcuts[i] = parseShortcut(it->second.as_string());
        if (config.shortcuts[i].enabled() && !used.insert(config.shortcuts[i]).second)
            throw Error("Duplicate shortcut: " + config.shortcuts[i].text(), 78);
        const auto &shortcut = config.shortcuts[i];
        if (config.switcher.enabled && shortcut.enabled())
            for (const auto &binding : config.switcher.shortcuts)
                if (switcher::matchesShortcut(binding, static_cast<switcher::Key>(shortcut.key),
                                              shortcut.modifiers))
                    throw Error("Window switcher shortcut conflicts with desktop shortcut: " +
                                    shortcut.text(),
                                78);
    }
    return config;
}

std::string editedConfig(const std::string &original, const WorkspaceChords &chords,
                         std::optional<switcher::Options> switcher) {
    parseConfig(original);
    auto root = document(original);
    auto &table = root.at("shortcuts");
    for (size_t i = 0; i < actionNames.size(); ++i) {
        auto normalized = parseShortcut(chords[i]).text();
        if (table.contains(actionNames[i]))
            table.at(actionNames[i]).as_string() = normalized;
        else
            table[actionNames[i]] = normalized;
    }
    if (switcher) {
        if (!root.contains("switcher"))
            root["switcher"] = toml::table{};
        auto &settings = root.at("switcher");
        if (settings.contains("enabled"))
            settings.at("enabled").as_boolean() = switcher->enabled;
        else
            settings["enabled"] = switcher->enabled;
        const std::string scope =
            switcher->scope == switcher::Scope::AllDesktops ? "all-desktops" : "current-desktop";
        if (settings.contains("scope"))
            settings.at("scope").as_string() = scope;
        else
            settings["scope"] = scope;
        for (size_t i = 0; i < switcher::shortcutActions.size(); ++i) {
            const auto key = switcher::shortcutActions[i].setting;
            const auto chord = switcher->shortcuts[i].text();
            if (settings.contains(key))
                settings.at(key).as_string() = chord;
            else
                settings[key] = chord;
        }
    }
    auto result = toml::format(root);
    parseConfig(result);
    return result;
}

std::string readFile(const std::string &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw Error("Cannot read config: " + path, 78);
    std::string result;
    char buffer[4096];
    while (input.read(buffer, sizeof(buffer)) || input.gcount()) {
        result.append(buffer, static_cast<size_t>(input.gcount()));
        if (result.size() > 1024 * 1024)
            throw Error("Config exceeds 1 MiB", 78);
    }
    if (input.bad())
        throw Error("Error reading config: " + path, 78);
    return result;
}

PendingSave::PendingSave(const std::string &path, const std::string &text)
    : target_(resolvedPath(path)), temporary_(target_ + ".tmp.XXXXXX") {
    int fd = mkstemp(temporary_.data());
    if (fd < 0)
        throw Error("Cannot prepare config save: " + std::string(strerror(errno)), 78);
    try {
        struct stat metadata{};
        if (stat(target_.c_str(), &metadata) == 0 && fchmod(fd, metadata.st_mode & 0777) != 0)
            throw Error("Cannot preserve config permissions", 78);
        size_t written = 0;
        while (written < text.size()) {
            ssize_t count = write(fd, text.data() + written, text.size() - written);
            if (count < 0 && errno == EINTR)
                continue;
            if (count <= 0)
                throw Error("Cannot write config: " + std::string(strerror(errno)), 78);
            written += static_cast<size_t>(count);
        }
        if (fsync(fd) != 0)
            throw Error("Cannot flush config: " + std::string(strerror(errno)), 78);
        close(fd);
    } catch (...) {
        close(fd);
        unlink(temporary_.c_str());
        throw;
    }
}
PendingSave::~PendingSave() {
    if (!temporary_.empty())
        unlink(temporary_.c_str());
}
void PendingSave::createIfMissing() {
    // link is atomic and cannot replace a file created concurrently by an editor.
    if (link(temporary_.c_str(), target_.c_str()) != 0)
        throw Error("Cannot create config without replacing an existing file: " +
                        std::string(strerror(errno)),
                    78);
    unlink(temporary_.c_str());
    temporary_.clear();
}
void PendingSave::commit(const std::optional<std::string> &expected) {
    if (expected && readFile(target_) != *expected)
        throw Error("Config changed outside Settings. Reload before saving.", 78);
    if (rename(temporary_.c_str(), target_.c_str()) != 0)
        throw Error("Cannot replace config: " + std::string(strerror(errno)), 78);
    temporary_.clear();
}

} // namespace spacewing
