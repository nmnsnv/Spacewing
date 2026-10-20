#include "macos.h"
#include "service.h"
#import <AppKit/AppKit.h>
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
            else if (argument == "--shortcut" && i + 1 < argc) {
                try {
                    options.shortcut = spacewing::switcher::parseShortcut(argv[++i]);
                } catch (const std::invalid_argument &error) {
                    fprintf(stderr, "%s\n", error.what());
                    return 64;
                }
            } else if (argument == "--help") {
                puts("Usage: spacewing-switcher [--all-desktops] [--shortcut CHORD]\n"
                     "The shortcut (default Option+Tab) selects the second window.\n"
                     "Up/Down navigate; Shift reverses cycling; Escape cancels.\n"
                     "Release a shortcut modifier to activate. Ctrl+C quits.");
                return 0;
            } else {
                fprintf(stderr, "Unknown option: %s\n", argv[i]);
                return 64;
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
