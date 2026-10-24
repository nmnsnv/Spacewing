#include "config.h"
#include "command.h"
#include "schema.h"
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
    text << "# Spacewing: native macOS Spaces. Empty strings disable shortcuts.\n";
    for (const auto &section : settingsSchema()) {
        text << "\n[" << section.key << "]\n";
        for (const auto &setting : section.settings) {
            text << "# " << setting.label << "\n" << setting.key << " = ";
            const auto value = settingDefault(setting);
            text << std::visit([](const auto &item) { return toml::format(toml::value(item)); },
                               value);
            text << "\n";
        }
    }
    return text.str();
}

Config parseConfig(const std::string &text) {
    const auto root = document(text);
    const auto schema = settingsSchema();
    for (const auto &[key, value] : root.as_table())
        if (std::none_of(schema.begin(), schema.end(),
                         [&](const auto &section) { return section.key == key; }))
            throw Error("Unknown setting: " + key, 78);
    Config config{};
    for (const auto &section : schema) {
        if (!root.contains(section.key)) {
            if (section.required)
                throw Error("Config requires a [" + section.key + "] table", 78);
        } else {
            const auto &table = root.at(section.key);
            if (!table.is_table())
                throw Error("[" + section.key + "] must be a table", 78);
            for (const auto &[key, value] : table.as_table())
                if (std::none_of(section.settings.begin(), section.settings.end(),
                                 [&](const auto &setting) { return setting.key == key; }))
                    throw Error("Unknown setting: " + section.key + "." + key, 78);
        }
        for (const auto &setting : section.settings) {
            auto value = settingDefault(setting, true);
            if (root.contains(section.key) && root.at(section.key).contains(setting.key)) {
                const auto &raw = root.at(section.key).at(setting.key);
                if (raw.is_string())
                    value = std::string(raw.as_string());
                else if (raw.is_boolean())
                    value = raw.as_boolean();
                else
                    throw Error("Invalid value type for " + section.key + "." + setting.key, 78);
            }
            try {
                setSettingValue(setting, config, value);
            } catch (const Error &error) {
                throw Error(section.key + "." + error.what(), error.code);
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

namespace {
std::string serializeConfig(const std::string &original, const Config &config, bool shortcutsOnly) {
    parseConfig(original);
    auto root = document(original);
    for (const auto &section : settingsSchema()) {
        if (shortcutsOnly && section.key != "shortcuts")
            continue;
        if (!root.contains(section.key))
            root[section.key] = toml::table{};
        auto &table = root.at(section.key);
        for (const auto &setting : section.settings) {
            std::visit(Visit{[&](const std::string &value) {
                                 if (table.contains(setting.key))
                                     table.at(setting.key).as_string() = value;
                                 else
                                     table[setting.key] = value;
                             },
                             [&](bool value) {
                                 if (table.contains(setting.key))
                                     table.at(setting.key).as_boolean() = value;
                                 else
                                     table[setting.key] = value;
                             }},
                       settingValue(setting, config));
        }
    }
    auto result = toml::format(root);
    parseConfig(result);
    return result;
}
} // namespace

std::string editedConfig(const std::string &original, const Config &config) {
    return serializeConfig(original, config, false);
}
std::string editedConfig(const std::string &original, const WorkspaceChords &chords,
                         std::optional<switcher::Options> switcher) {
    auto config = parseConfig(original);
    for (size_t i = 0; i < chords.size(); ++i)
        config.shortcuts[i] = parseShortcut(chords[i]);
    if (switcher)
        config.switcher = *switcher;
    return serializeConfig(original, config, !switcher);
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
