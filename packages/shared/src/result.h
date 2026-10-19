#pragma once
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace spacewing {
// Failure with an exit-status code that the host can preserve in CLI/IPC replies.
struct Error : std::runtime_error {
    int code;
    explicit Error(std::string message, int status = 1)
        : std::runtime_error(std::move(message)), code(status) {}
};

// Transport-independent operation result: status, displayable output/error, and
// optional structured string fields for callers such as the Settings helper.
struct Reply {
    int code = 0;
    std::string output;
    std::string error;
    std::vector<std::string> values;
    Reply(int status = 0, std::string out = {}, std::string err = {},
          std::vector<std::string> data = {})
        : code(status), output(std::move(out)), error(std::move(err)), values(std::move(data)) {}
};

} // namespace spacewing
