#include "native.h"
#include "window_move.h"
#import <AppKit/AppKit.h>
#include <algorithm>
#include <cmath>
#include <dlfcn.h>
#include <set>
#include <sstream>
#include <sys/sysctl.h>

namespace spacewing::workspace {
namespace {
template <typename T> T symbol(const char *name) {
    static void *library = dlopen("/System/Library/PrivateFrameworks/SkyLight.framework/SkyLight",
                                  RTLD_LAZY | RTLD_GLOBAL);
    void *address = library ? dlsym(library, name) : nullptr;
    if (!address)
        address = dlsym(RTLD_DEFAULT, name);
    if (!address)
        throw Error(std::string("Native API unavailable: ") + name);
    return reinterpret_cast<T>(address);
}
uint32_t connection() {
    static auto function = symbol<uint32_t (*)()>("SLSMainConnectionID");
    return function();
}
template <typename T> T cast(CFTypeRef value, CFTypeID type) {
    return value && CFGetTypeID(value) == type ? static_cast<T>(value) : nullptr;
}
CFTypeRef get(CFDictionaryRef dictionary, CFStringRef key) {
    return dictionary ? CFDictionaryGetValue(dictionary, key) : nullptr;
}
uint64_t number(CFTypeRef value) {
    auto n = cast<CFNumberRef>(value, CFNumberGetTypeID());
    int64_t result = 0;
    if (!n || !CFNumberGetValue(n, kCFNumberSInt64Type, &result))
        throw Error("Malformed native Space/window ID");
    return static_cast<uint64_t>(result);
}
std::string string(CFTypeRef value) {
    auto s = cast<CFStringRef>(value, CFStringGetTypeID());
    if (!s)
        throw Error("Malformed native display identifier");
    std::string result(
        CFStringGetMaximumSizeForEncoding(CFStringGetLength(s), kCFStringEncodingUTF8) + 1, '\0');
    if (!CFStringGetCString(s, result.data(), result.size(), kCFStringEncodingUTF8))
        throw Error("Cannot decode display identifier");
    result.resize(strlen(result.c_str()));
    return result;
}
std::string displayUUID(CGDirectDisplayID id) {
    CFRef<CFUUIDRef> uuid(CGDisplayCreateUUIDFromDisplayID(id));
    if (!uuid)
        throw Error("Cannot identify display");
    CFRef<CFStringRef> text(CFUUIDCreateString(nullptr, uuid.get()));
    return string(text.get());
}
CFRef<CFArrayRef> memberships(CFArrayRef windows) {
    static auto function =
        symbol<CFArrayRef (*)(uint32_t, int32_t, CFArrayRef)>("SLSCopySpacesForWindows");
    CFRef<CFArrayRef> result(function(connection(), 7, windows));
    if (!result || CFGetTypeID(result.get()) != CFArrayGetTypeID())
        throw Error("Cannot read native window membership");
    return result;
}
uint32_t windowID(AXUIElementRef element) {
    static auto function = symbol<AXError (*)(AXUIElementRef, uint32_t *)>("_AXUIElementGetWindow");
    uint32_t id = 0;
    const auto status = function(element, &id);
    if (status == kAXErrorAPIDisabled)
        throw Error("Accessibility access is disabled; enable Spacewing in System Settings", 77);
    if (status != kAXErrorSuccess || !id)
        throw FocusUnavailable("Cannot identify focused window for carrying; ordinary workspace "
                               "switching is still available");
    return id;
}
CFRef<AXUIElementRef> focusElement(int pid) {
    CFRef<AXUIElementRef> app(AXUIElementCreateApplication(pid));
    AXUIElementSetMessagingTimeout(app.get(), 0.25);
    CFTypeRef value = nullptr;
    const auto status = AXUIElementCopyAttributeValue(app.get(), kAXFocusedWindowAttribute, &value);
    CFRef<CFTypeRef> owner(value);
    if (status == kAXErrorNoValue || status == kAXErrorAttributeUnsupported)
        return {};
    if (status == kAXErrorAPIDisabled)
        throw Error("Accessibility access is disabled; enable Spacewing in System Settings", 77);
    if (status != kAXErrorSuccess)
        throw FocusUnavailable("Cannot query focused window (AX " + std::to_string(status) + ")",
                               75);
    auto window = cast<AXUIElementRef>(value, AXUIElementGetTypeID());
    if (!window)
        return {};
    CFRetain(window);
    AXUIElementSetMessagingTimeout(window, 0.25);
    return CFRef<AXUIElementRef>(window);
}
} // namespace

std::optional<uint64_t> Snapshot::neighbor(int offset) const {
    auto next = static_cast<int64_t>(current) + offset;
    return next >= 0 && next < static_cast<int64_t>(spaces.size()) ? std::optional(spaces[next])
                                                                   : std::nullopt;
}
std::string Snapshot::summary() const {
    return "Space " + std::to_string(current + 1) + " of " + std::to_string(spaces.size()) +
           " (id " + std::to_string(id()) + ", display " + display + ")";
}
Snapshot selectSnapshot(const std::vector<Snapshot> &displays, const std::string &requested) {
    if (displays.size() == 1 && (displays.front().display == "Main" || requested.empty()))
        return displays.front(); // Shared Spaces use the synthetic "Main" identifier.
    for (const auto &display : displays)
        if (display.display == requested)
            return display;
    throw Error("Cannot read native Spaces for the target display; it may have disconnected", 75);
}
Snapshot snapshot(const std::string &display) {
    static auto copy = symbol<CFArrayRef (*)(uint32_t)>("SLSCopyManagedDisplaySpaces");
    static auto active = symbol<uint64_t (*)(uint32_t)>("SLSGetActiveSpace");
    CFRef<CFArrayRef> raw(copy(connection()));
    if (!raw || CFGetTypeID(raw.get()) != CFArrayGetTypeID())
        throw Error("WindowServer did not return a native Space list");
    std::vector<Snapshot> displays;
    for (CFIndex i = 0; i < CFArrayGetCount(raw.get()); ++i) {
        auto item =
            cast<CFDictionaryRef>(CFArrayGetValueAtIndex(raw.get(), i), CFDictionaryGetTypeID());
        if (!item)
            throw Error("Malformed native display topology");
        Snapshot result;
        result.display = string(get(item, CFSTR("Display Identifier")));
        auto spaces = cast<CFArrayRef>(get(item, CFSTR("Spaces")), CFArrayGetTypeID());
        if (!spaces || !CFArrayGetCount(spaces))
            throw Error("Native display has no Spaces");
        for (CFIndex j = 0; j < CFArrayGetCount(spaces); ++j) {
            auto space =
                cast<CFDictionaryRef>(CFArrayGetValueAtIndex(spaces, j), CFDictionaryGetTypeID());
            result.spaces.push_back(number(get(space, CFSTR("id64"))));
        }
        if (std::set<uint64_t>(result.spaces.begin(), result.spaces.end()).size() !=
            result.spaces.size())
            throw Error("Duplicate native Space IDs");
        auto current =
            cast<CFDictionaryRef>(get(item, CFSTR("Current Space")), CFDictionaryGetTypeID());
        uint64_t currentID = current ? number(get(current, CFSTR("id64"))) : active(connection());
        auto found = std::find(result.spaces.begin(), result.spaces.end(), currentID);
        if (found == result.spaces.end())
            throw Error("Current native Space is absent from topology");
        result.current = found - result.spaces.begin();
        displays.push_back(std::move(result));
    }
    return selectSnapshot(displays, display);
}
std::string cursorDisplay() {
    CFRef<CGEventRef> event(CGEventCreate(nullptr));
    if (!event)
        throw Error("Cannot read pointer position");
    CGDirectDisplayID id = 0;
    uint32_t count = 0;
    if (CGGetDisplaysWithPoint(CGEventGetLocation(event.get()), 1, &id, &count) !=
            kCGErrorSuccess ||
        !count)
        throw Error("Pointer is not on a connected display");
    return displayUUID(id);
}
CGPoint displayCenter(const std::string &display) {
    uint32_t count = 0;
    if (CGGetActiveDisplayList(0, nullptr, &count) != kCGErrorSuccess)
        throw Error("Cannot enumerate displays");
    std::vector<CGDirectDisplayID> ids(count);
    if (CGGetActiveDisplayList(count, ids.data(), &count) != kCGErrorSuccess)
        throw Error("Cannot enumerate displays");
    for (auto id : ids)
        if (displayUUID(id) == display) {
            auto bounds = CGDisplayBounds(id);
            return {CGRectGetMidX(bounds), CGRectGetMidY(bounds)};
        }
    throw Error("The focused display is no longer connected", 75);
}
std::vector<uint64_t> windowSpaces(uint32_t window) {
    int64_t id = window;
    CFRef<CFNumberRef> number(CFNumberCreate(nullptr, kCFNumberSInt64Type, &id));
    const void *value = number.get();
    CFRef<CFArrayRef> windows(CFArrayCreate(nullptr, &value, 1, &kCFTypeArrayCallBacks));
    auto spaces = memberships(windows.get());
    std::vector<uint64_t> result;
    for (CFIndex i = 0; i < CFArrayGetCount(spaces.get()); ++i)
        result.push_back(spacewing::workspace::number(CFArrayGetValueAtIndex(spaces.get(), i)));
    return result;
}
bool isDesktop(uint64_t space) {
    static auto function = symbol<int32_t (*)(uint32_t, uint64_t)>("SLSSpaceGetType");
    return function(connection(), space) == 0;
}
uint64_t activeSpaceID() {
    static auto function = symbol<uint64_t (*)(uint32_t)>("SLSGetActiveSpace");
    return function(connection());
}
std::optional<bool> spaceHasWindows(uint64_t space) {
    try {
        CFRef<CFArrayRef> list(
            CGWindowListCopyWindowInfo(kCGWindowListExcludeDesktopElements, kCGNullWindowID));
        if (!list)
            return {};
        CFRef<CFMutableArrayRef> ids(CFArrayCreateMutable(nullptr, 0, &kCFTypeArrayCallBacks));
        for (CFIndex i = 0; i < CFArrayGetCount(list.get()); ++i) {
            auto info = cast<CFDictionaryRef>(CFArrayGetValueAtIndex(list.get(), i),
                                              CFDictionaryGetTypeID());
            if (info && number(get(info, kCGWindowLayer)) == 0) {
                auto id = get(info, kCGWindowNumber);
                if (id)
                    CFArrayAppendValue(ids.get(), id);
            }
        }
        if (!CFArrayGetCount(ids.get()))
            return false;
        auto spaces = memberships(ids.get());
        for (CFIndex i = 0; i < CFArrayGetCount(spaces.get()); ++i)
            if (number(CFArrayGetValueAtIndex(spaces.get(), i)) == space)
                return true;
        return false;
    } catch (...) {
        return {};
    } // Unknown is not proof of an empty desktop.
}
std::string osVersion() {
    size_t size = 0;
    if (sysctlbyname("kern.osproductversion", nullptr, &size, nullptr, 0) || !size)
        throw Error("Cannot read OS version");
    std::string text(size, '\0');
    if (sysctlbyname("kern.osproductversion", text.data(), &size, nullptr, 0))
        throw Error("Cannot read OS version");
    text.resize(strlen(text.c_str()));
    return text;
}
bool needsAugmentation(const std::string &version) {
    size_t consumed = 0;
    int major = 0;
    try {
        major = std::stoi(version, &consumed);
    } catch (...) {
        throw Error("Invalid OS version");
    }
    if (major < 1 || (consumed < version.size() && version[consumed] != '.'))
        throw Error("Invalid OS version");
    return major >= 27;
}
std::optional<Window> focusedWindow() {
    if (!trusted())
        throw Error("Accessibility is required. Grant Spacewing.app access in System Settings > "
                    "Privacy & Security > Accessibility, then restart the app.",
                    77);
    int pid = frontmostPID();
    if (!pid)
        return {};
    auto element = focusElement(pid);
    if (!element)
        return {};
    Window result;
    result.id = windowID(element.get());
    result.pid = pid;
    result.element = std::move(element);
    CFTypeRef fullscreen = nullptr;
    AXUIElementCopyAttributeValue(result.element.get(), CFSTR("AXFullScreen"), &fullscreen);
    CFRef<CFTypeRef> owner(fullscreen);
    result.fullscreen = fullscreen && CFEqual(fullscreen, kCFBooleanTrue);
    static auto display =
        symbol<CFStringRef (*)(uint32_t, uint32_t)>("SLSCopyManagedDisplayForWindow");
    CFRef<CFStringRef> name(display(connection(), result.id));
    if (!name)
        throw FocusUnavailable("Cannot locate focused window's display");
    result.display = string(name.get());
    return result;
}
void restoreFocus(const Window &window) {
    if (activePID(window.pid)) {
        auto focused = focusElement(window.pid);
        if (focused && windowID(focused.get()) == window.id)
            return;
    }
    const auto status =
        AXUIElementSetAttributeValue(window.element.get(), kAXMainAttribute, kCFBooleanTrue);
    if (status != kAXErrorSuccess && status != kAXErrorAttributeUnsupported)
        throw Error("Window moved, but setting its main state failed");
    if (AXUIElementPerformAction(window.element.get(), kAXRaiseAction) != kAXErrorSuccess ||
        (!activePID(window.pid) && !activatePID(window.pid)))
        throw Error("Window moved, but restoring focus failed");
}
PointerRoute::PointerRoute(const std::string &display) {
    if (display == "Main" || cursorDisplay() == display)
        return;
    CFRef<CGEventRef> event(CGEventCreate(nullptr));
    if (!event)
        throw Error("Cannot read pointer position");
    original_ = CGEventGetLocation(event.get());
    routed_ = displayCenter(display);
    if (CGWarpMouseCursorPosition(routed_) != kCGErrorSuccess)
        throw Error("Cannot route gesture to focused display");
    moved_ = true;
}
PointerRoute::~PointerRoute() {
    if (!moved_)
        return;
    CFRef<CGEventRef> event(CGEventCreate(nullptr));
    if (!event)
        return;
    const auto position = CGEventGetLocation(event.get());
    if (std::abs(position.x - routed_.x) < 1 && std::abs(position.y - routed_.y) < 1)
        CGWarpMouseCursorPosition(original_);
}
std::string diagnostics() {
    std::string result =
        "macOS: " + osVersion() + "\nAccessibility: " + (trusted() ? "granted" : "not granted") +
        "\nNative window move: " + (canMoveWindows() ? "available" : "unavailable") + "\n";
    try {
        result += snapshot(cursorDisplay()).summary() + "\n";
    } catch (const std::exception &e) {
        result += "Native Spaces: " + std::string(e.what()) + "\n";
    }
    return result;
}
bool canMoveWindows() {
    static void *library = dlopen("/System/Library/PrivateFrameworks/SkyLight.framework/SkyLight",
                                  RTLD_LAZY | RTLD_GLOBAL);
    return library && BFCanMoveWindows();
}
void submitWindowMove(uint32_t window, uint64_t space) {
    bool submitted = false;
    mainSync([&] { submitted = BFSubmitWindowMove(window, space); });
    if (!submitted)
        throw Error("Could not submit native window move; no switch gesture posted");
}
void activateEmptyDesktop(const std::string &display, uint64_t space) {
    try {
        if (snapshot(display).id() == space && activeSpaceID() == space)
            [NSApp activateIgnoringOtherApps:YES];
    } catch (...) {
    }
}
void observeWorkspace(const std::function<void()> &changed, const std::function<void()> &woke) {
    auto changedAction = changed;
    auto wokeAction = woke;
    auto center = NSWorkspace.sharedWorkspace.notificationCenter;
    [center addObserverForName:NSWorkspaceActiveSpaceDidChangeNotification
                        object:nil
                         queue:NSOperationQueue.mainQueue
                    usingBlock:^(NSNotification *) {
                      changedAction();
                    }];
    [center addObserverForName:NSWorkspaceDidWakeNotification
                        object:nil
                         queue:NSOperationQueue.mainQueue
                    usingBlock:^(NSNotification *) {
                      wokeAction();
                    }];
    [center addObserverForName:NSWorkspaceSessionDidBecomeActiveNotification
                        object:nil
                         queue:NSOperationQueue.mainQueue
                    usingBlock:^(NSNotification *) {
                      wokeAction();
                    }];
}
} // namespace spacewing::workspace
