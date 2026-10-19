#include "hotkeys.h"
#include "result.h"
#import <Carbon/Carbon.h>
#include <map>

namespace spacewing::workspace {
namespace {
std::function<void(uint64_t, bool)> hotkeyCallback;
std::map<uint64_t, EventHotKeyRef> hotkeyRefs;
uint32_t nextHotkey = 0;
} // namespace
void installHotkeys(const std::function<void(uint64_t, bool)> &callback) {
    hotkeyCallback = callback;
    EventTypeSpec events[] = {{kEventClassKeyboard, kEventHotKeyPressed},
                              {kEventClassKeyboard, kEventHotKeyReleased}};
    const auto status = InstallEventHandler(
        GetApplicationEventTarget(),
        [](EventHandlerCallRef, EventRef event, void *) -> OSStatus {
            EventHotKeyID id{};
            if (GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, nullptr,
                                  sizeof(id), nullptr, &id) == noErr)
                hotkeyCallback(id.id, GetEventKind(event) == kEventHotKeyPressed);
            return noErr;
        },
        2, events, nullptr, nullptr);
    if (status != noErr)
        throw Error("Cannot install global hotkey handler");
}
uint64_t registerHotkey(Shortcut shortcut) {
    UInt32 flags = 0;
    if (shortcut.modifiers & Control)
        flags |= controlKey;
    if (shortcut.modifiers & Shift)
        flags |= shiftKey;
    if (shortcut.modifiers & Option)
        flags |= optionKey;
    if (shortcut.modifiers & CommandKey)
        flags |= cmdKey;
    EventHotKeyID id{0x4c4f524b, ++nextHotkey};
    EventHotKeyRef ref = nullptr;
    OSStatus status = RegisterEventHotKey(shortcut.key, flags, id, GetApplicationEventTarget(),
                                          kEventHotKeyExclusive, &ref);
    if (status != noErr)
        throw Error("Cannot register " + shortcut.text() + " (status " + std::to_string(status) +
                        "); choose a shortcut not used by another app or macOS.",
                    78);
    hotkeyRefs[id.id] = ref;
    return id.id;
}
void unregisterHotkey(uint64_t id) {
    auto it = hotkeyRefs.find(id);
    if (it == hotkeyRefs.end())
        return;
    UnregisterEventHotKey(it->second);
    hotkeyRefs.erase(it);
}
} // namespace spacewing::workspace
