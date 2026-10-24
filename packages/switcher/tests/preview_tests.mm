#include "overlay.h"
#include "previews.h"
#import <AppKit/AppKit.h>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

using namespace spacewing;
using namespace spacewing::switcher;

namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void runFor(double seconds) {
    const auto deadline = [NSDate dateWithTimeIntervalSinceNow:seconds];
    while (deadline.timeIntervalSinceNow > 0)
        [NSRunLoop.mainRunLoop runMode:NSDefaultRunLoopMode beforeDate:deadline];
}
NSWindow *picker() {
    for (NSWindow *window in NSApp.windows)
        if ([window.identifier isEqualToString:@"spacewing.switcher.overlay"])
            return window;
    return nil;
}
NSArray<NSView *> *tiles(NSWindow *panel) {
    auto result = [NSMutableArray new];
    for (NSView *view in panel.contentView.subviews)
        if ([view.identifier hasPrefix:@"window-"])
            [result addObject:view];
    return result;
}
void overlayChecks(const char *renderPath) {
    Overlay overlay;
    Session session;
    std::vector<Candidate> windows;
    const char *applications[] = {"Editor", "Browser", "Terminal", "Notes"};
    for (uint32_t i = 0; i < 13; ++i)
        windows.push_back(
            {{getpid(), 1000000 + i}, applications[i % 4], "Window " + std::to_string(i + 1)});
    session.begin(windows, windows[0].id);
    overlay.show(session);
    auto panel = picker();
    check(panel && panel.visible && !panel.keyWindow && !panel.mainWindow,
          "preview panel must remain nonactivating");
    auto initialTiles = tiles(panel);
    check(initialTiles.count == windows.size(), "all windows must appear without paging");
    const auto grid = previewGrid(windows.size(), Overlay::availableArea());
    check(panel.frame.size.width <= Overlay::availableArea().width &&
              panel.frame.size.height <= Overlay::availableArea().height,
          "panel must fit the screen");
    for (NSView *tile in initialTiles)
        check(NSContainsRect(panel.contentView.bounds, tile.frame) && tile.frame.size.width <= 196,
              "every compact tile must be inside the panel");
    session.move(Direction::Down, grid.columns);
    overlay.show(session);
    check([tiles(panel) isEqualToArray:initialTiles],
          "moving selection must reuse tile views and their capture streams");
    check([[initialTiles[session.selected()] accessibilityValue] isEqual:@"Selected"],
          "spatial selection must update the highlighted tile");
    if (renderPath) {
        auto view = panel.contentView;
        [view displayIfNeeded];
        auto bitmap = [view bitmapImageRepForCachingDisplayInRect:view.bounds];
        [view cacheDisplayInRect:view.bounds toBitmapImageRep:bitmap];
        NSData *png = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
        check([png writeToFile:[NSString stringWithUTF8String:renderPath] atomically:YES],
              "render preview image");
    }
    overlay.show(session, Layout::List);
    check(tiles(panel).count == 0, "list layout must remove every preview tile");
    size_t rows = 0;
    bool selected = false;
    for (NSView *view in panel.contentView.subviews) {
        if (![view.identifier hasPrefix:@"list-window-"])
            continue;
        ++rows;
        selected = selected || [[view accessibilityValue] isEqual:@"Selected"];
        check(NSContainsRect(panel.contentView.bounds, view.frame), "list rows must fit the panel");
    }
    check(rows > 0 && rows <= 9 && selected,
          "original list shows up to nine rows including selection");
    session.begin(windows, windows[0].id);
    session.step(true);
    session.step(true); // Wrap to the final item, beyond the first nine rows.
    overlay.show(session, Layout::List);
    selected = false;
    for (NSView *view in panel.contentView.subviews)
        if ([view.identifier isEqualToString:@"list-window-1000012"])
            selected = [[view accessibilityValue] isEqual:@"Selected"];
    check(selected, "list scrolls to keep the selected window visible after wrap");
    overlay.show(session, Layout::Grid);
    check(tiles(panel).count == windows.size(), "changing back to the grid restores every tile");
    overlay.hide();
    check(!panel.visible && tiles(panel).count == 0,
          "hiding must discard preview views and frames");
    session.begin({windows[0]}, windows[0].id);
    overlay.show(session);
    check(tiles(panel).count == 1, "filtered grid must discard windows from the previous session");
    overlay.hide();
    // Let pending deferred capture requests run after cancellation.
    runFor(0.2);
    check(!panel.visible, "late capture work must not reopen the picker");

    int callbacks = 0;
    {
        Previews previews;
        previews.show({windows[0].id}, [&](WindowID id, CFRef<CGImageRef>, PreviewStatus status) {
            check(id == windows[0].id && status != PreviewStatus::Live,
                  "initial capture result must describe the requested window");
            ++callbacks;
        });
        check(callbacks == 1, "capture availability must be delivered immediately");
        previews.stop();
    }
    runFor(0.2);
    check(callbacks == 1, "destroying the preview owner must invalidate pending callbacks");
}
int captureChecks() {
    if (Previews::availability() != PreviewStatus::Loading) {
        std::cout << "SKIP: live capture requires an existing Screen Recording grant\n";
        return 77;
    }
    // Capture only a test-owned window. Never prompt or capture another application.
    auto window = [[NSWindow alloc] initWithContentRect:NSMakeRect(40, 40, 200, 140)
                                              styleMask:NSWindowStyleMaskBorderless
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
    window.releasedWhenClosed = NO;
    window.backgroundColor = NSColor.redColor;
    [window orderFrontRegardless];
    const WindowID id{getpid(), static_cast<uint32_t>(window.windowNumber)};
    Previews previews;
    CFRef<CFDataRef> firstFrame;
    int frames = 0;
    bool changed = false;
    previews.show({id}, [&](WindowID received, CFRef<CGImageRef> image, PreviewStatus status) {
        if (status != PreviewStatus::Live)
            return;
        check(received == id && image, "live frame must belong to the requested window");
        ++frames;
        CFRef<CFDataRef> data(CGDataProviderCopyData(CGImageGetDataProvider(image.get())));
        if (!firstFrame) {
            firstFrame = data;
            window.backgroundColor = NSColor.blueColor;
            [window display];
        } else if (!CFEqual(firstFrame.get(), data.get())) {
            changed = true;
        }
    });
    const auto deadline = [NSDate dateWithTimeIntervalSinceNow:8];
    while (!changed && deadline.timeIntervalSinceNow > 0)
        runFor(0.05);
    previews.stop();
    const auto stoppedFrames = frames;
    runFor(0.3);
    [window close];
    check(changed && frames >= 2, "live preview must update when the source window changes");
    check(frames == stoppedFrames, "capture must stop delivering frames after cancellation");
    return 0;
}
} // namespace

int main(int argc, char **argv) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        if (NSScreen.screens.count == 0)
            return 77;
        try {
            if (argc > 1 && std::string(argv[1]) == "--capture")
                return captureChecks();
            overlayChecks(argc > 1 ? argv[1] : nullptr);
            std::cout << "Native preview layout and cancellation checks passed\n";
        } catch (const std::exception &error) {
            std::cerr << "FAIL: " << error.what() << '\n';
            return 1;
        }
    }
}
