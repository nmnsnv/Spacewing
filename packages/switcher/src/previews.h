#pragma once

#include "cf_ref.h"
#include "session.h"
#include <CoreGraphics/CoreGraphics.h>
#include <functional>
#include <memory>

namespace spacewing::switcher {
enum class PreviewStatus { Loading, Live, Unavailable, PermissionRequired, Unsupported };

// Main-thread owner. Frames are transient and only the open picker is captured.
class Previews {
public:
    using Handler = std::function<void(WindowID, CFRef<CGImageRef>, PreviewStatus)>;
    Previews();
    ~Previews();
    Previews(const Previews &) = delete;
    Previews &operator=(const Previews &) = delete;
    void show(std::vector<WindowID> windows, Handler handler);
    void stop();
    static PreviewStatus availability();

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
} // namespace spacewing::switcher
