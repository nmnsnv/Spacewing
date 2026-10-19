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
            else if (argument == "--help") {
                puts("Usage: spacewing-switcher [--all-desktops]\nOption+Tab cycles windows; Shift "
                     "reverses, Escape cancels. Ctrl+C quits.");
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
