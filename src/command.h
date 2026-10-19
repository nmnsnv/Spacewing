#pragma once
#include "result.h"
#include "types.h"

namespace spacewing {
struct Command {
    std::string name = "run";
    std::string config;
    bool explicitConfig = false;
    bool drag = false;
    bool verify = false;
    bool dryRun = false;
    workspace::Action action() const {
        return name == "left" ? (drag ? workspace::Action::DragLeft : workspace::Action::Left)
                              : (drag ? workspace::Action::DragRight : workspace::Action::Right);
    }
};
Command parseArguments(const std::vector<std::string> &args);
std::string usage();
std::string defaultConfigPath();
std::string resolvedPath(const std::string &path);
} // namespace spacewing
