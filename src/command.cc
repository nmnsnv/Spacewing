#include "command.h"

#include <cstdlib>
#include <filesystem>
#include <pwd.h>
#include <set>
#include <unistd.h>

namespace spacewing {
std::string defaultConfigPath() {
    const char *home = getenv("HOME");
    if (!home || !*home) {
        const auto *entry = getpwuid(getuid());
        if (!entry)
            throw Error("Cannot locate your home directory", 78);
        home = entry->pw_dir;
    }
    return (std::filesystem::path(home) / ".spacewing").string();
}

std::string resolvedPath(const std::string &path) {
    if (path.empty() || path.find('\0') != std::string::npos)
        throw Error("Config path is empty or contains NUL", 64);
    std::string expanded = path;
    if (path == "~" || path.starts_with("~/")) {
        expanded =
            std::filesystem::path(defaultConfigPath()).parent_path().string() + path.substr(1);
    }
    std::error_code error;
    auto result = std::filesystem::weakly_canonical(std::filesystem::absolute(expanded), error);
    if (error)
        throw Error("Cannot resolve config path: " + error.message(), 78);
    return result.string();
}

Command parseArguments(const std::vector<std::string> &args) {
    Command command;
    bool hasName = false;
    std::set<std::string> seen;
    for (size_t i = 0; i < args.size(); ++i) {
        const auto &arg = args[i];
        if (arg == "--config" || arg.starts_with("--config=")) {
            if (command.explicitConfig)
                throw Error("Specify --config only once", 64);
            command.explicitConfig = true;
            if (arg == "--config") {
                if (++i == args.size() || args[i].starts_with("--"))
                    throw Error("--config needs a path", 64);
                command.config = resolvedPath(args[i]);
            } else
                command.config = resolvedPath(arg.substr(9));
        } else if (arg == "--drag-active" || arg == "--verify" || arg == "--dry-run") {
            if (!seen.insert(arg).second)
                throw Error("Repeated option: " + arg, 64);
            if (arg == "--drag-active")
                command.drag = true;
            if (arg == "--verify")
                command.verify = true;
            if (arg == "--dry-run")
                command.dryRun = true;
        } else {
            if (hasName)
                throw Error("Unexpected argument: " + arg, 64);
            command.name = arg == "--help" ? "help" : arg == "--version" ? "version" : arg;
            hasName = true;
        }
    }
    static const std::set<std::string> names = {"run",    "left",   "right",  "settings",
                                                "reload", "list",   "doctor", "check-config",
                                                "help",   "version"};
    if (!names.contains(command.name))
        throw Error("Unknown command: " + command.name + "\n" + usage(), 64);
    if (command.name != "left" && command.name != "right" &&
        (command.drag || command.verify || command.dryRun))
        throw Error("--drag-active, --verify and --dry-run require left or right", 64);
    return command;
}

std::string usage() {
    return "Usage: spacewing [--config PATH | --config=PATH]\n"
           "       spacewing left|right [--drag-active] [--verify] [--dry-run]\n"
           "       spacewing settings|reload|list|doctor|check-config\n"
           "       spacewing --help | --version\n\n"
           "Start Spacewing.app (or run with no command) before issuing workspace commands.\n"
           "Shortcuts follow the focused window's display. Config defaults to ~/.spacewing.\n";
}
} // namespace spacewing
