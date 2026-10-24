#pragma once

#include "preview_layout.h"
#include "session.h"
#include <memory>

namespace spacewing::switcher {
// Nonactivating, keyboard-driven preview panel. All methods run on the main thread.
class Overlay {
public:
    Overlay();
    ~Overlay();
    Overlay(const Overlay &) = delete;
    Overlay &operator=(const Overlay &) = delete;
    void show(const Session &session, Layout layout = Layout::Grid);
    void hide();
    static PreviewArea availableArea();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace spacewing::switcher
