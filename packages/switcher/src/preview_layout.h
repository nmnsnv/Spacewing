#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace spacewing::switcher {
struct PreviewArea {
    double width = 1100;
    double height = 700;
};
struct PreviewGrid {
    size_t columns = 0;
    size_t rows = 0;
    double tileWidth = 0;
    double tileHeight = 0;
    double gap = 0;
    double width = 0;
    double height = 0;
    static constexpr double margin = 16, header = 44, footer = 56;
};

// Prefer four compact columns; shrink and add columns as needed to fit every window.
inline PreviewGrid previewGrid(size_t count, PreviewArea area) {
    if (!count)
        return {};
    const double availableWidth =
        std::max(1.0, std::min(1100.0, area.width) - 2 * PreviewGrid::margin);
    const double availableHeight =
        std::max(1.0, area.height - PreviewGrid::header - PreviewGrid::footer);
    PreviewGrid best;
    double bestScale = -1;
    size_t bestDistance = count;
    const auto preferred = std::min<size_t>(4, count);
    for (size_t columns = 1; columns <= count; ++columns) {
        const auto rows = (count - 1) / columns + 1;
        const double scale = std::min({1.0, availableWidth / (columns * 196 + (columns - 1) * 10),
                                       availableHeight / (rows * 162 + (rows - 1) * 10)});
        const auto distance = columns > preferred ? columns - preferred : preferred - columns;
        if (scale < bestScale - 0.0001 ||
            (std::abs(scale - bestScale) < 0.0001 && distance >= bestDistance))
            continue;
        bestScale = scale;
        bestDistance = distance;
        best = {columns,
                rows,
                196 * scale,
                162 * scale,
                10 * scale,
                std::max(std::min(420.0, area.width),
                         2 * PreviewGrid::margin + (columns * 196 + (columns - 1) * 10) * scale),
                PreviewGrid::header + PreviewGrid::footer + (rows * 162 + (rows - 1) * 10) * scale};
    }
    return best;
}
} // namespace spacewing::switcher
