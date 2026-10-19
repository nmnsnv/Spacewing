#pragma once
#include "shortcuts.h"

namespace spacewing::workspace {
void installHotkeys(const std::function<void(uint64_t, bool)> &callback);
uint64_t registerHotkey(Shortcut shortcut);
void unregisterHotkey(uint64_t id);
} // namespace spacewing::workspace
