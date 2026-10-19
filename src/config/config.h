#pragma once

#include "result.h"
#include "session.h"
#include "shortcuts.h"
#include <functional>
#include <optional>

namespace spacewing {
struct Config {
    workspace::Shortcuts shortcuts;
    switcher::Options switcher;
};
std::string defaultConfig();
Config parseConfig(const std::string &text);
std::string editedConfig(const std::string &original, const std::array<std::string, 4> &chords,
                         std::optional<switcher::Options> switcher = std::nullopt);
std::string readFile(const std::string &path);

// Prepare before changing hotkeys; commit with a rename only after registration succeeds.
class PendingSave {
public:
    PendingSave(const std::string &path, const std::string &text);
    ~PendingSave();
    PendingSave(const PendingSave &) = delete;
    void commit(const std::optional<std::string> &expected = std::nullopt);
    void createIfMissing();

private:
    std::string target_;
    std::string temporary_;
};
} // namespace spacewing
