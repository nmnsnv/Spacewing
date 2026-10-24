#pragma once

#include "preview_layout.h"
#include "session.h"
#include <deque>

namespace spacewing::switcher {
enum class Phase { Idle, Discovering, Picking, Activating };

// Serializes switcher gestures across asynchronous discovery and focus changes.
// The native service supplies snapshots and acknowledges the exact focused window.
class Controller {
public:
    void accept(Intent intent, Mode mode = Mode::AllWindows);
    void discovered(std::vector<Candidate> windows, std::optional<WindowID> focused,
                    int frontmost = 0);
    bool activated(WindowID focused);
    void cancel();
    void setPreviewArea(PreviewArea area) { area_ = area; }
    void setLayout(Layout layout) { layout_ = layout; }
    Phase phase() const { return phase_; }
    const Session &session() const { return session_; }
    std::optional<WindowID> focused() const { return current_; }
    std::optional<WindowID> target() const { return target_; }

private:
    void advance(bool reuseCatalog = false);
    void open(std::vector<Candidate> windows, std::optional<WindowID> focused);
    void remember(WindowID focused);
    struct Action {
        Intent intent;
        Mode mode;
    };
    Session session_;
    PreviewArea area_;
    Layout layout_ = Layout::Grid;
    std::deque<Action> pending_;
    std::vector<WindowID> history_;
    std::vector<Candidate> catalog_;
    std::optional<WindowID> current_;
    std::optional<WindowID> target_;
    int application_ = 0;
    Mode mode_ = Mode::AllWindows;
    Phase phase_ = Phase::Idle;
};
} // namespace spacewing::switcher
