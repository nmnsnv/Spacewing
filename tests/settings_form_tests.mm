#include "platform/settings_form.h"
#include <iostream>
#include <set>
#include <stdexcept>

using namespace spacewing;

namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
NSControl *controlFor(NSView *view, const std::string &key) {
    for (NSView *child in view.subviews)
        if ([child.identifier isEqualToString:[NSString stringWithUTF8String:key.c_str()]])
            return (NSControl *)child;
    return nil;
}
void verify(const char *renderPath) {
    SettingsForm form(nil, @selector(record:), @selector(clear:));
    const auto original = defaultConfig();
    const auto defaults = parseConfig(original);
    form.load(defaults, true);
    size_t count = 0;
    std::set<std::string> keys;
    for (const auto &section : settingsSchema()) {
        check(!section.title.empty(), "every config section must have a Settings heading");
        for (const auto &setting : section.settings) {
            const auto key = section.key + "." + setting.key;
            check(keys.insert(key).second && !setting.label.empty(),
                  "each file setting must have a unique key and a window label");
            NSControl *control = controlFor(form.view(), key);
            check(control && control.enabled && !control.hidden,
                  "every file setting must have a visible, editable Settings control");
            check(NSContainsRect(form.view().bounds, control.frame),
                  "all registered controls must fit inside the scrolling form");
            check(&form.definition(count) == &setting &&
                      control.tag == static_cast<NSInteger>(count),
                  "controls and recorder tags must reference the exact schema entry");
            check([[control accessibilityLabel]
                      isEqualToString:[NSString stringWithUTF8String:setting.label.c_str()]],
                  "control labels must come from the config schema");
            const auto roundTrip = [&](const SettingValue &expected) {
                const auto edited = form.read();
                check(settingValue(setting, edited) == expected,
                      "a control edit must update the configured value");
                const auto parsed = parseConfig(editedConfig(original, edited));
                check(settingValue(setting, parsed) == expected,
                      "every control edit must survive file serialization and parsing");
                for (const auto &otherSection : settingsSchema())
                    for (const auto &other : otherSection.settings)
                        if (&other != &setting)
                            check(settingValue(other, parsed) == settingValue(other, defaults),
                                  "editing one setting must preserve all other settings");
            };
            std::visit(
                Visit{[&](const ShortcutSetting &) {
                          check([control isKindOfClass:NSTextField.class] &&
                                    form.shortcutField(count) == control,
                                "shortcut definitions must render recordable text fields");
                          size_t buttons = 0;
                          for (NSView *child in form.view().subviews)
                              if ([child isKindOfClass:NSButton.class]) {
                                  auto button = (NSButton *)child;
                                  if (button.tag == control.tag &&
                                      (button.action == @selector(record:) ||
                                       button.action == @selector(clear:)))
                                      ++buttons;
                              }
                          check(buttons == 2, "every shortcut must have Record and Clear actions");
                          control.stringValue = @"";
                          roundTrip(std::string{});
                      },
                      [&](const ToggleSetting &editor) {
                          check([control isKindOfClass:NSButton.class],
                                "boolean definitions must render checkboxes");
                          ((NSButton *)control).state = editor.read(defaults)
                                                            ? NSControlStateValueOff
                                                            : NSControlStateValueOn;
                          roundTrip(!editor.read(defaults));
                      },
                      [&](const ChoiceSetting &editor) {
                          check([control isKindOfClass:NSPopUpButton.class],
                                "choice definitions must render dropdowns");
                          auto menu = (NSPopUpButton *)control;
                          check(menu.numberOfItems == static_cast<NSInteger>(editor.choices.size()),
                                "every valid file choice must be available in the dropdown");
                          for (size_t i = 0; i < editor.choices.size(); ++i) {
                              const auto &choice = editor.choices[i];
                              NSMenuItem *item = [menu itemAtIndex:i];
                              check([item.title
                                        isEqualToString:[NSString
                                                            stringWithUTF8String:choice.label
                                                                                     .c_str()]] &&
                                        [item.representedObject
                                            isEqual:[NSString
                                                        stringWithUTF8String:choice.value.c_str()]],
                                    "dropdown labels and stored values must use the same declared "
                                    "choices");
                              [menu selectItemAtIndex:i];
                              roundTrip(choice.value);
                          }
                      }},
                setting.editor);
            form.load(defaults, true);
            ++count;
        }
    }
    size_t controls = 0;
    for (NSView *child in form.view().subviews)
        if (child.identifier.length)
            ++controls;
    check(controls == count, "file settings and window controls must have exactly the same keys");
    form.load(defaults, false);
    for (const auto &key : keys)
        check(!controlFor(form.view(), key).enabled,
              "invalid config must disable every setting editor");
    for (NSView *child in form.view().subviews)
        if ([child isKindOfClass:NSButton.class])
            check(!((NSButton *)child).enabled, "invalid config must disable Record and Clear too");
    form.load(defaults, true);
    const auto read = form.read();
    check(read.shortcuts == defaults.shortcuts && read.switcher == defaults.switcher,
          "loading and saving all controls preserves the full configuration");
    if (renderPath) {
        auto window = [[NSWindow alloc] initWithContentRect:form.view().frame
                                                  styleMask:NSWindowStyleMaskBorderless
                                                    backing:NSBackingStoreBuffered
                                                      defer:NO];
        window.releasedWhenClosed = NO;
        window.contentView = form.view();
        window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameAqua];
        [window orderFrontRegardless];
        [NSRunLoop.mainRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
        [window display];
        auto view = form.view();
        auto bitmap = [view bitmapImageRepForCachingDisplayInRect:view.bounds];
        [view cacheDisplayInRect:view.bounds toBitmapImageRep:bitmap];
        auto png = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
        check([png writeToFile:[NSString stringWithUTF8String:renderPath] atomically:YES],
              "render settings form");
        [window close];
    }
    std::cout << count << " settings verified across schema, controls, and file round-trips\n";
}
} // namespace
int main(int argc, char **argv) {
    @autoreleasepool {
        uint32_t displays = 0;
        if (CGGetActiveDisplayList(0, nullptr, &displays) != kCGErrorSuccess || displays == 0)
            return 77;
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        if (NSScreen.screens.count == 0)
            return 77;
        try {
            verify(argc > 1 ? argv[1] : nullptr);
        } catch (const std::exception &error) {
            std::cerr << "FAIL: " << error.what() << '\n';
            return 1;
        }
    }
}
