#include "macos.h"
#include "service.h"
#import <AppKit/AppKit.h>
#include <algorithm>
#include <cstdio>
#include <string>

// Minimal independent host. The embedded Spacewing host owns TOML and Settings.
int main(int argc, char **argv) {
    @autoreleasepool {
        spacewing::switcher::Options options;
        for (int i = 1; i < argc; ++i) {
            const std::string argument = argv[i];
            if (argument == "--all-desktops")
                options.scope = spacewing::switcher::Scope::AllDesktops;
            else if (argument == "--help") {
                puts("Usage: spacewing-switcher [--all-desktops] [SHORTCUT_OPTION CHORD]\n"
                     "Shortcut options (empty CHORD disables the binding):");
                for (const auto &action : spacewing::switcher::shortcutActions)
                    printf("  %s CHORD  %s (default %s)\n", action.cliOption, action.label,
                           action.defaultChord);
                puts("Opening selects the second eligible window.\n"
                     "Up/Down navigate; Shift reverses cycling; Escape cancels.\n"
                     "Release a shortcut modifier to activate. Ctrl+C quits.");
                return 0;
            } else {
                const auto &actions = spacewing::switcher::shortcutActions;
                const auto action =
                    std::find_if(actions.begin(), actions.end(),
                                 [&](const auto &item) { return argument == item.cliOption; });
                if (action == actions.end() || i + 1 == argc) {
                    fprintf(stderr, "Unknown option or missing chord: %s\n", argv[i]);
                    return 64;
                }
                try {
                    options.shortcuts[action - actions.begin()] =
                        spacewing::switcher::parseShortcut(argv[++i]);
                } catch (const std::invalid_argument &error) {
                    fprintf(stderr, "%s\n", error.what());
                    return 64;
                }
            }
        }
        spacewing::initializeApplication();
        spacewing::switcher::Service switcher(
            {[] { return false; },
             [](const auto &error) { fprintf(stderr, "switcher: %s\n", error.c_str()); }});
        try {
            switcher.configure(options);
            [NSApp run];
        } catch (const std::exception &error) {
            fprintf(stderr, "switcher: %s\n", error.what());
            return 1;
        }
    }
}
