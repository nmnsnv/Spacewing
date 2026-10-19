#pragma once
#include "cf_ref.h"
#include <CoreGraphics/CoreGraphics.h>
#include <cstdint>
#include <vector>

namespace spacewing::workspace {
inline constexpr int64_t eventTag = 0x4e535753;
int32_t fixed1616(double value);
std::vector<uint8_t> gesturePayload(CGEventRef event);
CFRef<CGEventRef> dockEvent(int phase, int direction, bool augmented, bool natural);
std::vector<CFRef<CGEventRef>> gestureEvents(int direction, bool augmented, bool natural);
bool naturalScrolling();
void postGesture(const std::vector<CFRef<CGEventRef>> &events);
} // namespace spacewing::workspace
