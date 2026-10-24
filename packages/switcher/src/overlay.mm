#include "overlay.h"
#include "preview_layout.h"
#include "previews.h"
#import <AppKit/AppKit.h>
#include <algorithm>

using namespace spacewing;
using namespace spacewing::switcher;

namespace {
NSString *ns(const std::string &text) {
    NSString *value = [NSString stringWithUTF8String:text.c_str()];
    return value ? value : @"";
}
NSTextField *label(NSString *text, NSRect frame, CGFloat size, NSFontWeight weight) {
    auto view = [NSTextField labelWithString:text];
    view.frame = frame;
    view.font = [NSFont systemFontOfSize:size weight:weight];
    view.lineBreakMode = NSLineBreakByTruncatingTail;
    return view;
}
NSScreen *pickerScreen() {
    NSScreen *screen = NSScreen.mainScreen;
    for (NSScreen *candidate in NSScreen.screens)
        if (NSPointInRect(NSEvent.mouseLocation, candidate.frame))
            screen = candidate;
    return screen;
}
} // namespace

@interface SWPreviewTile : NSView
@property(strong) NSImageView *preview;
@property(strong) NSImageView *placeholder;
@property(strong) NSTextField *status;
@property(strong) NSTextField *live;
- (instancetype)initWithCandidate:(const Candidate &)candidate frame:(NSRect)frame;
- (void)select:(BOOL)selected;
- (void)updateImage:(CGImageRef)image status:(PreviewStatus)status;
@end

@implementation SWPreviewTile
- (instancetype)initWithCandidate:(const Candidate &)candidate frame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (!self)
        return nil;
    self.wantsLayer = YES;
    self.layer.cornerRadius = 9;
    self.layer.masksToBounds = YES;
    const CGFloat width = frame.size.width, height = frame.size.height;
    const CGFloat metadata = std::min(44.0, height * 0.32);
    const CGFloat inset = std::min(8.0, width * 0.04);
    const NSRect imageFrame =
        NSMakeRect(inset, metadata, width - 2 * inset, height - metadata - inset);
    auto background = [[NSView alloc] initWithFrame:imageFrame];
    background.wantsLayer = YES;
    background.layer.backgroundColor = [NSColor.blackColor colorWithAlphaComponent:0.16].CGColor;
    background.layer.cornerRadius = 7;
    [self addSubview:background];
    self.preview = [[NSImageView alloc] initWithFrame:imageFrame];
    self.preview.imageScaling = NSImageScaleProportionallyUpOrDown;
    [self addSubview:self.preview];
    NSImage *icon =
        [NSRunningApplication runningApplicationWithProcessIdentifier:candidate.id.pid].icon;
    if (!icon)
        icon = [NSImage imageWithSystemSymbolName:@"macwindow" accessibilityDescription:nil];
    self.placeholder = [[NSImageView alloc]
        initWithFrame:NSMakeRect((width - 32) / 2, NSMidY(imageFrame) - 10, 32, 32)];
    self.placeholder.image = icon;
    self.placeholder.imageScaling = NSImageScaleProportionallyUpOrDown;
    [self addSubview:self.placeholder];
    self.status = label(@"Loading preview…", NSMakeRect(6, NSMidY(imageFrame) - 30, width - 12, 16),
                        9, NSFontWeightRegular);
    self.status.alignment = NSTextAlignmentCenter;
    self.status.textColor = [NSColor.labelColor colorWithAlphaComponent:0.75];
    [self addSubview:self.status];
    self.live =
        label(@"LIVE", NSMakeRect(inset + 4, height - inset - 17, 32, 13), 8, NSFontWeightSemibold);
    self.live.textColor = NSColor.whiteColor;
    self.live.backgroundColor = [NSColor.blackColor colorWithAlphaComponent:0.65];
    self.live.drawsBackground = YES;
    self.live.alignment = NSTextAlignmentCenter;
    self.live.hidden = YES;
    [self addSubview:self.live];
    const CGFloat iconSize = metadata < 32 ? 14 : 20;
    const CGFloat textX = 14 + iconSize;
    auto appIcon = [[NSImageView alloc]
        initWithFrame:NSMakeRect(8, (metadata - iconSize) / 2, iconSize, iconSize)];
    appIcon.image = icon;
    appIcon.imageScaling = NSImageScaleProportionallyUpOrDown;
    [self addSubview:appIcon];
    auto title = label(ns(candidate.title.empty() ? candidate.application : candidate.title),
                       NSMakeRect(textX, metadata < 32 ? (metadata - 14) / 2 : metadata / 2,
                                  width - textX - 8, metadata < 32 ? 14 : 17),
                       metadata < 32 ? 9 : 11, NSFontWeightSemibold);
    [self addSubview:title];
    auto application =
        label(ns(candidate.application), NSMakeRect(34, 5, width - 42, 14), 9, NSFontWeightRegular);
    application.textColor = [NSColor.labelColor colorWithAlphaComponent:0.75];
    application.hidden = metadata < 32;
    [self addSubview:application];
    self.toolTip = ns(candidate.application + " — " + candidate.title);
    [self setAccessibilityElement:YES];
    [self setAccessibilityRole:NSAccessibilityGroupRole];
    [self setAccessibilityLabel:self.toolTip];
    return self;
}
- (void)select:(BOOL)selected {
    self.layer.borderWidth = selected ? 3 : 1;
    self.layer.borderColor = selected
                                 ? NSColor.controlAccentColor.CGColor
                                 : [NSColor.separatorColor colorWithAlphaComponent:0.3].CGColor;
    self.layer.backgroundColor =
        selected ? [NSColor.controlAccentColor colorWithAlphaComponent:0.17].CGColor
                 : [NSColor.controlBackgroundColor colorWithAlphaComponent:0.35].CGColor;
    [self setAccessibilityValue:selected ? @"Selected" : @""];
}
- (void)updateImage:(CGImageRef)image status:(PreviewStatus)status {
    self.preview.image = image ? [[NSImage alloc] initWithCGImage:image size:NSZeroSize] : nil;
    self.placeholder.hidden = image != nullptr;
    self.status.hidden = image != nullptr || self.frame.size.width < 130;
    self.live.hidden = image == nullptr;
    switch (status) {
    case PreviewStatus::Loading:
        self.status.stringValue = @"Loading preview…";
        break;
    case PreviewStatus::PermissionRequired:
        self.status.stringValue = @"Preview access required";
        break;
    case PreviewStatus::Unsupported:
        self.status.stringValue = @"Preview unavailable";
        break;
    case PreviewStatus::Unavailable:
        self.status.stringValue = @"Preview unavailable";
        break;
    case PreviewStatus::Live:
        self.status.stringValue = @"";
        break;
    }
}
@end

namespace spacewing::switcher {
struct Overlay::Impl {
    NSPanel *panel = nil;
    NSTextField *heading = nil;
    NSTextField *position = nil;
    NSMutableArray<SWPreviewTile *> *tiles = [NSMutableArray new];
    std::vector<WindowID> visible;
    NSSize size = NSZeroSize;
    PreviewStatus availability = PreviewStatus::Unsupported;
    Layout layout = Layout::Grid;
    Previews previews;

    ~Impl() {
        hide();
        [panel close];
    }
    void hide() {
        previews.stop();
        visible.clear();
        [tiles removeAllObjects];
        panel.contentView = nil; // Release the last captured frames when the picker closes.
        heading = position = nil;
        [panel orderOut:nil];
    }
    void showList(const Session &session, NSRect screenFrame) {
        const auto &windows = session.windows();
        const auto area = Overlay::availableArea();
        const size_t capacity = std::max<size_t>(1, std::min(9.0, (area.height - 66) / 54));
        const size_t count = std::min(capacity, windows.size());
        const size_t start =
            std::min(session.selected() > count / 2 ? session.selected() - count / 2 : 0,
                     windows.size() - count);
        const CGFloat width = std::min(600.0, area.width), height = 66 + count * 54;
        auto view = [[NSVisualEffectView alloc] initWithFrame:NSMakeRect(0, 0, width, height)];
        view.material = NSVisualEffectMaterialHUDWindow;
        view.state = NSVisualEffectStateActive;
        view.wantsLayer = YES;
        view.layer.cornerRadius = 16;
        view.layer.masksToBounds = YES;
        auto title = label([NSString stringWithFormat:@"Switch windows    %zu / %zu",
                                                      session.selected() + 1, windows.size()],
                           NSMakeRect(22, height - 35, width - 44, 22), 15, NSFontWeightBold);
        [view addSubview:title];
        for (size_t i = 0; i < count; ++i) {
            const auto &window = windows[start + i];
            const CGFloat y = height - 52 - (i + 1) * 54;
            auto row = [[NSView alloc] initWithFrame:NSMakeRect(12, y, width - 24, 50)];
            row.identifier = [NSString stringWithFormat:@"list-window-%u", window.id.number];
            if (start + i == session.selected()) {
                row.wantsLayer = YES;
                row.layer.backgroundColor =
                    [NSColor.controlAccentColor colorWithAlphaComponent:0.3].CGColor;
                row.layer.cornerRadius = 8;
            }
            [row setAccessibilityElement:YES];
            [row setAccessibilityRole:NSAccessibilityGroupRole];
            [row setAccessibilityLabel:ns(window.application + " — " + window.title)];
            [row setAccessibilityValue:start + i == session.selected() ? @"Selected" : @""];
            auto icon = [[NSImageView alloc] initWithFrame:NSMakeRect(12, 9, 32, 32)];
            icon.image =
                [NSRunningApplication runningApplicationWithProcessIdentifier:window.id.pid].icon;
            [row addSubview:icon];
            [row addSubview:label(ns(window.title.empty() ? window.application : window.title),
                                  NSMakeRect(58, 25, width - 96, 20), 14, NSFontWeightMedium)];
            auto name = label(ns(window.application), NSMakeRect(58, 7, width - 96, 17), 11,
                              NSFontWeightRegular);
            name.textColor = [NSColor.labelColor colorWithAlphaComponent:0.75];
            [row addSubview:name];
            [view addSubview:row];
        }
        panel.contentView = view;
        [panel setFrame:NSMakeRect(NSMidX(screenFrame) - width / 2,
                                   NSMidY(screenFrame) - height / 2, width, height)
                display:YES];
        [panel orderFrontRegardless];
    }
    void show(const Session &session, Layout requestedLayout) {
        if (!session.active()) {
            hide();
            return;
        }
        if (layout != requestedLayout) {
            hide(); // Changing to the list releases all preview streams and frames.
            layout = requestedLayout;
        }
        if (!panel) {
            panel = [[NSPanel alloc] initWithContentRect:NSZeroRect
                                               styleMask:NSWindowStyleMaskBorderless |
                                                         NSWindowStyleMaskNonactivatingPanel
                                                 backing:NSBackingStoreBuffered
                                                   defer:NO];
            panel.identifier = @"spacewing.switcher.overlay";
            panel.releasedWhenClosed = NO;
            panel.level = NSFloatingWindowLevel;
            panel.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces |
                                       NSWindowCollectionBehaviorFullScreenAuxiliary;
            panel.hidesOnDeactivate = NO;
            panel.becomesKeyOnlyIfNeeded = YES;
            panel.ignoresMouseEvents = YES;
            panel.opaque = NO;
            panel.backgroundColor = NSColor.clearColor;
            panel.hasShadow = YES;
        }
        NSScreen *screen = pickerScreen();
        if (!screen)
            return;
        const auto screenFrame = screen.visibleFrame;
        if (layout == Layout::List) {
            showList(session, screenFrame);
            return;
        }
        const auto &windows = session.windows();
        const auto grid = previewGrid(windows.size(), Overlay::availableArea());
        const auto [columns, rows, tileWidth, tileHeight, gap, width, height] = grid;
        const CGFloat margin = (width - columns * tileWidth - (columns - 1) * gap) / 2;
        std::vector<WindowID> ids;
        for (const auto &window : windows)
            ids.push_back(window.id);
        const auto access = Previews::availability();
        if (ids != visible || !NSEqualSizes(size, NSMakeSize(width, height)) ||
            access != availability) {
            previews.stop();
            visible = ids;
            availability = access;
            size = NSMakeSize(width, height);
            [tiles removeAllObjects];
            auto view = [[NSVisualEffectView alloc] initWithFrame:NSMakeRect(0, 0, width, height)];
            view.material = NSVisualEffectMaterialHUDWindow;
            view.state = NSVisualEffectStateActive;
            view.wantsLayer = YES;
            view.layer.cornerRadius = 18;
            view.layer.masksToBounds = YES;
            heading = label(@"Switch windows", NSMakeRect(18, height - 33, width - 120, 22), 14,
                            NSFontWeightSemibold);
            [view addSubview:heading];
            position =
                label(@"", NSMakeRect(width - 96, height - 31, 78, 20), 12, NSFontWeightMedium);
            position.alignment = NSTextAlignmentRight;
            position.textColor = [NSColor.labelColor colorWithAlphaComponent:0.75];
            [view addSubview:position];
            for (size_t i = 0; i < windows.size(); ++i) {
                const auto frame = NSMakeRect(margin + (i % columns) * (tileWidth + gap),
                                              height - PreviewGrid::header - tileHeight -
                                                  (i / columns) * (tileHeight + gap),
                                              tileWidth, tileHeight);
                auto tile = [[SWPreviewTile alloc] initWithCandidate:windows[i] frame:frame];
                tile.identifier = [NSString stringWithFormat:@"window-%u", windows[i].id.number];
                [tiles addObject:tile];
                [view addSubview:tile];
            }
            auto help = label(@"← ↑ ↓ → Navigate   ·   Release to switch   ·   Esc Cancel",
                              NSMakeRect(12, 33, width - 24, 17), 11, NSFontWeightRegular);
            help.alignment = NSTextAlignmentCenter;
            help.textColor = [NSColor.labelColor colorWithAlphaComponent:0.75];
            [view addSubview:help];
            NSString *detail = access == PreviewStatus::PermissionRequired
                                   ? @"Enable Screen Recording in System Settings for previews"
                               : access == PreviewStatus::Unsupported
                                   ? @"Live previews require macOS 12.3 or later."
                                   : @"Tab to cycle   ·   Shift to reverse";
            auto note = label(detail, NSMakeRect(12, 12, width - 24, 17), 10, NSFontWeightRegular);
            note.alignment = NSTextAlignmentCenter;
            note.textColor = [NSColor.labelColor colorWithAlphaComponent:0.75];
            [view addSubview:note];
            panel.contentView = view;
            // The callback owns the current tiles, never the overlay or service.
            NSArray<SWPreviewTile *> *currentTiles = [tiles copy];
            previews.show(ids, [ids, currentTiles](WindowID id, CFRef<CGImageRef> image,
                                                   PreviewStatus state) {
                const auto it = std::find(ids.begin(), ids.end(), id);
                if (it != ids.end())
                    [currentTiles[it - ids.begin()] updateImage:image.get() status:state];
            });
        }
        position.stringValue =
            [NSString stringWithFormat:@"%zu / %zu", session.selected() + 1, windows.size()];
        for (size_t i = 0; i < windows.size(); ++i)
            [tiles[i] select:i == session.selected()];
        [panel setFrame:NSMakeRect(NSMidX(screenFrame) - width / 2,
                                   NSMidY(screenFrame) - height / 2, width, height)
                display:YES];
        [panel orderFrontRegardless]; // Does not take keyboard focus from the selected application.
    }
};

Overlay::Overlay() : impl_(std::make_unique<Impl>()) {}
Overlay::~Overlay() = default;
void Overlay::show(const Session &session, Layout layout) { impl_->show(session, layout); }
void Overlay::hide() { impl_->hide(); }
PreviewArea Overlay::availableArea() {
    const auto frame = pickerScreen().visibleFrame;
    return {std::max(160.0, frame.size.width - 40), std::max(160.0, frame.size.height - 40)};
}
} // namespace spacewing::switcher
