#include "session.h"
#include <iostream>
#include <stdexcept>

using namespace spacewing::switcher;
namespace {
int checks = 0;
void check(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
const WindowID a{10, 1}, b{10, 2}, c{20, 3};
const std::vector<Candidate> windows{
    {a, "Editor", "First"}, {b, "Editor", "Second"}, {c, "Browser", "Page"}};
void selection() {
    Session session;
    session.begin(windows, a);
    check(session.selected() == 1, "first Tab selects next individual window in same app");
    session.step(false);
    check(session.selected() == 2, "Tab advances");
    session.step(false);
    check(session.selected() == 0, "forward wraps");
    session.step(true);
    check(session.selected() == 2, "reverse wraps");
    check(session.commit() == c && !session.active(),
          "release returns exact window and closes session");
    check(!session.commit(), "release without session does nothing");
    session.begin(windows, c);
    check(session.windows()[0].id == c && session.windows()[1].id == a &&
              session.windows()[2].id == b,
          "active window moves first while remaining windows retain their order");
    check(session.selected() == 1 && session.commit() == a,
          "opening always selects the second item even when focus was last");
    session.begin(windows, b);
    check(session.windows()[0].id == b && session.windows()[1].id == a &&
              session.windows()[2].id == c,
          "moving a middle window first preserves remaining order");
    session.step(true);
    check(session.selected() == 0 && session.commit() == b, "Up reaches the active window");
    session.begin(windows, std::nullopt);
    check(session.commit() == b, "unknown focus still starts at the second item");
    session.begin(windows, WindowID{99, 99});
    check(session.commit() == b, "focus outside scope still starts at the second item");
    session.begin({windows.front()}, a);
    session.step(false);
    check(session.commit() == a, "single window remains selected");
    session.begin({}, a);
    session.step(true);
    check(!session.active() && !session.commit(), "empty catalog is safe");
    session.begin(windows, a);
    session.cancel();
    check(!session.commit() && session.windows().empty(), "cancel never activates");
    auto duplicates = windows;
    duplicates.push_back(windows.front());
    duplicates.push_back({{0, 5}, "Invalid", ""});
    duplicates.push_back({{10, 0}, "Invalid", ""});
    duplicates.push_back({{30, 1}, "Other process", "Reused number"});
    session.begin(duplicates, a);
    check(session.windows().size() == 4, "deduplicates identities and drops invalid IDs");
    check(session.windows()[3].id == WindowID{30, 1}, "identity includes process");
}
Input down(Key key, bool option = true, bool shift = false, bool repeat = false) {
    return {InputType::Down, key, (option ? Option : 0u) | (shift ? Shift : 0u), repeat};
}
void keyboard() {
    Keyboard keyboard;
    auto result = keyboard.handle(down(Key::Tab, false), false);
    check(!result.consume && result.intent == Intent::None, "ordinary Tab passes through");
    result = keyboard.handle(down(Key::Tab), true);
    check(!result.consume && !keyboard.active(), "busy host preserves Option+Tab");
    result = keyboard.handle(down(Key::Tab), false);
    check(result.consume && result.intent == Intent::Forward && keyboard.active(),
          "Option+Tab opens");
    result = keyboard.handle(down(Key::Tab, true, false, true), false);
    check(result.consume && result.intent == Intent::None, "held Tab does not repeat");
    result = keyboard.handle({InputType::Up, Key::Tab}, false);
    check(result.consume && keyboard.active(), "Tab release keeps picker open");
    result = keyboard.handle(down(Key::Tab, true, true), false);
    check(result.consume && result.intent == Intent::Backward, "Shift reverses");
    result = keyboard.handle({InputType::Modifiers, Key::Other, Option}, false);
    check(result.intent == Intent::None && keyboard.active(), "Shift release does not commit");
    result = keyboard.handle({InputType::Modifiers}, false);
    check(!result.consume && result.intent == Intent::Commit && !keyboard.active(),
          "Option release commits");
    check(keyboard.handle(down(Key::Tab, false, false, true), true).consume,
          "held Tab cannot leak repeats after Option release while activation is pending");
    check(keyboard.handle({InputType::Up, Key::Tab}, false).consume,
          "Option before Tab release keeps key-up paired");
    check(!keyboard.handle({InputType::Up, Key::Tab}, false).consume, "unrelated key-up passes");
    check(keyboard.handle({InputType::Modifiers}, false).intent == Intent::None,
          "no double commit");
    keyboard.handle(down(Key::Tab), false);
    keyboard.handle({InputType::Up, Key::Tab}, false);
    result = keyboard.handle(down(Key::Escape), false);
    check(result.consume && result.intent == Intent::Cancel, "Escape cancels");
    check(keyboard.handle(down(Key::Escape, true, false, true), false).consume,
          "Escape repeat swallowed");
    check(keyboard.handle({InputType::Up, Key::Escape}, false).consume, "Escape key-up paired");
    check(keyboard.handle({InputType::Modifiers}, false).intent == Intent::None,
          "release after Escape does nothing");
    keyboard.handle(down(Key::Tab), false);
    keyboard.cancel();
    check(keyboard.handle(down(Key::Tab, true, false, true), true).consume,
          "held Tab stays swallowed after external cancellation");
    check(keyboard.handle({InputType::Up, Key::Tab}, false).consume,
          "external cancellation retains key pairing");
    check(keyboard.handle({InputType::Modifiers}, false).intent == Intent::None,
          "desktop change prevents activation");
    keyboard.handle(down(Key::Tab), false);
    result = keyboard.handle({InputType::Modifiers, Key::Other, Option | CommandKey}, false);
    check(result.intent == Intent::Cancel, "Command or Control cancels");
    keyboard.reset();
    auto modified = down(Key::Tab);
    modified.modifiers |= Control;
    check(!keyboard.handle(modified, false).consume, "other Option+Tab chords pass");
    check(!keyboard.handle(down(Key::Tab, true, false, true), false).consume,
          "repeat cannot open picker");
    keyboard.handle(down(Key::Tab), false);
    result = keyboard.handle(down(Key::Other), false);
    check(!result.consume && result.intent == Intent::Cancel,
          "typing cancels without losing typed key");
    keyboard.reset();
    keyboard.handle(down(Key::Tab), false);
    result = keyboard.handle({InputType::Modifiers}, true);
    check(result.intent == Intent::Cancel, "busy host prevents release activation");
    keyboard.reset();
    check(!keyboard.active() && !keyboard.handle({InputType::Up, Key::Tab}, false).consume,
          "sleep or tap reset clears held keys");
}
void arrows() {
    Keyboard keyboard;
    check(!keyboard.handle(down(Key::Up), false).consume &&
              !keyboard.handle(down(Key::Down), false).consume,
          "arrow keys pass through outside the picker");
    keyboard.handle(down(Key::Tab), false);
    keyboard.handle({InputType::Up, Key::Tab}, false);
    auto result = keyboard.handle(down(Key::Up), false);
    check(result.consume && result.intent == Intent::Backward, "Up moves selection up");
    result = keyboard.handle(down(Key::Up, true, false, true), false);
    check(result.consume && result.intent == Intent::Backward, "held Up continues navigating");
    check(keyboard.handle({InputType::Up, Key::Up}, false).consume, "Up release is paired");
    result = keyboard.handle(down(Key::Down), false);
    check(result.consume && result.intent == Intent::Forward, "Down moves selection down");
    result = keyboard.handle(down(Key::Escape), false);
    check(result.consume && result.intent == Intent::Cancel && !keyboard.active(),
          "Escape closes picker after arrow navigation");
    result = keyboard.handle(down(Key::Down, true, false, true), false);
    check(result.consume && result.intent == Intent::None,
          "arrow repeats stay swallowed after cancellation");
    check(keyboard.handle({InputType::Up, Key::Down}, false).consume,
          "arrow key-up stays paired after cancellation");
    check(keyboard.handle({InputType::Modifiers}, false).intent == Intent::None,
          "modifier release after Escape cannot activate");
    keyboard.reset();
    keyboard.handle(down(Key::Tab), false);
    keyboard.handle(down(Key::Down), false);
    check(keyboard.handle({InputType::Modifiers}, false).intent == Intent::Commit,
          "modifier release commits after arrow navigation");
    check(keyboard.handle(down(Key::Down, false, false, true), true).consume,
          "held arrow does not leak after commit");
    check(keyboard.handle({InputType::Up, Key::Down}, false).consume,
          "arrow release is consumed after commit");
    keyboard.reset();
    keyboard.handle(down(Key::Tab), false);
    result = keyboard.handle(down(Key::Down), true);
    check(!result.consume && result.intent == Intent::Cancel,
          "host blocking cancels arrow navigation");
    keyboard.reset();
    keyboard.handle(down(Key::Tab), false);
    result = keyboard.handle(down(Key::Up, true, false, true), false);
    check(!result.consume && result.intent == Intent::Cancel,
          "repeat from an arrow already held before opening retains its original owner");
    check(!keyboard.handle({InputType::Up, Key::Up}, false).consume,
          "unowned arrow release passes through");
}
void shortcuts() {
    check(parseShortcut(" ALT + tab ") == Shortcut{}, "default shortcut and alias");
    check(parseShortcut("Command + Control + Space").text() == "ctrl+cmd+space",
          "custom shortcut normalizes modifiers");
    check(parseShortcut("option+a").key == static_cast<Key>(0),
          "zero key code is a valid shortcut");
    for (auto text : {"", "tab", "ctrl", "ctrl++tab", "ctrl+option+alt+tab", "ctrl+up",
                      "option+down", "cmd+escape", "ctrl+tab+space", "ctrl+unknown"}) {
        bool rejected = false;
        try {
            parseShortcut(text);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(rejected, "invalid or reserved switcher shortcut rejected");
    }
    Keyboard keyboard;
    const auto shortcut = parseShortcut("ctrl+space");
    keyboard.configure(shortcut);
    check(!keyboard.handle(down(Key::Tab), false).consume,
          "old shortcut passes through after reconfiguration");
    auto result = keyboard.handle({InputType::Down, shortcut.key, Control}, false);
    check(result.consume && result.intent == Intent::Forward && keyboard.active(),
          "custom shortcut opens picker");
    keyboard.handle({InputType::Up, shortcut.key}, false);
    result = keyboard.handle({InputType::Down, shortcut.key, Control | Shift}, false);
    check(result.consume && result.intent == Intent::Backward,
          "extra Shift reverses the configured shortcut");
    check(keyboard.handle({InputType::Modifiers, Key::Other, Control}, false).intent ==
              Intent::None,
          "releasing extra Shift keeps the picker open");
    check(keyboard.handle({InputType::Down, Key::Down, Control}, false).intent == Intent::Forward,
          "arrow navigation uses the configured modifier");
    check(keyboard.handle({InputType::Modifiers}, false).intent == Intent::Commit,
          "releasing configured modifier commits");
    keyboard.configure(Shortcut{});
    check(keyboard.handle({InputType::Up, shortcut.key}, false).consume,
          "reconfiguration retains ownership of an old shortcut key-up");
    const auto multiple = parseShortcut("ctrl+option+a");
    keyboard.configure(multiple);
    result = keyboard.handle({InputType::Down, multiple.key, Control | Option}, false);
    check(result.consume && result.intent == Intent::Forward, "multiple modifiers and A open");
    check(keyboard.handle({InputType::Modifiers, Key::Other, Control}, false).intent ==
              Intent::Commit,
          "releasing any required modifier commits");
    keyboard.reset();
    keyboard.configure(parseShortcut("shift+tab"));
    result = keyboard.handle({InputType::Down, Key::Tab, Shift}, false);
    check(result.consume && result.intent == Intent::Forward,
          "Shift in the configured chord is not interpreted as reverse");
}
} // namespace
int main() {
    try {
        selection();
        keyboard();
        arrows();
        shortcuts();
        std::cout << checks << " switcher checks passed\n";
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
