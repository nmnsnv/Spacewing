#pragma once
#include "native.h"
#include "types.h"
#include <functional>

namespace spacewing::workspace {
struct FocusQueries {
    std::function<std::optional<Window>()> window = focusedWindow;
    std::function<std::string()> cursor = cursorDisplay;
};
std::optional<Window> resolveFocus(bool requireWindow, std::string &lastDisplay,
                                   const FocusQueries &queries = {});
struct MovePlan {
    uint32_t window;
    uint64_t source, target;
};
void validateMove(const MovePlan &plan, const std::vector<uint64_t> &membership, bool fullscreen);
struct MoveActions {
    std::function<void()> submit;
    std::function<std::vector<uint64_t>()> membership;
    std::function<uint64_t()> current;
    std::function<void()> follow;
    std::function<void()> focus;
    std::function<double()> now;
    std::function<void()> wait;
};
void carryWindow(const MovePlan &plan, const MoveActions &actions);
struct Outcome {
    Reply reply;
    std::optional<Snapshot> landed;
    double postMilliseconds = 0;
    double completeMilliseconds = 0;
};
class Controller {
public:
    Outcome execute(const Request &request);
    std::string list();

private:
    std::string lastDisplay_;
};
} // namespace spacewing::workspace
