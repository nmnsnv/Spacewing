#pragma once
#include "macos.h"
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace spacewing {
void runApplication(const std::function<void()> &ready);
void setMenu(const std::function<void()> &settings, const std::function<void()> &reload);
void setStatus(const std::string &status);
void setDesktopNumber(std::optional<size_t> number);
void launchSettings();
void closeSettings();
struct Watcher {
    virtual ~Watcher() = default;
};
std::unique_ptr<Watcher> watchConfig(const std::string &path, std::function<void()> changed);
} // namespace spacewing
