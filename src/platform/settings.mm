#include "application.h"
#include "ipc/socket.h"
#include "shortcut.h"
#include "shortcuts.h"
#import <AppKit/AppKit.h>
#include <thread>

using namespace spacewing;
using namespace spacewing::workspace;
namespace {
NSString *ns(const std::string &text) {
    return [NSString stringWithUTF8String:text.c_str()] ?: @"";
}
void exchange(std::vector<std::string> args, std::function<void(Reply)> completion) {
    std::thread([args = std::move(args), completion = std::move(completion)] {
        @autoreleasepool {
            Reply reply;
            try {
                reply = request(args);
            } catch (const Error &e) {
                reply = {e.code, "", e.what()};
            } catch (const std::exception &e) {
                reply = {1, "", e.what()};
            }
            mainAsync([reply = std::move(reply), completion] { completion(reply); });
        }
    }).detach();
}
NSTextField *label(NSString *text, NSRect frame) {
    auto view = [NSTextField wrappingLabelWithString:text];
    view.frame = frame;
    return view;
}
NSButton *button(NSString *text, NSRect frame, id target, SEL action) {
    auto view = [NSButton buttonWithTitle:text target:target action:action];
    view.frame = frame;
    view.bezelStyle = NSBezelStyleRounded;
    return view;
}
} // namespace

@interface SettingsDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate> {
    NSWindow *window_;
    NSArray<NSTextField *> *fields_;
    NSArray<NSButton *> *recordButtons_;
    NSButton *save_;
    NSButton *switcherEnabled_;
    NSPopUpButton *switcherScope_;
    NSTextField *path_;
    NSTextField *message_;
    std::string original_;
    std::string configPath_;
    std::string token_;
    NSInteger recordingRow_;
    NSUInteger generation_;
    bool polling_;
    bool editable_;
    NSTimer *timer_;
    id monitor_;
}
- (void)loadConfig:(id)sender;
- (void)saveConfig:(id)sender;
- (void)record:(NSButton *)sender;
- (void)clear:(NSButton *)sender;
- (void)openConfig:(id)sender;
- (void)permissions:(id)sender;
- (void)endRecording;
- (void)acceptChord:(NSString *)chord;
- (BOOL)capture:(NSEvent *)event;
- (void)pollRecording;
@end

@implementation SettingsDelegate
- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    recordingRow_ = -1;
    window_ =
        [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 700, 550)
                                    styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                              NSWindowStyleMaskMiniaturizable
                                      backing:NSBackingStoreBuffered
                                        defer:NO];
    window_.title = @"Spacewing Settings";
    window_.delegate = self;
    window_.releasedWhenClosed = NO;
    auto view = window_.contentView;
    auto title = label(@"Workspace shortcuts", NSMakeRect(24, 499, 650, 28));
    title.font = [NSFont boldSystemFontOfSize:20];
    [view addSubview:title];
    [view
        addSubview:label(@"Shortcuts follow the focused window’s display. One move per key press.",
                         NSMakeRect(24, 467, 650, 24))];
    NSArray<NSString *> *names =
        @[ @"Switch left", @"Switch right", @"Carry window left", @"Carry window right" ];
    NSMutableArray *fields = [NSMutableArray new];
    NSMutableArray *records = [NSMutableArray new];
    for (NSInteger i = 0; i < 4; ++i) {
        CGFloat y = 417 - i * 42;
        [view addSubview:label(names[i], NSMakeRect(24, y + 3, 155, 24))];
        NSTextField *field = [[NSTextField alloc] initWithFrame:NSMakeRect(184, y, 310, 28)];
        field.placeholderString = @"Disabled";
        [fields addObject:field];
        [view addSubview:field];
        NSButton *record =
            button(@"Record", NSMakeRect(504, y - 2, 88, 32), self, @selector(record:));
        record.tag = i;
        [records addObject:record];
        [view addSubview:record];
        NSButton *clear = button(@"Clear", NSMakeRect(598, y - 2, 76, 32), self, @selector(clear:));
        clear.tag = i;
        [view addSubview:clear];
    }
    switcherEnabled_ = [NSButton checkboxWithTitle:@"Enable window switcher" target:nil action:nil];
    switcherEnabled_.frame = NSMakeRect(24, 235, 430, 26);
    [view addSubview:switcherEnabled_];
    [view addSubview:label(@"Switcher shortcut", NSMakeRect(24, 199, 155, 24))];
    auto switcherField = [[NSTextField alloc] initWithFrame:NSMakeRect(184, 196, 310, 28)];
    switcherField.placeholderString = @"option+tab";
    [fields addObject:switcherField];
    [view addSubview:switcherField];
    auto switcherRecord = button(@"Record", NSMakeRect(504, 194, 88, 32), self, @selector(record:));
    switcherRecord.tag = 4;
    [records addObject:switcherRecord];
    [view addSubview:switcherRecord];
    auto switcherReset = button(@"Reset", NSMakeRect(598, 194, 76, 32), self, @selector(clear:));
    switcherReset.tag = 4;
    [view addSubview:switcherReset];
    fields_ = fields;
    recordButtons_ = records;
    [view addSubview:label(@"Window scope", NSMakeRect(24, 157, 155, 24))];
    switcherScope_ = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(184, 154, 310, 28)
                                                pullsDown:NO];
    [switcherScope_ addItemsWithTitles:@[ @"Current desktop", @"All desktops" ]];
    [view addSubview:switcherScope_];
    message_ = label(@"Loading…", NSMakeRect(24, 94, 650, 48));
    message_.textColor = NSColor.secondaryLabelColor;
    [view addSubview:message_];
    path_ = label(@"", NSMakeRect(24, 64, 650, 24));
    path_.selectable = YES;
    path_.font = [NSFont systemFontOfSize:11];
    [view addSubview:path_];
    [view addSubview:button(@"Open Config", NSMakeRect(18, 18, 112, 32), self,
                            @selector(openConfig:))];
    [view addSubview:button(@"Accessibility…", NSMakeRect(132, 18, 130, 32), self,
                            @selector(permissions:))];
    [view addSubview:button(@"Reload", NSMakeRect(484, 18, 86, 32), self, @selector(loadConfig:))];
    save_ = button(@"Save", NSMakeRect(580, 18, 100, 32), self, @selector(saveConfig:));
    save_.keyEquivalent = @"s";
    save_.keyEquivalentModifierMask = NSEventModifierFlagCommand;
    [view addSubview:save_];
    __weak SettingsDelegate *weakSelf = self;
    monitor_ =
        [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
                                              handler:^NSEvent *(NSEvent *event) {
                                                return [weakSelf capture:event] ? nil : event;
                                              }];
    [window_ center];
    [window_ makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    [self loadConfig:nil];
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    return YES;
}
- (void)applicationWillTerminate:(NSNotification *)note {
    [self endRecording];
    if (monitor_)
        [NSEvent removeMonitor:monitor_];
}
- (void)windowDidResignKey:(NSNotification *)notification {
    [self endRecording];
}
- (void)loadConfig:(id)sender {
    [self endRecording];
    save_.enabled = NO;
    exchange({"__settings-read"}, [self](Reply reply) {
        if (reply.code || reply.values.size() != 11) {
            message_.stringValue =
                ns(reply.error.empty() ? "Cannot read resident settings." : reply.error);
            return;
        }
        configPath_ = reply.values[0];
        original_ = reply.values[1];
        editable_ = reply.values[2] == "ready";
        path_.stringValue = ns(configPath_);
        message_.stringValue =
            ns(reply.values[3].empty()
                   ? "Record a shortcut, type a chord, or clear a binding to disable it."
                   : reply.values[3]);
        for (NSUInteger i = 0; i < 4; ++i) {
            fields_[i].stringValue = ns(reply.values[4 + i]);
            fields_[i].enabled = editable_;
            recordButtons_[i].enabled = editable_;
        }
        fields_[4].stringValue = ns(reply.values[10]);
        fields_[4].enabled = recordButtons_[4].enabled = editable_;
        switcherEnabled_.state =
            reply.values[8] == "true" ? NSControlStateValueOn : NSControlStateValueOff;
        [switcherScope_ selectItemAtIndex:reply.values[9] == "all-desktops" ? 1 : 0];
        switcherEnabled_.enabled = switcherScope_.enabled = editable_;
        save_.enabled = editable_;
    });
}
- (void)saveConfig:(id)sender {
    if (!editable_)
        return;
    [self endRecording];
    std::vector<std::string> args{"__settings-save", original_};
    for (NSUInteger i = 0; i < 4; ++i)
        args.emplace_back(fields_[i].stringValue.UTF8String);
    args.emplace_back(switcherEnabled_.state == NSControlStateValueOn ? "true" : "false");
    args.emplace_back(switcherScope_.indexOfSelectedItem == 1 ? "all-desktops" : "current-desktop");
    args.emplace_back(fields_[4].stringValue.UTF8String);
    save_.enabled = NO;
    exchange(std::move(args), [self](Reply reply) {
        save_.enabled = editable_;
        if (reply.code) {
            message_.stringValue = ns(reply.error);
            return;
        }
        if (!reply.values.empty())
            original_ = reply.values[0];
        message_.stringValue = reply.values.size() > 1 && !reply.values[1].empty()
                                   ? ns("Saved. Window switcher: " + reply.values[1])
                                   : @"Saved. Shortcuts and window switching have been updated.";
    });
}
- (void)record:(NSButton *)sender {
    [self endRecording];
    recordingRow_ = sender.tag;
    NSUInteger generation = ++generation_;
    message_.stringValue = @"Starting shortcut recorder…";
    exchange({"__record-start"}, [self, generation](Reply reply) {
        if (generation != generation_) {
            if (!reply.code && !reply.values.empty())
                exchange({"__record-end", reply.values[0]}, [](Reply) {});
            return;
        }
        if (reply.code || reply.values.empty()) {
            message_.stringValue = ns(reply.error);
            [self endRecording];
            return;
        }
        token_ = reply.values[0];
        message_.stringValue = recordingRow_ == 4
                                   ? @"Press a modifier with Tab, Space, a letter, or another key. "
                                      "Escape cancels."
                                   : @"Press Control / Shift / Option / Command with ← or →. "
                                      "Escape cancels.";
        [window_ makeFirstResponder:window_];
        timer_ = [NSTimer scheduledTimerWithTimeInterval:0.1
                                                 repeats:YES
                                                   block:^(NSTimer *) {
                                                     [self pollRecording];
                                                   }];
    });
}
- (void)pollRecording {
    if (token_.empty() || polling_)
        return;
    polling_ = true;
    const auto token = token_;
    exchange({"__record-poll", token}, [self, token](Reply reply) {
        polling_ = false;
        if (token != token_)
            return;
        if (reply.code) {
            [self endRecording];
            message_.stringValue = ns(reply.error);
            return;
        }
        if (!reply.values.empty() && !reply.values[0].empty())
            [self acceptChord:ns(reply.values[0])];
    });
}
- (BOOL)capture:(NSEvent *)event {
    if (recordingRow_ < 0)
        return NO;
    if (event.keyCode == 53) {
        [self endRecording];
        message_.stringValue = @"Recording cancelled.";
        return YES;
    }
    if (token_.empty())
        return YES;
    if (recordingRow_ == 4) {
        unsigned modifiers = 0;
        if (event.modifierFlags & NSEventModifierFlagControl)
            modifiers |= switcher::Control;
        if (event.modifierFlags & NSEventModifierFlagShift)
            modifiers |= switcher::Shift;
        if (event.modifierFlags & NSEventModifierFlagOption)
            modifiers |= switcher::Option;
        if (event.modifierFlags & NSEventModifierFlagCommand)
            modifiers |= switcher::CommandKey;
        const auto chord = switcher::Shortcut{modifiers, static_cast<switcher::Key>(event.keyCode)};
        try {
            [self acceptChord:ns(switcher::parseShortcut(chord.text()).text())];
        } catch (const std::invalid_argument &) {
            message_.stringValue = @"Hold a modifier with a supported key. Up/Down navigate the "
                                    "picker; Escape cancels.";
        }
        return YES;
    }
    if (event.keyCode != 123 && event.keyCode != 124)
        return YES;
    Shortcut shortcut;
    shortcut.key = event.keyCode;
    auto flags = event.modifierFlags;
    if (flags & NSEventModifierFlagControl)
        shortcut.modifiers |= Control;
    if (flags & NSEventModifierFlagShift)
        shortcut.modifiers |= Shift;
    if (flags & NSEventModifierFlagOption)
        shortcut.modifiers |= Option;
    if (flags & NSEventModifierFlagCommand)
        shortcut.modifiers |= CommandKey;
    if (!shortcut.modifiers) {
        message_.stringValue = @"Hold at least one modifier with the arrow key.";
        return YES;
    }
    [self acceptChord:ns(shortcut.text())];
    return YES;
}
- (void)acceptChord:(NSString *)chord {
    if (recordingRow_ < 0)
        return;
    fields_[recordingRow_].stringValue = chord;
    [self endRecording];
    message_.stringValue = @"Shortcut recorded. Save to apply it.";
}
- (void)endRecording {
    ++generation_;
    recordingRow_ = -1;
    [timer_ invalidate];
    timer_ = nil;
    if (!token_.empty())
        exchange({"__record-end", token_}, [](Reply) {});
    token_.clear();
}
- (void)clear:(NSButton *)sender {
    if (!editable_)
        return;
    [self endRecording];
    fields_[sender.tag].stringValue = sender.tag == 4 ? @"option+tab" : @"";
}
- (void)openConfig:(id)sender {
    if (!configPath_.empty())
        [NSWorkspace.sharedWorkspace openURL:[NSURL fileURLWithPath:ns(configPath_)]];
}
- (void)permissions:(id)sender {
    if (!openAccessibilitySettings())
        message_.stringValue =
            @"Cannot open System Settings. Open Privacy & Security > Accessibility manually.";
}
@end

int main() {
    @autoreleasepool {
        initializeApplication();
        static SettingsDelegate *settings;
        settings = [SettingsDelegate new];
        NSApp.delegate = settings;
        [NSApp run];
    }
}
