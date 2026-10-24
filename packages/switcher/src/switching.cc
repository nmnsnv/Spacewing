#include "switching.h"
#include <algorithm>

namespace spacewing::switcher {
void Controller::remember(WindowID focused) {
    if (focused.pid <= 0 || !focused.number)
        return;
    std::erase(history_, focused);
    history_.insert(history_.begin(), focused);
    // Bound history for long-running hosts; unavailable IDs never enter a picker.
    if (history_.size() > 1024)
        history_.pop_back();
}
void Controller::accept(Intent intent, Mode mode) {
    if (intent == Intent::Cancel) {
        cancel();
    } else if (intent != Intent::None) {
        pending_.push_back({intent, mode});
        advance();
    }
}
void Controller::advance(bool reuseCatalog) {
    while (!pending_.empty() && phase_ != Phase::Discovering && phase_ != Phase::Activating) {
        const auto [intent, mode] = pending_.front();
        pending_.pop_front();
        if (phase_ == Phase::Idle) {
            if (intent != Intent::Commit) {
                mode_ = mode;
                if (reuseCatalog && current_ && !catalog_.empty())
                    open(catalog_, current_);
                else
                    phase_ = Phase::Discovering;
            }
        } else if (intent == Intent::Commit) {
            target_ = session_.commit();
            phase_ = Phase::Activating;
        } else if (mode != mode_) {
            mode_ = mode;
            open(catalog_, current_);
        } else {
            session_.step(intent == Intent::Backward);
        }
    }
}
void Controller::discovered(std::vector<Candidate> windows, std::optional<WindowID> focused,
                            int frontmost) {
    if (phase_ != Phase::Discovering)
        return;
    if (focused)
        remember(*focused);
    catalog_ = windows;
    current_ = focused;
    application_ = frontmost > 0 ? frontmost : (focused ? focused->pid : 0);
    open(std::move(windows), focused);
    advance();
}
void Controller::open(std::vector<Candidate> windows, std::optional<WindowID> focused) {
    if (mode_ == Mode::ApplicationWindows)
        std::erase_if(windows, [&](const auto &window) {
            return application_ <= 0 || window.id.pid != application_;
        });
    const auto rank = [&](WindowID id) {
        return std::find(history_.begin(), history_.end(), id) - history_.begin();
    };
    std::stable_sort(windows.begin(), windows.end(),
                     [&](const auto &a, const auto &b) { return rank(a.id) < rank(b.id); });
    session_.begin(std::move(windows), focused);
    if (!session_.active()) {
        cancel();
        return;
    }
    phase_ = Phase::Picking;
}
bool Controller::activated(WindowID focused) {
    if (phase_ != Phase::Activating || target_ != focused)
        return false;
    remember(focused);
    current_ = focused;
    application_ = focused.pid;
    target_.reset();
    phase_ = Phase::Idle;
    // Already queued gestures use the last eligible catalog. Native activation
    // still validates each exact target, without repeating expensive discovery.
    advance(true);
    return true;
}
void Controller::cancel() {
    pending_.clear();
    session_.cancel();
    target_.reset();
    catalog_.clear();
    current_.reset();
    application_ = 0;
    mode_ = Mode::AllWindows;
    phase_ = Phase::Idle;
}
} // namespace spacewing::switcher
