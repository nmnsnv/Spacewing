#include "settings_form.h"
#include <algorithm>

@interface SpacewingSettingsContent : NSView
@end
@implementation SpacewingSettingsContent
- (BOOL)isFlipped {
    return YES;
}
- (BOOL)isOpaque {
    return YES;
}
- (void)drawRect:(NSRect)rect {
    [NSColor.windowBackgroundColor setFill];
    NSRectFill(rect);
}
@end

namespace spacewing {
namespace {
NSString *ns(const std::string &text) {
    NSString *result = [NSString stringWithUTF8String:text.c_str()];
    return result ? result : @"";
}
NSTextField *label(const std::string &text, NSRect frame) {
    auto field = [NSTextField labelWithString:ns(text)];
    field.frame = frame;
    field.lineBreakMode = NSLineBreakByTruncatingTail;
    return field;
}
} // namespace
struct SettingsForm::Impl {
    NSView *view = [[SpacewingSettingsContent alloc] initWithFrame:NSMakeRect(0, 0, 700, 0)];
    NSMutableArray<NSControl *> *controls = [NSMutableArray new];
    NSMutableArray<NSButton *> *buttons = [NSMutableArray new];
    std::vector<const SettingDefinition *> definitions;
    Config original;

    Impl(id target, SEL record, SEL clear) {
        CGFloat y = 20;
        for (const auto &section : settingsSchema()) {
            auto heading = label(section.title, NSMakeRect(24, y, 650, 28));
            heading.font = [NSFont boldSystemFontOfSize:20];
            [view addSubview:heading];
            auto detail = label(section.description, NSMakeRect(24, y + 34, 650, 20));
            detail.font = [NSFont systemFontOfSize:12];
            detail.textColor = NSColor.secondaryLabelColor;
            [view addSubview:detail];
            y += 68;
            for (const auto &setting : section.settings) {
                const auto index = definitions.size();
                definitions.push_back(&setting);
                const auto identifier = ns(section.key + "." + setting.key);
                const auto add = [&](NSControl *control) {
                    control.identifier = identifier;
                    control.tag = index;
                    [control setAccessibilityLabel:ns(setting.label)];
                    [controls addObject:control];
                    [view addSubview:control];
                };
                const auto addLabel = [&] {
                    [view addSubview:label(setting.label, NSMakeRect(24, y + 5, 170, 22))];
                };
                std::visit(Visit{[&](const ShortcutSetting &editor) {
                                     addLabel();
                                     auto field = [[NSTextField alloc]
                                         initWithFrame:NSMakeRect(200, y, 294, 28)];
                                     field.placeholderString = @"Disabled";
                                     field.toolTip = ns("Default: " + editor.defaultValue);
                                     add(field);
                                     const auto action = [&](NSString *title, CGFloat x,
                                                             CGFloat width, SEL selector) {
                                         auto button = [NSButton buttonWithTitle:title
                                                                          target:target
                                                                          action:selector];
                                         button.frame = NSMakeRect(x, y - 2, width, 32);
                                         button.bezelStyle = NSBezelStyleRounded;
                                         button.tag = index;
                                         [buttons addObject:button];
                                         [view addSubview:button];
                                     };
                                     action(@"Record", 504, 88, record);
                                     action(@"Clear", 598, 76, clear);
                                 },
                                 [&](const ToggleSetting &) {
                                     auto checkbox = [NSButton checkboxWithTitle:ns(setting.label)
                                                                          target:nil
                                                                          action:nil];
                                     checkbox.frame = NSMakeRect(24, y, 650, 28);
                                     add(checkbox);
                                 },
                                 [&](const ChoiceSetting &editor) {
                                     addLabel();
                                     auto menu = [[NSPopUpButton alloc]
                                         initWithFrame:NSMakeRect(200, y, 294, 28)
                                             pullsDown:NO];
                                     for (const auto &choice : editor.choices) {
                                         [menu addItemWithTitle:ns(choice.label)];
                                         menu.lastItem.representedObject = ns(choice.value);
                                     }
                                     add(menu);
                                 }},
                           setting.editor);
                y += 42;
            }
            y += 18;
        }
        view.frame = NSMakeRect(0, 0, 700, y);
    }
    void load(const Config &config, bool enabled) {
        original = config;
        for (size_t i = 0; i < definitions.size(); ++i) {
            NSControl *control = controls[i];
            control.enabled = enabled;
            std::visit(
                Visit{[&](const ShortcutSetting &editor) {
                          control.stringValue = ns(editor.read(config));
                      },
                      [&](const ToggleSetting &editor) {
                          ((NSButton *)control).state =
                              editor.read(config) ? NSControlStateValueOn : NSControlStateValueOff;
                      },
                      [&](const ChoiceSetting &editor) {
                          const auto value = editor.read(config);
                          const auto it =
                              std::find_if(editor.choices.begin(), editor.choices.end(),
                                           [&](const auto &item) { return item.value == value; });
                          [(NSPopUpButton *)control selectItemAtIndex:it - editor.choices.begin()];
                      }},
                definitions[i]->editor);
        }
        for (NSButton *button in buttons)
            button.enabled = enabled;
    }
    Config read() const {
        Config config = original;
        for (size_t i = 0; i < definitions.size(); ++i) {
            NSControl *control = controls[i];
            auto value =
                std::visit(Visit{[&](const ShortcutSetting &) -> SettingValue {
                                     return std::string(control.stringValue.UTF8String);
                                 },
                                 [&](const ToggleSetting &) -> SettingValue {
                                     return ((NSButton *)control).state == NSControlStateValueOn;
                                 },
                                 [&](const ChoiceSetting &) -> SettingValue {
                                     NSString *selected =
                                         ((NSPopUpButton *)control).selectedItem.representedObject;
                                     return std::string(selected ? selected.UTF8String : "");
                                 }},
                           definitions[i]->editor);
            setSettingValue(*definitions[i], config, value);
        }
        return config;
    }
};

SettingsForm::SettingsForm(id target, SEL record, SEL clear)
    : impl_(std::make_unique<Impl>(target, record, clear)) {}
SettingsForm::~SettingsForm() = default;
NSView *SettingsForm::view() const { return impl_->view; }
void SettingsForm::load(const Config &config, bool enabled) { impl_->load(config, enabled); }
Config SettingsForm::read() const { return impl_->read(); }
const SettingDefinition &SettingsForm::definition(size_t index) const {
    return *impl_->definitions.at(index);
}
NSTextField *SettingsForm::shortcutField(size_t index) const {
    if (!std::holds_alternative<ShortcutSetting>(definition(index).editor))
        throw std::logic_error("Only shortcut settings can be recorded");
    return (NSTextField *)impl_->controls[index];
}
} // namespace spacewing
