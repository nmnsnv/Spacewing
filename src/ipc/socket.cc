#include "socket.h"
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <poll.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace spacewing {
namespace {
constexpr uint32_t magic = 0x4c4b5331;
constexpr uint32_t maxBytes = 1024 * 1024 + 4096;
struct FD {
    int value;
    ~FD() {
        if (value >= 0)
            close(value);
    }
};
void transfer(int fd, void *buffer, size_t bytes, bool writing,
              std::chrono::steady_clock::time_point deadline) {
    auto *data = static_cast<char *>(buffer);
    while (bytes) {
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                             deadline - std::chrono::steady_clock::now())
                             .count();
        pollfd p{fd, static_cast<short>(writing ? POLLOUT : POLLIN), 0};
        if (remaining <= 0)
            throw Error("IPC timed out; an already accepted action may still finish", 75);
        int ready = poll(&p, 1, static_cast<int>(remaining));
        if (ready < 0 && errno == EINTR)
            continue;
        if (ready <= 0)
            throw Error("IPC timed out or disconnected", 75);
        ssize_t count = writing ? send(fd, data, bytes, 0) : recv(fd, data, bytes, 0);
        if (count < 0 && (errno == EINTR || errno == EAGAIN))
            continue;
        if (count <= 0)
            throw Error("Resident connection closed", 69);
        data += count;
        bytes -= static_cast<size_t>(count);
    }
}
void sendMessage(int fd, const std::vector<std::string> &parts) {
    std::vector<uint8_t> bytes;
    auto append = [&](uint32_t value) {
        value = htonl(value);
        const auto *p = reinterpret_cast<const uint8_t *>(&value);
        bytes.insert(bytes.end(), p, p + 4);
    };
    append(magic);
    append(static_cast<uint32_t>(parts.size()));
    for (const auto &part : parts) {
        append(static_cast<uint32_t>(part.size()));
        bytes.insert(bytes.end(), part.begin(), part.end());
    }
    if (parts.size() > 64 || bytes.size() > maxBytes)
        throw Error("IPC request too large", 64);
    transfer(fd, bytes.data(), bytes.size(), true,
             std::chrono::steady_clock::now() + std::chrono::seconds(15));
}
std::vector<std::string> receiveMessage(int fd, int seconds = 15) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    auto readWord = [&]() {
        uint32_t value;
        transfer(fd, &value, sizeof(value), false, deadline);
        return ntohl(value);
    };
    if (readWord() != magic)
        throw Error("Incompatible resident protocol; restart Spacewing", 69);
    const auto count = readWord();
    if (count > 64)
        throw Error("Invalid IPC field count", 64);
    uint32_t total = 8;
    std::vector<std::string> result;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t length = readWord();
        if (length > maxBytes - 4 || total > maxBytes - 4 - length)
            throw Error("IPC request too large", 64);
        total += length + 4;
        std::string value(length, '\0');
        transfer(fd, value.data(), value.size(), false, deadline);
        result.push_back(std::move(value));
    }
    return result;
}
sockaddr_un address(const std::string &path) {
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path))
        throw Error("IPC socket path too long");
    memcpy(addr.sun_path, path.c_str(), path.size() + 1);
    return addr;
}
void noSigPipe(int fd) {
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
    fcntl(fd, F_SETFD, FD_CLOEXEC);
}
} // namespace

std::string socketPath() {
    char buffer[PATH_MAX];
    if (!confstr(_CS_DARWIN_USER_TEMP_DIR, buffer, sizeof(buffer)))
        throw Error("Cannot find per-user temporary directory");
    auto directory = std::filesystem::path(buffer) / "spacewing";
    if (mkdir(directory.c_str(), 0700) != 0 && errno != EEXIST)
        throw Error("Cannot create IPC directory");
    struct stat info{};
    if (lstat(directory.c_str(), &info) || !S_ISDIR(info.st_mode) || info.st_uid != getuid() ||
        (info.st_mode & 077))
        throw Error("IPC directory must be owned by you with mode 0700");
    return (directory / "control.sock").string();
}

Reply request(const std::vector<std::string> &args, const std::string &path) {
    FD fd{socket(AF_UNIX, SOCK_STREAM, 0)};
    if (fd.value < 0)
        throw Error("Cannot create IPC socket", 69);
    noSigPipe(fd.value);
    auto addr = address(path);
    if (connect(fd.value, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0)
        throw Error("Spacewing is not running. Open Spacewing.app or run spacewing first.", 69);
    uid_t uid;
    gid_t gid;
    if (getpeereid(fd.value, &uid, &gid) != 0 || uid != getuid())
        throw Error("Invalid resident owner", 69);
    sendMessage(fd.value, args);
    auto parts = receiveMessage(fd.value);
    if (parts.size() < 3)
        throw Error("Invalid resident reply", 69);
    Reply reply;
    try {
        reply.code = std::stoi(parts[0]);
    } catch (...) {
        throw Error("Invalid resident status", 69);
    }
    reply.output = std::move(parts[1]);
    reply.error = std::move(parts[2]);
    reply.values.assign(parts.begin() + 3, parts.end());
    return reply;
}

Server::Server(Handler handler, std::string path)
    : handler_(std::move(handler)), path_(std::move(path)) {
    lock_ = open((path_ + ".lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (lock_ < 0 || flock(lock_, LOCK_EX | LOCK_NB) != 0) {
        if (lock_ >= 0)
            close(lock_);
        throw Error("Spacewing is already running", 69);
    }
    try {
        listener_ = socket(AF_UNIX, SOCK_STREAM, 0);
        if (listener_ < 0)
            throw Error("Cannot create resident socket");
        noSigPipe(listener_);
        unlink(path_.c_str()); // Singleton lock held: stale socket only.
        auto addr = address(path_);
        if (bind(listener_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0 ||
            chmod(path_.c_str(), 0600) != 0 || listen(listener_, 32) != 0 || pipe(wake_) != 0)
            throw Error("Cannot initialize resident IPC: " + std::string(strerror(errno)),
                        errno == EPERM || errno == EACCES ? 77 : 1);
        for (int fd : wake_)
            fcntl(fd, F_SETFD, FD_CLOEXEC);
        thread_ = std::thread([this] { serve(); });
    } catch (...) {
        if (listener_ >= 0)
            close(listener_);
        close(lock_);
        unlink(path_.c_str());
        throw;
    }
}
Server::~Server() {
    char byte = 0;
    write(wake_[1], &byte, 1);
    if (thread_.joinable())
        thread_.join();
    close(listener_);
    close(wake_[0]);
    close(wake_[1]);
    unlink(path_.c_str());
    close(lock_);
}
void Server::serve() {
    for (;;) {
        pollfd descriptors[] = {{listener_, POLLIN, 0}, {wake_[0], POLLIN, 0}};
        if (poll(descriptors, 2, -1) < 0) {
            if (errno == EINTR)
                continue;
            return;
        }
        if (descriptors[1].revents)
            return;
        FD client{accept(listener_, nullptr, nullptr)};
        if (client.value < 0)
            continue;
        noSigPipe(client.value);
        uid_t uid;
        gid_t gid;
        if (getpeereid(client.value, &uid, &gid) || uid != getuid())
            continue;
        Reply reply;
        try {
            reply = handler_(receiveMessage(client.value, 1));
        } catch (const Error &e) {
            reply = {e.code, "", e.what()};
        } catch (const std::exception &e) {
            reply = {1, "", e.what()};
        }
        std::vector<std::string> parts{std::to_string(reply.code), reply.output, reply.error};
        parts.insert(parts.end(), reply.values.begin(), reply.values.end());
        try {
            sendMessage(client.value, parts);
        } catch (...) {
        }
    }
}
} // namespace spacewing
