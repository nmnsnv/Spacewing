#include "session.h"
#include "preview_layout.h"
#include "switching.h"
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
void previewGridAndNavigation() {
    check(previewGrid(0, {}).columns == 0, "empty sessions have no preview grid");
    for (const PreviewArea area : {PreviewArea{1100, 700}, {760, 540}, {1440, 900}, {500, 320}}) {
        for (size_t count = 1; count <= 100; ++count) {
            const auto grid = previewGrid(count, area);
            check(grid.columns > 0 && grid.columns <= count &&
                      grid.rows == (count + grid.columns - 1) / grid.columns,
                  "grid contains every window exactly once, including a partial final row");
            check(grid.width <= area.width + 0.01 && grid.height <= area.height + 0.01 &&
                      grid.tileWidth > 0 && grid.tileWidth <= 196 && grid.tileHeight <= 162,
                  "compact previews fit all windows inside the available screen");
            std::vector<Candidate> catalog;
            for (size_t i = 0; i < count; ++i)
                catalog.push_back({{10, static_cast<uint32_t>(i + 1)}, "Editor", "Window"});
            Session session;
            session.begin(catalog, std::nullopt);
            for (size_t i = 0; i < count; ++i) {
                const auto selected = session.selected();
                for (auto direction : {Direction::Left, Direction::Right, Direction::Up, Direction::Down}) {
                    Session moved = session;
                    moved.move(direction, grid.columns);
                    const auto target = moved.selected();
                    check(target < count, "arrow movement never selects an empty cell");
                    if (direction == Direction::Left)
                        check(target == (selected + count - 1) % count,
                              "Left selects the previous window like Option+Shift+Tab, including wrap");
                    else if (direction == Direction::Right)
                        check(target == (selected + 1) % count,
                              "Right selects the next window like Option+Tab, including wrap");
                    else
                        check(target % grid.columns == selected % grid.columns,
                              "vertical arrows wrap within the same occupied column");
                    const auto opposite = direction == Direction::Left ? Direction::Right
                        : direction == Direction::Right ? Direction::Left
                        : direction == Direction::Up ? Direction::Down : Direction::Up;
                    moved.move(opposite, grid.columns);
                    check(moved.selected() == selected,
                          "opposite arrows return to the same cell, including wrap and partial rows");
                }
                session.step(false);
            }
        }
    }
    check(previewGrid(8, {1100, 700}).columns == 4, "compact default uses four columns");
    std::vector<Candidate> catalog;
    for (uint32_t i = 1; i <= 10; ++i)
        catalog.push_back({{10, i}, "Editor", "Window"});
    Controller controller;
    controller.setPreviewArea({1100, 700});
    controller.accept(Intent::Forward);
    controller.accept(Intent::Down); // Queued before the catalog is available.
    controller.discovered(catalog, catalog[0].id);
    check(controller.session().selected() == 5, "queued Down moves one complete grid row");
    controller.accept(Intent::Right);
    check(controller.session().selected() == 6, "Right selects the next window");
    controller.accept(Intent::Down);
    check(controller.session().selected() == 2,
          "Down wraps to the top when the final row has no cell in this column");
    controller.accept(Intent::Left);
    controller.accept(Intent::Up);
    check(controller.session().selected() == 9, "Up from the top reaches the lowest occupied cell");
    controller.accept(Intent::Down);
    check(controller.session().selected() == 1, "Down from the lowest cell returns to the top");
    controller.accept(Intent::Left);
    controller.accept(Intent::Left);
    check(controller.session().selected() == 9, "Left wraps from the first window to the last");
    controller.accept(Intent::Right);
    check(controller.session().selected() == 0, "Right wraps from the last window to the first");
    controller.accept(Intent::Right);
    controller.accept(Intent::Commit);
    check(controller.target() == catalog[1].id, "release commits the exact selected window");
    Session session;
    session.begin(catalog, catalog[0].id);
    session.move(Direction::Right, 4);
    check(session.selected() == 2, "Right selects the next window");
    session.move(Direction::Right, 4);
    session.move(Direction::Right, 4);
    check(session.selected() == 4, "Right crosses from the end of a row to the next row");
    session.move(Direction::Left, 4);
    check(session.selected() == 3, "Left crosses from the start of a row to the previous row");
    session.move(Direction::Left, 4);
    session.move(Direction::Left, 4);
    session.move(Direction::Left, 4);
    check(session.selected() == 0, "Left selects the previous window");
    session.move(Direction::Left, 4);
    check(session.selected() == 9, "Left from the first window wraps to the last window");
    session.move(Direction::Right, 4);
    check(session.selected() == 0, "Right from the last window wraps to the first window");
    session.move(Direction::Up, 4);
    check(session.selected() == 8, "Up reaches the final partial row in the same column");
    session.move(Direction::Left, 4);
    check(session.selected() == 7, "Left from the partial row reaches the previous full row");
    session.move(Direction::Right, 4);
    check(session.selected() == 8, "Right from the full row reaches the final partial row");
    session.move(Direction::Right, 4);
    check(session.selected() == 9, "Right reaches the final window in a partial row");
    session.move(Direction::Right, 4);
    check(session.selected() == 0, "Right skips missing cells when wrapping to the first window");
    session.begin({catalog[0]}, catalog[0].id);
    for (auto direction : {Direction::Left, Direction::Right, Direction::Up, Direction::Down}) {
        session.move(direction, 4);
        check(session.selected() == 0, "a single window wraps to itself in every direction");
    }
}
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
Keyboard configuredKeyboard(const Shortcuts &shortcuts = defaultShortcuts()) {
    Keyboard keyboard;
    keyboard.configure(shortcuts);
    return keyboard;
}
Shortcuts withPrimary(Shortcut shortcut) {
    auto shortcuts = defaultShortcuts();
    shortcuts[0] = shortcut;
    return shortcuts;
}
void listLayout() {
    Controller controller;
    controller.setLayout(Layout::List);
    controller.accept(Intent::Forward);
    controller.accept(Intent::Down);
    controller.discovered(windows, a);
    check(controller.session().selected() == 2, "list Down advances one row during discovery");
    controller.accept(Intent::Down);
    check(controller.session().selected() == 0, "list Down wraps to the first row");
    controller.accept(Intent::Up);
    check(controller.session().selected() == 2, "list Up wraps to the last row");
    controller.accept(Intent::Commit);
    check(controller.target() == c, "list selection commits its exact window");

    Keyboard keyboard;
    keyboard.configure(defaultShortcuts(), Layout::List);
    keyboard.handle(down(Key::Tab), false);
    check(keyboard.handle(down(Key::Down), false).intent == Intent::Down,
          "list retains Up/Down navigation");
    auto result = keyboard.handle(down(Key::Right), false);
    check(!result.consume && result.intent == Intent::Cancel,
          "list retains the original pass-through behavior for horizontal arrows");
    keyboard.configure(defaultShortcuts(), Layout::Grid);
    keyboard.handle(down(Key::Tab), false);
    check(keyboard.handle(down(Key::Right), false).intent == Intent::Right,
          "changing to the grid enables horizontal navigation");
    keyboard.configure(defaultShortcuts(), Layout::List);
    check(keyboard.handle({InputType::Up, Key::Right}, false).consume,
          "changing layouts preserves ownership of an already consumed arrow");
}
void keyboard() {
    auto keyboard = configuredKeyboard();
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
    for (const auto [key, intent] : {std::pair{Key::Left, Intent::Left},
                                     {Key::Right, Intent::Right}, {Key::Up, Intent::Up},
                                     {Key::Down, Intent::Down}}) {
        auto input = configuredKeyboard();
        check(!input.handle(down(key), false).consume, "all arrows pass through outside the picker");
        input.handle(down(Key::Tab), false);
        auto result = input.handle(down(key), false);
        check(result.consume && result.intent == intent, "each arrow dispatches its grid direction");
        result = input.handle(down(key, true, false, true), false);
        check(result.consume && result.intent == intent, "held arrows repeat spatial movement");
        input.handle({InputType::Modifiers}, false);
        check(input.handle(down(key, false, false, true), false).consume &&
                  input.handle({InputType::Up, key}, false).consume,
              "all arrow repeats and releases remain paired after commit");
    }
    {
        auto input = configuredKeyboard(withPrimary(parseShortcut("ctrl+left")));
        check(input.handle({InputType::Down, Key::Left, Control}, false).intent == Intent::Forward,
              "existing arrow shortcut can still open the picker");
        check(input.handle({InputType::Down, Key::Left, Control, true}, false).intent == Intent::None,
              "a shortcut using an arrow retains shortcut repeat behavior");
        input.handle({InputType::Up, Key::Left}, false);
        check(input.handle({InputType::Down, Key::Left, Control}, false).intent == Intent::Forward,
              "explicitly configured arrow chords take precedence over grid movement");
    }
    auto keyboard = configuredKeyboard();
    check(!keyboard.handle(down(Key::Up), false).consume &&
              !keyboard.handle(down(Key::Down), false).consume,
          "arrow keys pass through outside the picker");
    keyboard.handle(down(Key::Tab), false);
    keyboard.handle({InputType::Up, Key::Tab}, false);
    auto result = keyboard.handle(down(Key::Up), false);
    check(result.consume && result.intent == Intent::Up, "Up moves selection up");
    result = keyboard.handle(down(Key::Up, true, false, true), false);
    check(result.consume && result.intent == Intent::Up, "held Up continues navigating");
    check(keyboard.handle({InputType::Up, Key::Up}, false).consume, "Up release is paired");
    result = keyboard.handle(down(Key::Down), false);
    check(result.consume && result.intent == Intent::Down, "Down moves selection down");
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
    check(parseShortcut(" ALT + tab ") == defaultShortcuts()[0], "default shortcut and alias");
    check(parseShortcut("Command + Control + Space").text() == "ctrl+cmd+space",
          "custom shortcut normalizes modifiers");
    check(parseShortcut("option+a").key == static_cast<Key>(0),
          "zero key code is a valid shortcut");
    for (auto text : {"tab", "ctrl", "ctrl++tab", "ctrl+option+alt+tab", "ctrl+up", "option+down",
                      "cmd+escape", "ctrl+tab+space", "ctrl+unknown"}) {
        bool rejected = false;
        try {
            parseShortcut(text);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(rejected, "invalid or reserved switcher shortcut rejected");
    }
    auto keyboard = configuredKeyboard();
    const auto shortcut = parseShortcut("ctrl+space");
    keyboard.configure(withPrimary(shortcut));
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
    check(keyboard.handle({InputType::Down, Key::Down, Control}, false).intent == Intent::Down,
          "arrow navigation uses the configured modifier");
    check(keyboard.handle({InputType::Modifiers}, false).intent == Intent::Commit,
          "releasing configured modifier commits");
    keyboard.configure(defaultShortcuts());
    check(keyboard.handle({InputType::Up, shortcut.key}, false).consume,
          "reconfiguration retains ownership of an old shortcut key-up");
    const auto multiple = parseShortcut("ctrl+option+a");
    keyboard.configure(withPrimary(multiple));
    result = keyboard.handle({InputType::Down, multiple.key, Control | Option}, false);
    check(result.consume && result.intent == Intent::Forward, "multiple modifiers and A open");
    check(keyboard.handle({InputType::Modifiers, Key::Other, Control}, false).intent ==
              Intent::Commit,
          "releasing any required modifier commits");
    keyboard.reset();
    keyboard.configure(withPrimary(parseShortcut("shift+tab")));
    result = keyboard.handle({InputType::Down, Key::Tab, Shift}, false);
    check(result.consume && result.intent == Intent::Forward,
          "Shift in the configured chord is not interpreted as reverse");
}
void quickTap(Controller &controller) {
    controller.accept(Intent::Forward);
    controller.accept(Intent::Commit);
}
void configuredBindings() {
    Keyboard unconfigured;
    for (unsigned key = 0; key < 128; ++key)
        for (unsigned modifiers = 0; modifiers < 16; ++modifiers)
            check(!unconfigured.handle({InputType::Down, static_cast<Key>(key), modifiers}, false)
                       .consume,
                  "an unconfigured keyboard never intercepts any chord");
    unconfigured.configure({});
    for (unsigned key = 0; key < 128; ++key)
        for (unsigned modifiers = 0; modifiers < 16; ++modifiers)
            check(!unconfigured.handle({InputType::Down, static_cast<Key>(key), modifiers}, false)
                       .consume,
                  "an empty configuration has no built-in fallback chords");
    check(!parseShortcut("  ").enabled(), "empty switcher binding is disabled");
    for (size_t i = 0; i < shortcutActions.size(); ++i) {
        Shortcuts bindings{};
        const auto custom = parseShortcut("ctrl+space");
        bindings[i] = custom;
        auto keyboard = configuredKeyboard(bindings);
        for (const auto &original : defaultShortcuts())
            check(!keyboard.handle({InputType::Down, original.key, original.modifiers}, false)
                       .consume,
                  "remapped bindings do not retain built-in fallback shortcuts");
        auto result = keyboard.handle({InputType::Down, custom.key, custom.modifiers}, false);
        check(result.consume && result.intent == Intent::Forward &&
                  result.mode == shortcutActions[i].mode,
              "every registered action dispatches from its configured chord");
        keyboard.handle({InputType::Up, custom.key}, false);
        result = keyboard.handle({InputType::Down, custom.key, custom.modifiers | Shift}, false);
        check(result.intent == Intent::Backward && result.mode == shortcutActions[i].mode,
              "every configured action supports its reverse chord");
        check(keyboard.handle({InputType::Modifiers, Key::Other, Control}, false).intent ==
                  Intent::None,
              "releasing extra Shift retains the configured action");
        result = keyboard.handle({InputType::Modifiers}, false);
        check(result.intent == Intent::Commit && result.mode == shortcutActions[i].mode,
              "every configured action commits on its own modifier release");
        keyboard.configure({});
        check(keyboard.handle({InputType::Up, custom.key}, false).consume,
              "clearing configuration retains ownership of a swallowed key-up");
        check(!keyboard.handle({InputType::Down, custom.key, custom.modifiers}, false).consume,
              "clearing an action removes its configured binding");
    }
    for (const auto chord : {"option+tab", "option+shift+tab"}) {
        auto bindings = defaultShortcuts();
        bindings[1] = parseShortcut(chord);
        auto keyboard = configuredKeyboard();
        bool rejected = false;
        try {
            keyboard.configure(bindings);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(rejected, "overlapping forward or reverse bindings are rejected");
        check(keyboard.handle(down(Key::Grave), false).mode == Mode::ApplicationWindows,
              "invalid reconfiguration preserves the previous valid bindings");
    }
}
void applicationKeyboard() {
    auto keyboard = configuredKeyboard();
    check(!keyboard.handle(down(Key::Grave, false), false).consume,
          "ordinary backtick passes through");
    check(!keyboard.handle({InputType::Down, Key::Grave, CommandKey}, false).consume,
          "native Command+backtick passes through");
    check(!keyboard.handle({InputType::Down, Key::Grave, Option | Control}, false).consume,
          "unrelated modified backtick passes through");
    check(!keyboard.handle(down(Key::Grave), true).consume && !keyboard.active(),
          "busy host preserves Option+backtick");
    check(!keyboard.handle(down(Key::Grave, true, false, true), false).consume,
          "held backtick cannot open a picker");
    auto result = keyboard.handle(down(Key::Grave), false);
    check(result.consume && result.intent == Intent::Forward &&
              result.mode == Mode::ApplicationWindows && keyboard.active(),
          "Option+backtick opens the application picker");
    check(keyboard.handle(down(Key::Grave, true, false, true), false).intent == Intent::None,
          "held backtick does not cycle");
    check(keyboard.handle({InputType::Up, Key::Grave}, false).consume && keyboard.active(),
          "backtick release is paired and keeps the picker open");
    result = keyboard.handle(down(Key::Grave, true, true), false);
    check(result.consume && result.intent == Intent::Backward &&
              result.mode == Mode::ApplicationWindows,
          "Shift+Option+backtick reverses within the application");
    check(keyboard.handle({InputType::Modifiers, Key::Other, Option}, false).intent == Intent::None,
          "releasing extra Shift keeps the application picker open");
    result = keyboard.handle(down(Key::Up), false);
    check(result.intent == Intent::Up && result.mode == Mode::ApplicationWindows,
          "arrow navigation retains the application filter");
    result = keyboard.handle({InputType::Modifiers}, false);
    check(result.intent == Intent::Commit && result.mode == Mode::ApplicationWindows &&
              !keyboard.active(),
          "Option release commits the application picker");
    check(keyboard.handle(down(Key::Grave, false, false, true), true).consume &&
              keyboard.handle({InputType::Up, Key::Grave}, false).consume,
          "backtick repeats and key-up stay paired after modifier release");
    keyboard.reset();
    keyboard.handle(down(Key::Grave), false);
    result = keyboard.handle(down(Key::Escape), false);
    check(result.consume && result.intent == Intent::Cancel && !keyboard.active(),
          "Escape cancels the application picker");
    check(keyboard.handle({InputType::Modifiers}, false).intent == Intent::None,
          "release after cancelling the application picker does not commit");
    keyboard.reset();
    keyboard.configure(withPrimary(parseShortcut("ctrl+space")));
    result = keyboard.handle(down(Key::Grave), false);
    check(result.mode == Mode::ApplicationWindows && keyboard.active(),
          "application shortcut remains available with a custom main shortcut");
    check(keyboard.handle({InputType::Modifiers, Key::Other, Option}, false).intent == Intent::None,
          "application picker uses Option even when the main shortcut uses Control");
    check(keyboard.handle({InputType::Modifiers}, false).intent == Intent::Commit,
          "application picker commits on Option release with a custom main shortcut");
    keyboard.reset();
    auto remapped = defaultShortcuts();
    std::swap(remapped[0], remapped[1]);
    keyboard.configure(remapped);
    result = keyboard.handle(down(Key::Grave), false);
    check(result.mode == Mode::AllWindows && result.intent == Intent::Forward,
          "swapping configured shortcuts changes their actions without fixed-key fallbacks");
}
void applicationSwitching() {
    auto keyboard = configuredKeyboard();
    Controller controller;
    const auto send = [&](Input input) {
        const auto decision = keyboard.handle(input, false);
        controller.accept(decision.intent, decision.mode);
    };
    send(down(Key::Grave));
    controller.discovered({windows[0], windows[2], windows[1]}, a);
    check(controller.session().windows().size() == 2 && controller.session().windows()[0].id == a &&
              controller.session().windows()[1].id == b && controller.session().selected() == 1,
          "application picker skips another app even when it is next in window order");
    send({InputType::Up, Key::Grave});
    send(down(Key::Grave));
    check(controller.session().selected() == 0, "application cycling wraps within the app");
    send({InputType::Up, Key::Grave});
    send(down(Key::Grave, true, true));
    check(controller.session().selected() == 1, "application reverse cycling wraps within the app");
    send({InputType::Up, Key::Grave});
    send(down(Key::Tab));
    check(controller.session().windows().size() == 3 && controller.session().windows()[1].id == c,
          "Option+Tab restores all windows during the same modifier hold");
    send({InputType::Up, Key::Tab});
    send(down(Key::Grave));
    send({InputType::Modifiers});
    check(controller.target() == b, "returning to Option+backtick selects only this app");
    send({InputType::Up, Key::Grave});
    // Queue both modes while the first native activation is still pending.
    send(down(Key::Tab));
    send({InputType::Up, Key::Tab});
    send(down(Key::Tab));
    send({InputType::Up, Key::Tab});
    send({InputType::Modifiers});
    send(down(Key::Grave));
    send({InputType::Up, Key::Grave});
    send({InputType::Modifiers});
    check(controller.activated(b) && controller.target() == c,
          "queued all-window gesture restores the full catalog after an application gesture");
    check(controller.activated(c) && controller.target() == c,
          "queued application gesture uses the newly activated app and handles its single window");
    check(controller.activated(c) && controller.phase() == Phase::Idle,
          "mixed queued gestures finish exactly once");
    for (int i = 0; i < 4; ++i) {
        send(down(Key::Grave));
        send({InputType::Modifiers});
        send({InputType::Up, Key::Grave});
    }
    controller.discovered({windows[2], windows[0], windows[1]}, a);
    for (const auto expected : {b, a, b, a}) {
        check(
            controller.target() == expected && controller.activated(expected),
            "quick application gestures queued during discovery alternate only that app's windows");
    }
    check(controller.phase() == Phase::Idle, "rapid application gestures drain the queue");
    controller.accept(Intent::Forward, Mode::ApplicationWindows);
    controller.discovered(windows, std::nullopt, a.pid);
    check(controller.session().windows().size() == 2,
          "known frontmost app still filters when its focused AX window is unavailable");
    controller.cancel();
    controller.accept(Intent::Forward, Mode::ApplicationWindows);
    controller.discovered(windows, std::nullopt);
    check(controller.phase() == Phase::Idle,
          "unknown frontmost app cannot fall back to switching another app");
    controller.accept(Intent::Forward, Mode::ApplicationWindows);
    controller.discovered({windows[2]}, a);
    check(controller.phase() == Phase::Idle && !controller.target(),
          "no eligible windows in the current app closes without activating another app");
}
void rapidSwitching() {
    Controller controller;
    quickTap(controller);
    // More complete gestures can arrive before the first catalog is ready.
    for (int i = 0; i < 1000; ++i)
        quickTap(controller);
    check(controller.phase() == Phase::Discovering, "rapid taps wait for first discovery");
    controller.discovered(windows, a);
    check(controller.focused() == a, "activation request alone leaves the observed focus intact");
    check(controller.target() == b && controller.phase() == Phase::Activating,
          "first quick tap activates the second window");
    check(!controller.activated(c) && controller.target() == b,
          "focus on a third window cannot complete the requested switch");
    check(!controller.activated(a), "another window in the same app cannot confirm activation");
    check(controller.focused() == a, "wrong focus acknowledgment cannot change recorded focus");
    check(!controller.activated(WindowID{20, b.number}),
          "matching a window number in the wrong app cannot complete activation");
    for (int i = 0; i <= 1000; ++i) {
        const auto expected = i % 2 == 0 ? b : a;
        check(controller.target() == expected, "rapid gestures alternate the same two windows");
        check(controller.activated(expected), "exact focus confirms each switch");
        check(controller.focused() == expected,
              "confirmed focus is retained for delayed app notifications");
        if (i == 1000)
            break;
        check(controller.phase() == Phase::Activating,
              "queued gesture advances only after focus confirmation without another catalog");
        check(controller.session().windows().empty(), "queued release commits the next picker");
    }
    check(controller.phase() == Phase::Idle && !controller.target(),
          "all queued gestures finish exactly once");
    quickTap(controller);
    controller.discovered({windows[1], windows[2], windows[0]}, b);
    check(controller.target() == a, "a later tap still returns to the previous window");
    quickTap(controller); // Also queue a gesture during native activation.
    check(controller.activated(a) && controller.phase() == Phase::Activating,
          "gestures arriving during activation are retained");
    check(controller.target() == b, "queued gesture uses confirmed focus instead of stale focus");
    controller.activated(b);
    auto current = b;
    for (int i = 0; i < 1000; ++i) {
        quickTap(controller);
        check(controller.phase() == Phase::Discovering, "a later gesture refreshes the catalog");
        // Simulate a third window coming ahead of the previous one on every refresh.
        controller.discovered({windows[2], windows[1], windows[0]}, current);
        const auto expected = current == a ? b : a;
        check(controller.target() == expected, "fresh reordered snapshots retain the same pair");
        check(controller.activated(expected), "refreshed switch confirms exact focus");
        current = expected;
    }
}
void historyAndCancellation() {
    Controller controller;
    quickTap(controller);
    controller.discovered(windows, a);
    controller.activated(b);
    controller.accept(Intent::Forward);
    controller.discovered({windows[1], windows[2], windows[0]}, b);
    check(controller.session().windows()[0].id == b && controller.session().windows()[1].id == a,
          "picker displays active window first and previous window second");
    controller.accept(Intent::Forward);
    check(controller.session().selected() == 2,
          "holding the modifier and pressing Tab can still reach a third window");
    controller.accept(Intent::Cancel);
    check(controller.phase() == Phase::Idle, "Escape closes picker without altering history");
    quickTap(controller);
    controller.discovered({windows[1], windows[2], windows[0]}, b);
    check(controller.target() == a, "browsing and cancelling does not promote a third window");
    quickTap(controller);
    controller.cancel(); // Native failure or host cancellation while work is queued.
    check(!controller.activated(a) && controller.phase() == Phase::Idle,
          "cancelled activation and queued gestures cannot update history");
    quickTap(controller);
    controller.discovered({windows[1], windows[2], windows[0]}, b);
    check(controller.target() == a, "failed activation retains previous confirmed history");
    controller.cancel();
    quickTap(controller);
    controller.discovered({windows[1], windows[2]}, b);
    check(controller.target() == c, "closed or out-of-scope previous window is skipped");
    controller.activated(c);
    quickTap(controller);
    controller.discovered(windows, a); // An externally focused window was observed at opening.
    check(controller.target() == c, "externally focused window becomes first in history");
    controller.cancel();
    controller.accept(Intent::Backward);
    controller.discovered(windows, c);
    check(controller.session().selected() == 1 && controller.session().windows()[1].id == a,
          "opening with reverse shortcut still selects the previous window in row two");
    controller.accept(Intent::Backward);
    check(controller.session().selected() == 0, "reverse navigation reaches the active window");
    controller.cancel();
    quickTap(controller);
    controller.discovered({}, std::nullopt);
    check(controller.phase() == Phase::Idle && !controller.target(),
          "empty discovery clears pending gestures without activation");
    controller.accept(Intent::Forward);
    controller.accept(Intent::Cancel);
    controller.discovered(windows, a);
    check(controller.phase() == Phase::Idle, "cancelled discovery does not reopen a picker");
    quickTap(controller);
    controller.discovered({windows[0], windows[2], windows[1]}, a);
    check(controller.activated(c), "switch can confirm a window in another app");
    controller.accept(Intent::Forward); // A queued gesture whose modifier remains held.
    controller.discovered({windows[2], windows[1], windows[0]}, c);
    check(controller.session().windows()[1].id == a,
          "cross-app activation retains the previous window ahead of another app window");
    controller.accept(Intent::Commit);
    controller.accept(Intent::Forward);
    check(controller.activated(a) && controller.phase() == Phase::Picking &&
              controller.session().windows()[1].id == c,
          "a gesture held during activation opens its picker after confirmation");
    controller.accept(Intent::Backward);
    controller.accept(Intent::Commit);
    check(controller.target() == a, "queued picker navigation can select the active window");
}
} // namespace
int main() {
    try {
        previewGridAndNavigation();
        listLayout();
        selection();
        keyboard();
        arrows();
        shortcuts();
        configuredBindings();
        applicationKeyboard();
        applicationSwitching();
        rapidSwitching();
        historyAndCancellation();
        std::cout << checks << " switcher checks passed\n";
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
