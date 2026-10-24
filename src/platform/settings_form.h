#pragma once

#include "config/schema.h"
#import <AppKit/AppKit.h>
#include <memory>

namespace spacewing {
// Every registered setting gets exactly one control. Section/row counts and choice
// menus come from the schema; no individual setting is named in this renderer.
class SettingsForm {
public:
    SettingsForm(id target, SEL record, SEL clear);
    ~SettingsForm();
    NSView *view() const;
    void load(const Config &config, bool enabled);
    Config read() const;
    const SettingDefinition &definition(size_t index) const;
    NSTextField *shortcutField(size_t index) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace spacewing
