#pragma once
#include "result.h"
#include <atomic>
#include <functional>
#include <thread>

namespace spacewing {
std::string socketPath();
Reply request(const std::vector<std::string> &args, const std::string &path = socketPath());
class Server {
public:
    using Handler = std::function<Reply(const std::vector<std::string> &)>;
    Server(Handler handler, std::string path = socketPath());
    ~Server();
    Server(const Server &) = delete;

private:
    void serve();
    Handler handler_;
    std::string path_;
    int listener_ = -1;
    int lock_ = -1;
    int wake_[2] = {-1, -1};
    std::thread thread_;
};
} // namespace spacewing
