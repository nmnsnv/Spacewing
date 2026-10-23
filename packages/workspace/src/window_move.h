#pragma once
#include <cstdint>
bool BFCanMoveWindows(bool fullscreenTarget = false);
bool BFSubmitWindowMove(uint32_t window, uint64_t space, bool fullscreenTarget = false);
