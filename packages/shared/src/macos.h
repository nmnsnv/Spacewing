#pragma once
#include <functional>

namespace spacewing {
// Runs inline on the main thread, or blocks the caller until the main queue finishes.
// Exceptions from the callback are rethrown on the calling thread.
void mainSync(const std::function<void()> &function);
// Queues work on the main thread and returns immediately. The callback must handle
// its exceptions and keep any referenced objects alive until it runs.
void mainAsync(std::function<void()> function);
// Call on the main thread before creating UI; initializes AppKit without a Dock icon.
void initializeApplication();
// These PID helpers marshal AppKit access to the main thread. activatePID returns
// AppKit's activation result; it does not verify focus on a particular window.
int frontmostPID();
bool activePID(int pid);
bool activatePID(int pid);
// Reads the current Accessibility grant without prompting or changing permissions.
bool trusted();
// Requests the asynchronous system permission prompt; does not wait for a grant.
void requestAccessibility();
// Opens the Accessibility settings pane; a successful open does not grant access.
bool openAccessibilitySettings();
} // namespace spacewing
