#include "config/config.h"
#include "ipc/socket.h"
#include "native.h"
#include "platform/application.h"
#include "runtime.h"
#include <iostream>

using namespace spacewing;
int main(int argc, char **argv) {
    try {
        std::vector<std::string> arguments(argv + 1, argv + argc);
        auto command = parseArguments(arguments);
        if (command.name == "help") {
            std::cout << usage();
            return 0;
        }
        if (command.name == "version") {
            std::cout << "spacewing 0.1.0\n";
            return 0;
        }
        if (command.name == "check-config") {
            auto path = command.explicitConfig ? command.config : resolvedPath(defaultConfigPath());
            parseConfig(readFile(path));
            std::cout << "Valid config: " << path << "\n";
            return 0;
        }
        if (command.name == "run") {
            // Process-lifetime owner. NSApplication terminates the process; no main-thread
            // destructor joins a worker that may be waiting for an AppKit callback.
            runApplication([command] { (new Runtime(command))->start(); });
            return 0;
        }
        Reply reply;
        try {
            reply = request(arguments);
        } catch (const Error &e) {
            if (command.name != "doctor" || e.code != 69)
                throw;
            std::cout << "Resident: not running\nApp Accessibility: unknown (a CLI grant is not an "
                         "app grant)\n"
                      << "macOS: " << workspace::osVersion() << "\nNative window move: "
                      << (workspace::canMoveWindows() ? "available" : "unavailable") << "\n";
            return 69;
        }
        std::cout << reply.output;
        if (!reply.error.empty())
            std::cerr << "spacewing: " << reply.error << "\n";
        return reply.code;
    } catch (const Error &e) {
        std::cerr << "spacewing: " << e.what() << "\n";
        return e.code;
    } catch (const std::exception &e) {
        std::cerr << "spacewing: " << e.what() << "\n";
        return 1;
    }
}
