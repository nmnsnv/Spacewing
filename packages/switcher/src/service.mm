#include "service.h"
#include "cf_ref.h"
#include "macos.h"
#include "overlay.h"
#include "switching.h"
#import <AppKit/AppKit.h>
#include <ApplicationServices/ApplicationServices.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <dlfcn.h>
#include <map>
#include <set>
#include <stdexcept>
#include <thread>
#include <unistd.h>
#include <utility>

namespace spacewing::switcher {
namespace {
template <typename T> T symbol(const char *name) {
    static void *library = dlopen("/System/Library/PrivateFrameworks/SkyLight.framework/SkyLight",
                                  RTLD_LAZY | RTLD_LOCAL);
    void *address = dlsym(RTLD_DEFAULT, name);
    if (!address && library)
        address = dlsym(library, name);
    if (!address)
        throw std::runtime_error(std::string("Window switcher API unavailable: ") + name);
    return reinterpret_cast<T>(address);
}
CFRef<CFTypeRef> attribute(AXUIElementRef element, CFStringRef name) {
    CFTypeRef value = nullptr;
    AXUIElementCopyAttributeValue(element, name, &value);
    return CFRef<CFTypeRef>(value);
}
std::string string(CFTypeRef value) {
    if (!value || CFGetTypeID(value) != CFStringGetTypeID())
        return {};
    const char *text = [(__bridge NSString *)value UTF8String];
    return text ? text : "";
}
uint32_t windowNumber(AXUIElementRef element) {
    static auto get = symbol<AXError (*)(AXUIElementRef, uint32_t *)>("_AXUIElementGetWindow");
    uint32_t number = 0;
    return get(element, &number) == kAXErrorSuccess ? number : 0;
}
uint64_t currentDesktop() {
    static auto connection = symbol<uint32_t (*)()>("SLSMainConnectionID");
    static auto active = symbol<uint64_t (*)(uint32_t)>("SLSGetActiveSpace");
    return active(connection());
}
bool belongsTo(uint32_t window, uint64_t desktop) {
    static auto connection = symbol<uint32_t (*)()>("SLSMainConnectionID");
    static auto spaces =
        symbol<CFArrayRef (*)(uint32_t, int32_t, CFArrayRef)>("SLSCopySpacesForWindows");
    int64_t id = window;
    CFRef<CFNumberRef> number(CFNumberCreate(nullptr, kCFNumberSInt64Type, &id));
    const void *value = number.get();
    CFRef<CFArrayRef> windows(CFArrayCreate(nullptr, &value, 1, &kCFTypeArrayCallBacks));
    CFRef<CFArrayRef> membership(spaces(connection(), 7, windows.get()));
    if (!membership || CFGetTypeID(membership.get()) != CFArrayGetTypeID())
        throw std::runtime_error("Window switcher cannot read desktop membership");
    for (CFIndex i = 0; i < CFArrayGetCount(membership.get()); ++i) {
        auto item = CFArrayGetValueAtIndex(membership.get(), i);
        int64_t space = 0;
        if (item && CFGetTypeID(item) == CFNumberGetTypeID() &&
            CFNumberGetValue(static_cast<CFNumberRef>(item), kCFNumberSInt64Type, &space) &&
            static_cast<uint64_t>(space) == desktop)
            return true;
    }
    return false;
}
CFRef<AXUIElementRef> application(int pid) {
    CFRef<AXUIElementRef> app(AXUIElementCreateApplication(pid));
    AXUIElementSetMessagingTimeout(app.get(), 0.1);
    return app;
}
std::optional<WindowID> focusedWindow(int pid) {
    if (pid <= 0)
        return {};
    auto app = application(pid);
    auto focused = attribute(app.get(), kAXFocusedWindowAttribute);
    if (focused && CFGetTypeID(focused.get()) == AXUIElementGetTypeID()) {
        auto number = windowNumber(static_cast<AXUIElementRef>(focused.get()));
        if (number)
            return WindowID{pid, number};
    }
    return {};
}
struct Catalog {
    std::vector<Candidate> windows;
    std::optional<WindowID> focused;
};
Catalog catalog(Scope scope, int frontmost, const std::atomic_bool &cancelled) {
    if (!trusted())
        throw std::runtime_error(
            "Window switching requires Accessibility access; grant access and restart.");
    Catalog result;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    result.focused = focusedWindow(frontmost);
    const auto desktop = scope == Scope::CurrentDesktop ? currentDesktop() : 0;
    if (scope == Scope::CurrentDesktop && !desktop)
        throw std::runtime_error("Window switcher cannot identify the current desktop");
    CFRef<CFArrayRef> raw(
        CGWindowListCopyWindowInfo(kCGWindowListExcludeDesktopElements, kCGNullWindowID));
    if (!raw)
        throw std::runtime_error("Window switcher cannot enumerate windows");
    std::vector<WindowID> order;
    std::map<WindowID, std::string> names;
    for (NSDictionary *info in (__bridge NSArray *)raw.get()) {
        if (cancelled)
            return {};
        const WindowID id{[info[(__bridge NSString *)kCGWindowOwnerPID] intValue],
                          [info[(__bridge NSString *)kCGWindowNumber] unsignedIntValue]};
        if (id.pid <= 0 || id.pid == getpid() || !id.number ||
            [info[(__bridge NSString *)kCGWindowLayer] intValue] != 0)
            continue;
        if (desktop && !belongsTo(id.number, desktop))
            continue;
        order.push_back(id);
        const char *name = [info[(__bridge NSString *)kCGWindowOwnerName] UTF8String];
        names[id] = name ? name : "Application";
    }
    std::set<int> visited;
    std::map<WindowID, Candidate> candidates;
    for (auto id : order) {
        if (cancelled)
            return {};
        if (std::chrono::steady_clock::now() >= deadline)
            break;
        if (!visited.insert(id.pid).second)
            continue;
        auto app = application(id.pid);
        auto windows = attribute(app.get(), kAXWindowsAttribute);
        if (!windows || CFGetTypeID(windows.get()) != CFArrayGetTypeID())
            continue; // One unresponsive or unsupported app must not break the picker.
        auto array = static_cast<CFArrayRef>(windows.get());
        for (CFIndex i = 0; i < CFArrayGetCount(array); ++i) {
            if (cancelled || std::chrono::steady_clock::now() >= deadline)
                break;
            auto value = CFArrayGetValueAtIndex(array, i);
            if (!value || CFGetTypeID(value) != AXUIElementGetTypeID())
                continue;
            auto element = static_cast<AXUIElementRef>(value);
            AXUIElementSetMessagingTimeout(element, 0.1);
            WindowID key{id.pid, windowNumber(element)};
            if (!names.contains(key))
                continue;
            auto minimized = attribute(element, kAXMinimizedAttribute);
            if (minimized && CFEqual(minimized.get(), kCFBooleanTrue))
                continue;
            auto role = attribute(element, kAXSubroleAttribute);
            if (!role || (!CFEqual(role.get(), kAXStandardWindowSubrole) &&
                          !CFEqual(role.get(), kAXDialogSubrole)))
                continue;
            auto title = attribute(element, kAXTitleAttribute);
            candidates.emplace(key, Candidate{key, names.at(key), string(title.get())});
        }
    }
    for (auto id : order)
        if (auto it = candidates.find(id); it != candidates.end())
            result.windows.push_back(std::move(it->second));
    return result;
}
bool activateWindow(WindowID target, Scope scope, const std::atomic_bool &cancelled) {
    if (!trusted())
        throw std::runtime_error("Accessibility access was revoked");
    if (scope == Scope::CurrentDesktop && !belongsTo(target.number, currentDesktop()))
        throw std::runtime_error("The selected window is no longer on the current desktop");
    auto app = application(target.pid);
    auto windows = attribute(app.get(), kAXWindowsAttribute);
    if (windows && CFGetTypeID(windows.get()) == CFArrayGetTypeID()) {
        auto array = static_cast<CFArrayRef>(windows.get());
        for (CFIndex i = 0; i < CFArrayGetCount(array); ++i) {
            auto value = CFArrayGetValueAtIndex(array, i);
            if (!value || CFGetTypeID(value) != AXUIElementGetTypeID())
                continue;
            auto element = static_cast<AXUIElementRef>(value);
            AXUIElementSetMessagingTimeout(element, 0.1);
            if (windowNumber(element) != target.number)
                continue;
            auto minimized = attribute(element, kAXMinimizedAttribute);
            if (minimized && CFEqual(minimized.get(), kCFBooleanTrue))
                throw std::runtime_error("The selected window was minimized");
            const auto focus = [&] {
                auto main = AXUIElementSetAttributeValue(element, kAXMainAttribute, kCFBooleanTrue);
                if (main != kAXErrorSuccess && main != kAXErrorAttributeUnsupported)
                    throw std::runtime_error("Cannot set the selected window's main state");
                // Some apps expose these as read-only. Raising plus exact focus
                // verification remains required even when either write is unsupported.
                AXUIElementSetAttributeValue(app.get(), kAXFocusedWindowAttribute, element);
                AXUIElementSetAttributeValue(element, kAXFocusedAttribute, kCFBooleanTrue);
                if (AXUIElementPerformAction(element, kAXRaiseAction) != kAXErrorSuccess)
                    throw std::runtime_error("Cannot raise the selected window");
            };
            if (cancelled)
                return false;
            focus();
            if (cancelled)
                return false;
            if (frontmostPID() != target.pid && !activatePID(target.pid)) {
                if (cancelled)
                    return false;
                // AppKit may refuse a background resident's activation request.
                // The user explicitly selected this window, so try the permitted
                // Accessibility activation path and still verify exact focus below.
                if (frontmostPID() != target.pid &&
                    AXUIElementSetAttributeValue(app.get(), kAXFrontmostAttribute,
                                                 kCFBooleanTrue) != kAXErrorSuccess)
                    throw std::runtime_error("Cannot activate the selected application");
            }
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            bool raisedAfterActivation = false;
            unsigned matches = 0;
            while (!cancelled && std::chrono::steady_clock::now() < deadline) {
                if (frontmostPID() == target.pid) {
                    if (!raisedAfterActivation) {
                        // App activation can restore a different key window. Apply
                        // the exact selection again after the app is frontmost.
                        focus();
                        raisedAfterActivation = true;
                    }
                    if (cancelled)
                        return false;
                    if (focusedWindow(target.pid) == target && frontmostPID() == target.pid)
                        ++matches;
                    else
                        matches = 0;
                    if (matches == 2)
                        return true;
                } else {
                    matches = 0;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if (cancelled)
                return false;
            throw std::runtime_error("Cannot confirm focus on the selected window");
        }
    }
    throw std::runtime_error("The selected window has closed or is no longer accessible");
}
} // namespace

struct Service::Impl : std::enable_shared_from_this<Service::Impl> {
    explicit Impl(Host callbacks) : host(std::move(callbacks)) {}
    Host host;
    Options options{false, Scope::CurrentDesktop};
    Controller controller;
    Keyboard keyboard;
    CFMachPortRef tap = nullptr;
    CFRunLoopSourceRef source = nullptr;
    dispatch_queue_t worker =
        dispatch_queue_create("dev.spacewing.switcher", DISPATCH_QUEUE_SERIAL);
    Overlay overlay;
    NSMutableArray *observers = [NSMutableArray new];
    uint64_t generation = 0;
    bool loading = false;
    bool activating = false;
    bool finishing = false;
    std::shared_ptr<std::atomic_bool> workCancelled = std::make_shared<std::atomic_bool>(false);

    void report(const std::string &error) {
        try {
            host.reportError(error);
        } catch (...) {
        }
    }
    void cancel() {
        ++generation;
        *workCancelled = true;
        finishing = false;
        loading = false;
        controller.cancel();
        keyboard.cancel(); // Keep swallowed key-downs paired with their key-ups.
        overlay.hide();
    }
    void stop() {
        cancel();
        if (source) {
            CFRunLoopRemoveSource(CFRunLoopGetMain(), source, kCFRunLoopCommonModes);
            CFRelease(source);
            source = nullptr;
        }
        if (tap) {
            CFMachPortInvalidate(tap);
            CFRelease(tap);
            tap = nullptr;
        }
        keyboard.reset();
        auto center = NSWorkspace.sharedWorkspace.notificationCenter;
        for (id observer in observers)
            [center removeObserver:observer];
        [observers removeAllObjects];
    }
    void start() {
        if (tap)
            return;
        if (!trusted())
            throw std::runtime_error(
                "Window switching requires Accessibility access; grant access and restart.");
        const auto mask = CGEventMaskBit(kCGEventKeyDown) | CGEventMaskBit(kCGEventKeyUp) |
                          CGEventMaskBit(kCGEventFlagsChanged);
        tap = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap, kCGEventTapOptionDefault,
                               mask, &event, this);
        if (!tap)
            throw std::runtime_error(
                "Cannot install window switcher keyboard handler; check Accessibility access.");
        source = CFMachPortCreateRunLoopSource(nullptr, tap, 0);
        if (!source) {
            stop();
            throw std::runtime_error("Cannot install window switcher run-loop source");
        }
        CFRunLoopAddSource(CFRunLoopGetMain(), source, kCFRunLoopCommonModes);
        CGEventTapEnable(tap, true);
        auto weak = weak_from_this();
        auto center = NSWorkspace.sharedWorkspace.notificationCenter;
        for (NSNotificationName name in @[
                 NSWorkspaceActiveSpaceDidChangeNotification,
                 NSWorkspaceDidActivateApplicationNotification
             ]) {
            [observers
                addObject:
                    [center addObserverForName:name
                                        object:nil
                                         queue:NSOperationQueue.mainQueue
                                    usingBlock:^(NSNotification *note) {
                                      auto self = weak.lock();
                                      if (!self)
                                          return;
                                      if ([note.name
                                              isEqualToString:
                                                  NSWorkspaceDidActivateApplicationNotification]) {
                                          NSRunningApplication *app =
                                              note.userInfo[NSWorkspaceApplicationKey];
                                          const auto pid = app.processIdentifier;
                                          if (pid != frontmostPID())
                                              return;
                                          // App activation can be notified after
                                          // its exact window was already confirmed.
                                          const auto focused = self->controller.focused();
                                          if (focused && focused->pid == pid)
                                              return;
                                      }
                                      if (!self->activating)
                                          self->cancel();
                                    }]];
        }
        for (NSNotificationName name in
             @[ NSWorkspaceWillSleepNotification, NSWorkspaceSessionDidResignActiveNotification ]) {
            [observers addObject:[center addObserverForName:name
                                                     object:nil
                                                      queue:NSOperationQueue.mainQueue
                                                 usingBlock:^(NSNotification *) {
                                                   if (auto self = weak.lock()) {
                                                       self->cancel();
                                                       self->keyboard.reset();
                                                   }
                                                 }]];
        }
    }
    static CGEventRef event(CGEventTapProxy, CGEventType type, CGEventRef event, void *context) {
        auto &self = *static_cast<Impl *>(context);
        if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
            self.cancel();
            self.keyboard.reset();
            if (self.tap)
                CGEventTapEnable(self.tap, true);
            return event;
        }
        const auto flags = CGEventGetFlags(event);
        const auto code = CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);
        Input input{type == kCGEventKeyDown ? InputType::Down
                    : type == kCGEventKeyUp ? InputType::Up
                                            : InputType::Modifiers,
                    static_cast<Key>(code),
                    ((flags & kCGEventFlagMaskAlternate) ? Option : 0u) |
                        ((flags & kCGEventFlagMaskShift) ? Shift : 0u) |
                        ((flags & kCGEventFlagMaskControl) ? Control : 0u) |
                        ((flags & kCGEventFlagMaskCommand) ? CommandKey : 0u),
                    CGEventGetIntegerValueField(event, kCGKeyboardEventAutorepeat) != 0};
        try {
            const auto decision = self.keyboard.handle(input, self.host.unavailable());
            if (decision.intent == Intent::Commit)
                self.finishing = true;
            if (decision.intent != Intent::None) {
                auto weak = self.weak_from_this();
                const auto generation = self.generation;
                dispatch_async(dispatch_get_main_queue(), ^{
                  if (auto owner = weak.lock(); owner && owner->generation == generation)
                      owner->accept(decision.intent, decision.mode);
                });
            }
            return decision.consume ? nullptr : event;
        } catch (...) {
            self.cancel();
            return event;
        }
    }
    void accept(Intent intent, Mode mode) {
        try {
            if (intent == Intent::Cancel || host.unavailable()) {
                cancel();
                return;
            }
            controller.setPreviewArea(Overlay::availableArea());
            controller.accept(intent, mode);
            pump();
        } catch (const std::exception &e) {
            cancel();
            report(e.what());
        }
    }
    void pump() {
        try {
            if (activating || loading)
                return;
            if (host.unavailable()) {
                cancel();
                return;
            }
            if (controller.phase() == Phase::Picking) {
                show();
                return;
            }
            if (controller.phase() == Phase::Activating) {
                commit();
                return;
            }
            if (controller.phase() == Phase::Discovering) {
                discover();
                return;
            }
            finishing = false;
            overlay.hide();
        } catch (const std::exception &e) {
            cancel();
            report(e.what());
        }
    }
    void discover() {
        host.began();
        loading = true;
        workCancelled = std::make_shared<std::atomic_bool>(false);
        const auto cancelled = workCancelled;
        auto weak = weak_from_this();
        const auto token = generation;
        const auto scope = options.scope;
        const int frontmost = frontmostPID();
        dispatch_async(worker, ^{
          @autoreleasepool {
              Catalog result;
              std::string error;
              try {
                  if (!*cancelled)
                      result = catalog(scope, frontmost, *cancelled);
              } catch (const std::exception &e) {
                  error = e.what();
              }
              dispatch_async(dispatch_get_main_queue(), ^{
                auto self = weak.lock();
                if (!self || token != self->generation)
                    return;
                self->loading = false;
                if (!error.empty()) {
                    self->cancel();
                    self->report(error);
                    return;
                }
                if (self->host.unavailable()) {
                    self->cancel();
                    return;
                }
                self->controller.discovered(std::move(result.windows), result.focused, frontmost);
                self->pump();
              });
          }
        });
    }
    void commit() {
        const auto target = controller.target();
        overlay.hide();
        if (!target) {
            finishing = false;
            return;
        }
        activating = true;
        auto weak = weak_from_this();
        const auto token = generation;
        const auto scope = options.scope;
        const auto cancelled = workCancelled;
        dispatch_async(worker, ^{
          @autoreleasepool {
              std::string error;
              bool confirmed = false;
              try {
                  if (!*cancelled)
                      confirmed = activateWindow(*target, scope, *cancelled);
              } catch (const std::exception &e) {
                  error = e.what();
              }
              dispatch_async(dispatch_get_main_queue(), ^{
                auto self = weak.lock();
                if (!self)
                    return;
                self->activating = false;
                self->finishing = false;
                if (token != self->generation) {
                    self->pump();
                    return;
                }
                if (confirmed && error.empty()) {
                    self->controller.activated(*target);
                    self->report("");
                    self->pump();
                } else {
                    self->cancel();
                    self->report(error);
                }
              });
          }
        });
    }
    void show() { overlay.show(controller.session(), options.layout); }
};

Service::Service(Host host) : impl_(std::make_shared<Impl>(std::move(host))) {}
Service::~Service() { impl_->stop(); }
void Service::configure(Options options) {
    validateShortcuts(options.shortcuts);
    if (options == impl_->options && (!options.enabled || impl_->tap))
        return;
    if (options.enabled)
        impl_->start(); // Failed installation leaves the previous options intact.
    else
        impl_->stop();
    impl_->cancel();
    impl_->keyboard.configure(options.shortcuts, options.layout);
    impl_->controller.setLayout(options.layout);
    impl_->options = options;
}
void Service::cancel() { impl_->cancel(); }
bool Service::activating() const { return impl_->activating; }
bool Service::busy() const {
    return impl_->loading || impl_->controller.phase() != Phase::Idle || impl_->keyboard.active() ||
           impl_->finishing || impl_->activating;
}
bool Service::running() const { return impl_->tap && CGEventTapIsEnabled(impl_->tap); }
} // namespace spacewing::switcher
