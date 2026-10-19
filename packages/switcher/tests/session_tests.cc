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
    session.begin(windows, a, false);
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
    session.begin(windows, a, true);
    check(session.commit() == c, "initial reverse selects previous window");
    session.begin(windows, c, false);
    check(session.commit() == a, "focused window need not be first");
    session.begin(windows, std::nullopt, false);
    check(session.commit() == a, "unknown focus starts at first candidate");
    session.begin(windows, WindowID{99, 99}, true);
    check(session.commit() == c, "focus outside scope starts at last when reversing");
    session.begin({windows.front()}, a, true);
    session.step(false);
    check(session.commit() == a, "single window remains selected");
    session.begin({}, a, false);
    session.step(true);
    check(!session.active() && !session.commit(), "empty catalog is safe");
    session.begin(windows, a, false);
    session.cancel();
    check(!session.commit() && session.windows().empty(), "cancel never activates");
    auto duplicates = windows;
    duplicates.push_back(windows.front());
    duplicates.push_back({{0, 5}, "Invalid", ""});
    duplicates.push_back({{10, 0}, "Invalid", ""});
    duplicates.push_back({{30, 1}, "Other process", "Reused number"});
    session.begin(duplicates, a, false);
    check(session.windows().size() == 4, "deduplicates identities and drops invalid IDs");
    check(session.windows()[3].id == WindowID{30, 1}, "identity includes process");
}
Input down(Key key, bool option = true, bool shift = false, bool repeat = false) {
    return {InputType::Down, key, option, shift, false, repeat};
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
    result = keyboard.handle({InputType::Modifiers, Key::Other, true}, false);
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
    result = keyboard.handle({InputType::Modifiers, Key::Other, true, false, true}, false);
    check(result.intent == Intent::Cancel, "Command or Control cancels");
    keyboard.reset();
    auto modified = down(Key::Tab);
    modified.otherModifier = true;
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
} // namespace
int main() {
    try {
        selection();
        keyboard();
        std::cout << checks << " switcher checks passed\n";
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
