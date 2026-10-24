#include "application.h"
#include "result.h"
#import <AppKit/AppKit.h>
#include <fcntl.h>
#include <filesystem>
#include <mach-o/dyld.h>
#include <sys/stat.h>

using namespace spacewing;

@interface SpacewingDelegate : NSObject <NSApplicationDelegate>
@property(copy) void (^ready)(void);
@property(copy) void (^settings)(void);
@property(copy) void (^reload)(void);
- (void)showSettings:(id)sender;
- (void)reloadConfig:(id)sender;
- (void)showAccessibility:(id)sender;
- (void)showPreviewAccess:(id)sender;
@end
@implementation SpacewingDelegate
- (void)applicationDidFinishLaunching:(NSNotification *)note {
    self.ready();
}
- (BOOL)applicationShouldHandleReopen:(NSApplication *)app hasVisibleWindows:(BOOL)visible {
    if (self.settings)
        [self showSettings:nil];
    return NO;
}
- (void)applicationWillTerminate:(NSNotification *)notification {
    closeSettings();
}
- (void)showSettings:(id)sender {
    try {
        self.settings();
    } catch (const std::exception &error) {
        setStatus(error.what());
    }
}
- (void)reloadConfig:(id)sender {
    self.reload();
}
- (void)showAccessibility:(id)sender {
    if (!openAccessibilitySettings())
        setStatus("Cannot open System Settings. Open Privacy & Security > Accessibility manually.");
}
- (void)showPreviewAccess:(id)sender {
    // Request from the resident app so permission belongs to its bundle identity.
    if (@available(macOS 12.3, *)) {
        if (!CGPreflightScreenCaptureAccess())
            CGRequestScreenCaptureAccess();
        if (![NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:
                @"x-apple.systempreferences:com.apple.preference.security?Privacy_ScreenCapture"]])
            setStatus("Open System Settings > Privacy & Security > Screen Recording to enable previews.");
    } else {
        setStatus("Live window previews require macOS 12.3 or later.");
    }
}
@end

namespace spacewing {
namespace {
SpacewingDelegate *delegate;
NSStatusItem *statusItem;
NSMenuItem *statusLine;
NSTask *settingsTask;

class FileWatcher final : public Watcher {
public:
    FileWatcher(std::string path, std::function<void()> callback)
        : path_(std::move(path)), changed_(std::move(callback)) {
        exists_ = stat(path_.c_str(), &metadata_) == 0;
        arm();
    }
    ~FileWatcher() override {
        *alive_ = false;
        if (file_)
            dispatch_source_cancel(file_);
        if (parent_)
            dispatch_source_cancel(parent_);
    }

private:
    dispatch_source_t watch(const std::string &path) {
        int fd = open(path.c_str(), O_EVTONLY | O_CLOEXEC);
        if (fd < 0)
            return nullptr;
        auto source = dispatch_source_create(DISPATCH_SOURCE_TYPE_VNODE, fd,
                                             DISPATCH_VNODE_WRITE | DISPATCH_VNODE_DELETE |
                                                 DISPATCH_VNODE_RENAME | DISPATCH_VNODE_ATTRIB,
                                             dispatch_get_main_queue());
        auto lifetime = alive_;
        dispatch_source_set_event_handler(source, ^{
          if (!*lifetime)
              return;
          const auto generation = ++generation_;
          dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC),
                         dispatch_get_main_queue(), ^{
                           if (!*lifetime || generation != generation_)
                               return;
                           arm();
                           struct stat current{};
                           const bool exists = stat(path_.c_str(), &current) == 0;
                           if (exists != exists_ || current.st_ino != metadata_.st_ino ||
                               current.st_size != metadata_.st_size ||
                               current.st_mtimespec.tv_sec != metadata_.st_mtimespec.tv_sec ||
                               current.st_mtimespec.tv_nsec != metadata_.st_mtimespec.tv_nsec) {
                               exists_ = exists;
                               metadata_ = current;
                               changed_();
                           }
                         });
        });
        dispatch_source_set_cancel_handler(source, ^{
          close(fd);
        });
        dispatch_resume(source);
        return source;
    }
    void arm() {
        if (file_)
            dispatch_source_cancel(file_);
        if (parent_)
            dispatch_source_cancel(parent_);
        file_ = watch(path_);
        const auto slash = path_.find_last_of('/');
        parent_ = watch(path_.substr(0, slash));
    }
    std::string path_;
    std::function<void()> changed_;
    dispatch_source_t file_ = nullptr, parent_ = nullptr;
    uint64_t generation_ = 0;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    struct stat metadata_{};
    bool exists_ = false;
};
} // namespace
void runApplication(const std::function<void()> &ready) {
    initializeApplication();
    delegate = [SpacewingDelegate new];
    delegate.ready = ^{
      try {
          ready();
      } catch (const std::exception &e) {
          fprintf(stderr, "spacewing: %s\n", e.what());
          exit(dynamic_cast<const Error *>(&e) ? static_cast<const Error &>(e).code : 1);
      }
    };
    NSApp.delegate = delegate;
    [NSApp run];
}
void setMenu(const std::function<void()> &settings, const std::function<void()> &reload) {
    auto settingsAction = settings;
    auto reloadAction = reload;
    delegate.settings = ^{
      settingsAction();
    };
    delegate.reload = ^{
      reloadAction();
    };
    statusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
    statusItem.button.title = @"↔";
    statusItem.button.toolTip = @"Spacewing";
    NSMenu *menu = [NSMenu new];
    statusLine = [[NSMenuItem alloc] initWithTitle:@"Spacewing" action:nil keyEquivalent:@""];
    [menu addItem:statusLine];
    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem *setting = [[NSMenuItem alloc] initWithTitle:@"Settings…"
                                                     action:@selector(showSettings:)
                                              keyEquivalent:@""];
    setting.target = delegate;
    [menu addItem:setting];
    NSMenuItem *accessibility = [[NSMenuItem alloc] initWithTitle:@"Accessibility…"
                                                           action:@selector(showAccessibility:)
                                                    keyEquivalent:@""];
    accessibility.target = delegate;
    [menu addItem:accessibility];
    NSMenuItem *previews = [[NSMenuItem alloc] initWithTitle:@"Window Preview Access…"
                                                   action:@selector(showPreviewAccess:)
                                            keyEquivalent:@""];
    previews.target = delegate;
    [menu addItem:previews];
    NSMenuItem *reloadItem = [[NSMenuItem alloc] initWithTitle:@"Reload Config"
                                                        action:@selector(reloadConfig:)
                                                 keyEquivalent:@""];
    reloadItem.target = delegate;
    [menu addItem:reloadItem];
    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem *quit = [[NSMenuItem alloc] initWithTitle:@"Quit Spacewing"
                                                  action:@selector(terminate:)
                                           keyEquivalent:@""];
    quit.target = NSApp;
    [menu addItem:quit];
    statusItem.menu = menu;
}
void setStatus(const std::string &status) {
    NSString *text = [NSString stringWithUTF8String:status.c_str()];
    statusItem.button.toolTip = text;
    statusLine.title =
        [text length] > 100 ? [[text substringToIndex:100] stringByAppendingString:@"…"] : text;
}
void setDesktopNumber(std::optional<size_t> number) {
    auto title = [[NSMutableAttributedString alloc]
        initWithString:@"↔"
            attributes:@{NSFontAttributeName : statusItem.button.font}];
    if (number) {
        auto badge = [[NSAttributedString alloc]
            initWithString:[NSString stringWithFormat:@"\u2009%zu", *number]
                attributes:@{
                    NSFontAttributeName :
                        [NSFont monospacedDigitSystemFontOfSize:9 weight:NSFontWeightSemibold],
                    NSBaselineOffsetAttributeName : @3
                }];
        [title appendAttributedString:badge];
    }
    statusItem.button.attributedTitle = title;
    statusItem.button.accessibilityLabel =
        number ? [NSString stringWithFormat:@"Spacewing, Desktop %zu", *number] : @"Spacewing";
}
void launchSettings() {
    if (settingsTask.running) {
        [[NSRunningApplication
            runningApplicationWithProcessIdentifier:settingsTask.processIdentifier]
            activateWithOptions:0];
        return;
    }
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> executable(size);
    if (_NSGetExecutablePath(executable.data(), &size) != 0)
        throw Error("Cannot locate the settings helper");
    // Resolve the CLI symlink too; the helper has its own app identity and windows.
    const auto directory = std::filesystem::canonical(executable.data()).parent_path();
    auto path = (directory / "../Helpers/SpacewingSettings.app/Contents/MacOS/spacewing-settings")
                    .lexically_normal();
    if (!std::filesystem::exists(path))
        path = directory / "spacewing-settings"; // Unpackaged CMake/debug executable.
    NSString *helper = [NSString stringWithUTF8String:path.c_str()];
    settingsTask = [NSTask new];
    settingsTask.executableURL = [NSURL fileURLWithPath:helper];
    NSError *error = nil;
    if (![settingsTask launchAndReturnError:&error])
        throw Error("Cannot open Settings: " + std::string(error.localizedDescription.UTF8String));
}
void closeSettings() {
    if (settingsTask.running)
        [settingsTask terminate];
}
std::unique_ptr<Watcher> watchConfig(const std::string &path, std::function<void()> changed) {
    return std::make_unique<FileWatcher>(path, std::move(changed));
}
} // namespace spacewing
