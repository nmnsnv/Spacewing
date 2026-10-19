#include "macos.h"
#include "cf_ref.h"
#import <AppKit/AppKit.h>
#include <ApplicationServices/ApplicationServices.h>
#include <exception>

namespace spacewing {
void mainSync(const std::function<void()> &function) {
    if ([NSThread isMainThread]) {
        function();
        return;
    }
    __block std::exception_ptr error;
    dispatch_sync(dispatch_get_main_queue(), ^{
      try {
          function();
      } catch (...) {
          error = std::current_exception();
      }
    });
    if (error)
        std::rethrow_exception(error);
}
void mainAsync(std::function<void()> function) {
    dispatch_async(dispatch_get_main_queue(), ^{
      function();
    });
}
void initializeApplication() {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
}
bool openAccessibilitySettings() {
    return [NSWorkspace.sharedWorkspace
        openURL:[NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?"
                                     @"Privacy_Accessibility"]];
}
int frontmostPID() {
    int pid = 0;
    mainSync([&] { pid = NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier; });
    return pid;
}
bool activePID(int pid) {
    bool active = false;
    mainSync([&] {
        active = [NSRunningApplication runningApplicationWithProcessIdentifier:pid].active;
    });
    return active;
}
bool activatePID(int pid) {
    bool active = false;
    mainSync([&] {
        active = [[NSRunningApplication runningApplicationWithProcessIdentifier:pid]
            activateWithOptions:0];
    });
    return active;
}
bool trusted() { return AXIsProcessTrusted(); }
void requestAccessibility() {
    const void *keys[] = {kAXTrustedCheckOptionPrompt};
    const void *values[] = {kCFBooleanTrue};
    CFRef<CFDictionaryRef> options(CFDictionaryCreate(nullptr, keys, values, 1,
                                                      &kCFTypeDictionaryKeyCallBacks,
                                                      &kCFTypeDictionaryValueCallBacks));
    // The return value is the current trust state, not the user's eventual choice.
    AXIsProcessTrustedWithOptions(options.get());
}
} // namespace spacewing
