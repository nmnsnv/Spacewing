#pragma once
#include "cf_ref.h"
#include "macos.h"
#include "result.h"
#include <ApplicationServices/ApplicationServices.h>
#include <functional>
#include <optional>

namespace spacewing::workspace {
struct Snapshot {
    std::string display;
    std::vector<uint64_t> spaces;
    size_t current = 0;
    uint64_t id() const { return spaces.at(current); }
    std::optional<uint64_t> neighbor(int direction) const;
    std::string summary() const;
};
Snapshot selectSnapshot(const std::vector<Snapshot> &displays, const std::string &requested);
Snapshot snapshot(const std::string &display);
std::string cursorDisplay();
CGPoint displayCenter(const std::string &display);
std::vector<uint64_t> windowSpaces(uint32_t window);
bool isDesktop(uint64_t space);
uint64_t activeSpaceID();
std::optional<bool> spaceHasWindows(uint64_t space);
std::string osVersion();
bool needsAugmentation(const std::string &version);
std::string diagnostics();

struct Window {
    uint32_t id = 0;
    int pid = 0;
    bool fullscreen = false;
    std::string display;
    CFRef<AXUIElementRef> element;
};
// An app may expose incomplete or unavailable AX windows. Navigation can fall
// back to a known display; carrying must preserve this failure.
struct FocusUnavailable : Error {
    using Error::Error;
};
std::optional<Window> focusedWindow();
void restoreFocus(const Window &window);
bool canMoveWindows();
void submitWindowMove(uint32_t window, uint64_t space);
void activateEmptyDesktop(const std::string &display, uint64_t space);
void observeWorkspace(const std::function<void()> &changed, const std::function<void()> &woke);

class PointerRoute {
public:
    explicit PointerRoute(const std::string &display);
    ~PointerRoute();

private:
    bool moved_ = false;
    CGPoint original_{}, routed_{};
};
} // namespace spacewing::workspace
