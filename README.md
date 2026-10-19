# Spacewing

A native macOS Spaces MVP: configurable shortcuts to switch desktops or carry the
focused window with you. The engine is C++20, with Objective-C++ for AppKit and the
native window-move bridge. An independent window-switcher package supplies an
Option+Tab overlay. Tiling remains outside this MVP.

## Build and launch

Requires macOS, Xcode Command Line Tools, and CMake 3.25 or newer. The TOML
dependency is vendored; building requires no network access.

~~~sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
open build/Spacewing.app
~~~

The build always generates **build/compile_commands.json**.
The root **.clangd** reads it; **.clang-format** uses four spaces and no tabs.
Feature code lives under **packages/workspace/** and **packages/switcher/**;
shared foundations live under **packages/shared/**. The combined application's
entry points, configuration, IPC, Settings, and runtime wiring live under **src/**.
Headers and implementations always live next to each other, including inside
packages; there is no separate public **include/** tree.

The app adds a **↔** menu-bar item with Settings, Accessibility, Reload Config,
status, and Quit. When Accessibility access is missing, startup requests the
native macOS permission prompt. Use its **Open System Settings** button to grant
**Spacewing.app** access, then quit and reopen the app. The **Accessibility…** menu
item and the button in Spacewing Settings reopen the permission page if you dismiss
the prompt. If the app is not listed, use **+** and select it in the build directory.
A permission granted to a terminal is not necessarily a grant to an app opened
through Finder. Reloading config and running CLI commands do not trigger prompts.

There is no login service or automatic startup. To select a different config:

~~~sh
open build/Spacewing.app --args --config /absolute/path/workspaces.toml
# Equivalent flag syntax:
open build/Spacewing.app --args --config=/absolute/path/workspaces.toml
~~~

Quit an already running instance before changing its startup config. The executable
also runs as a foreground resident process when invoked without a command:

~~~sh
./build/spacewing --config /absolute/path/workspaces.toml
~~~

A terminal launch may use that terminal's Accessibility grant instead.
The build packages and signs a fresh app bundle before replacing the old one.
It never overwrites a running executable. Quit and reopen after rebuilding to
use the new version. The default ad-hoc signature can require renewing Accessibility
access; configure a persistent signing identity below to avoid this on every rebuild.

## Keep Accessibility access across builds

macOS tracks the app's code-signing identity. The default ad-hoc signature identifies
one exact build. A certificate-backed signature with the same bundle identifier and
compatible designated requirement lets macOS recognize updates. Keep the certificate
and its private key; recreating a self-signed certificate with the same name creates
a different identity. See [Apple's explanation of designated requirements](https://developer.apple.com/documentation/technotes/tn3127-inside-code-signing-requirements).

For local development, use an existing **Apple Development** identity or create a
local certificate once in **Keychain Access → Certificate Assistant → Create a
Certificate**. Name it **Spacewing Development**, choose **Self Signed Root** and
**Code Signing**, and keep it in your login keychain. This local option needs no
paid developer membership. If necessary, set the certificate's **Trust → Code
Signing** policy to **Always Trust**. See [Apple's certificate creation guide](https://support.apple.com/guide/keychain-access/create-self-signed-certificates-kyca8916/mac).

List available identities, then configure CMake with the certificate's name or
SHA-1 fingerprint (use the fingerprint if names are ambiguous):

~~~sh
security find-identity -v -p codesigning
cmake -S . -B build -DSPACEWING_CODESIGN_IDENTITY="Spacewing Development"
cmake --build build -j
~~~

CMake remembers this choice for subsequent builds, signs both the resident and its
settings helper, and repackages if the signing options change. An unavailable
identity fails the build; it never silently falls back to ad-hoc signing. Local
builds do not contact a timestamp service.

Quit the old app, remove its old Accessibility entry, open the newly signed app,
grant it access, and restart once. Later builds using the same identity should
retain that grant. Development and distribution signatures are different identities;
switching between them may require another grant. Keep each installed channel's
signer consistent. If you later ship separate dev and production apps side by side,
give them distinct bundle identifiers and grant each separately.

For production distributed outside the Mac App Store, use a **Developer ID
Application** certificate from your Apple Developer Program team:

~~~sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release \
  -DSPACEWING_CODESIGN_IDENTITY="Developer ID Application: YOUR NAME (TEAMID)" \
  -DSPACEWING_DISTRIBUTION=ON
cmake --build build-release -j
~~~

Distribution mode enables hardened runtime and secure timestamps for both apps,
and verifies their Developer ID certificate chains before replacing the output
bundle. It requires network access to Apple's timestamp service. Signing does not
upload or notarize the app. Before shipping, submit it with **notarytool**, staple
the accepted ticket with **stapler**, and test the notarized build on a clean Mac.
See [Apple's distribution guide](https://developer.apple.com/developer-id/).
Notarization handles Gatekeeper; users still grant Accessibility themselves once.
Production updates should keep the bundle identifier, team, and compatible signing
requirements. Certificate renewal within that identity does not inherently require
new permission grants.

Inspect the signature and designated requirement with:

~~~sh
codesign --verify --deep --strict build/Spacewing.app
codesign -d -r- build/Spacewing.app
~~~

Certificate-backed builds should identify the signer and bundle rather than only
an exact binary's **cdhash**. The certificate-backed and notarization flows require
your signing identity and credentials; they have not been validated in the tool
sandbox, which currently exposes no valid signing identities.

## Configuration

The default file is **~/.spacewing**, created on first launch if absent:

~~~toml
[shortcuts]
left = "ctrl+shift+option+left"
right = "ctrl+shift+option+right"
drag_left = "ctrl+shift+cmd+left"
drag_right = "ctrl+shift+cmd+right"

[switcher]
enabled = true
scope = "current-desktop" # Or "all-desktops".
~~~

Every desktop action has an independent shortcut. Use Left or Right with any combination
of Control, Shift, Option, and Command; at least one modifier is required. Modifier
order and letter case do not matter. Control/ctrl, alt/option, and command/cmd are
aliases. Empty or omitted bindings are disabled. Unknown settings, duplicate chords,
and malformed TOML are errors.

The settings window lets you record or type each chord, clear bindings, and save
to the same file. During recording, existing Spacewing shortcuts are captured
without switching desktops. Recording ends on Escape or loss of window focus;
a short lease also expires if the settings process disappears.

File edits reload automatically after a 100 ms debounce, including atomic editor
saves. Invalid changes preserve the previous working bindings. At startup, invalid
configuration leaves shortcuts disabled while settings and diagnostics remain
available. Fix malformed files in your editor, then reload the settings window.
An explicitly supplied missing config is an error; it does not fall back to defaults.

Settings saves retain comments and follow symlinks without replacing the symlink.
They check for external edits before saving, and preserve existing bindings if
registration or writing fails. Formatting may be normalized by the TOML serializer.
If a shortcut is already reserved, choose another or change its existing assignment
yourself; Spacewing does not rewrite macOS keyboard settings.

## Window switching

Hold **Option** and press **Tab** to select the next individual window. Keep Option
held and press Tab again to advance, add **Shift** to reverse, release Option to
activate, or press **Escape** to cancel. A held Tab does not repeat. The overlay
shows app icons and window titles without taking keyboard focus. Other typing
cancels the picker and passes through to the focused app.

The default scope is the currently active native desktop. Change **Window scope**
in Settings, or set **switcher.scope** to **"all-desktops"** in the config file.
Set **switcher.enabled = false** to disable the feature. Both settings reload live;
an omitted switcher table uses the defaults above, and invalid edits preserve the
last valid configuration. The initial keyboard binding is fixed to Option+Tab.

This first version lists standard Accessibility windows and dialogs, excluding
minimized windows, desktop elements, and Spacewing's own overlay. It starts from
WindowServer front-to-back order and freezes that order while Option is held;
it does not yet maintain a persistent most-recently-used history or show previews.
Titles come from Accessibility; no screen images are captured. Unsupported or
unresponsive applications are skipped; discovery stops querying apps after a
two-second budget and uses the candidates found so far. A selected window that closes or leaves
the current desktop before activation reports an error without falling back to a
different window. In all-desktops mode, activation relies on macOS to follow the
selected window; it does not move the window between desktops.

A desktop action, desktop change, config change affecting the switcher, shortcut
recording, or sleep cancels an open picker. While a desktop operation or window
activation is in flight, new switcher gestures pass through. Discovery runs on a
serial worker, outside the keyboard callback. A quick Option release is retained
until discovery finishes; cancelling invalidates that result. Switcher startup
failures appear in the menu status, Settings, and doctor, while desktop shortcuts
remain available.

### Package boundaries and source layout

| Location | Owns | Depends on |
| --- | --- | --- |
| **packages/workspace** | Desktop requests, shortcut parsing/registration, gestures, native Spaces, window moves, and workspace tests | shared |
| **packages/switcher** | Window selection, keyboard state, discovery/activation, overlay, and switcher tests | shared for its macOS service |
| **packages/shared** | Error/result types, Core Foundation ownership, main-thread dispatch, Accessibility, and application activation | Standard library and macOS frameworks |
| **src** | Combined CLI, TOML schema/persistence, IPC protocol, Settings/menu, and runtime composition | Public APIs of the packages |

Workspace and switcher have no dependency on each other or on **src/**. Shared has
no feature or application dependencies. Keep shared limited to infrastructure that
has actual consumers; feature policy stays in its owning package. Combined config
and Settings stay with the app because they assemble both features.

Each package keeps headers and implementations directly in its **src/** directory.
Linking a package's CMake target provides its include directory, so consumers use
includes such as **"controller.h"** or **"cf_ref.h"**. Do not repeat the repository
or package name beneath **src/**. For example:

~~~text
packages/
  workspace/src/controller.h
  workspace/src/controller.cc
  switcher/src/service.h
  switcher/src/service.mm
  shared/src/macos.h
  shared/src/macos.mm
src/
  command.h / command.cc
  runtime.h / runtime.cc
  executor.h / executor.cc
  config/     # Combined app configuration
  ipc/        # Resident/settings/CLI protocol
  platform/   # App menu, settings helper, config watching
~~~

Header-only types and templates do not need an empty implementation file. Tests
live inside the owning package; app-level config, CLI, IPC, and executor tests
remain under the root **tests/** directory.

The app's [Runtime](src/runtime.h) composes **workspace::Controller** and
**switcher::Service**. Workspace takes a typed **workspace::Request**, independent
of CLI parsing, and its binding set accepts only workspace shortcuts. Switcher
communication uses a small main-thread interface in
[service.h](packages/switcher/src/service.h): the host provides
availability, session-start, and error callbacks, and sends configuration and
cancellation notifications. Configuration crosses that boundary as an Options
value. The current transport is in-process; a future separate process can supply
an adapter without changing selection logic.

CMake enforces the dependencies through **Spacewing::Workspace**,
**Spacewing::Switcher**, and **Spacewing::SharedMacOS** targets. Portable types are
available through **Spacewing::Shared**, **Spacewing::WorkspaceModel**, and
**Spacewing::SwitcherModel**. The app-only **spacewing_host** target is never a
package dependency.

Build and test either feature without the combined app. Keep its **shared** sibling
when extracting a feature; standalone CMake loads that dependency automatically:

~~~sh
cmake -S packages/workspace -B build-workspace -DCMAKE_BUILD_TYPE=Release
cmake --build build-workspace -j
ctest --test-dir build-workspace --output-on-failure

cmake -S packages/switcher -B build-switcher -DCMAKE_BUILD_TYPE=Release
cmake --build build-switcher -j
ctest --test-dir build-switcher --output-on-failure
./build-switcher/spacewing-switcher
# Optional independent-host scope:
./build-switcher/spacewing-switcher --all-desktops
~~~

The standalone workspace build produces its libraries and tests. The minimal
standalone switcher host does not read Spacewing's TOML or launch its Settings
helper. It runs in the foreground; Ctrl+C quits. It needs its own applicable
Accessibility grant. Run one switcher host at a time to avoid competing Option+Tab
handlers. The integrated app remains the normal launch path and owns config
persistence and Settings. Portable models build without macOS; native services
and their executable hosts require macOS.

## CLI

The app must already be running. Add the build directory to your shell's PATH or use:

~~~sh
./build/spacewing left
./build/spacewing right --drag-active
./build/spacewing right --dry-run
./build/spacewing left --verify
./build/spacewing settings
./build/spacewing reload
./build/spacewing list
./build/spacewing doctor
./build/spacewing check-config --config resources/default.toml
~~~

Both config flag forms work before or after a command. Without the flag, a client
uses the resident app's config. With the flag, the requested path must match the
resident app's path. The check-config command validates a file offline; it does not
check whether another application owns a shortcut.

Workspace commands return after observing the target native Space, or immediately
at a boundary. The verify flag prints the native ID and dispatch/completion timings.
These timings are **not** measurements of rendered pixels or input readiness.
Dry-run resolves the target and builds events without moving windows, posting
gestures, or moving the pointer. It still needs Accessibility to identify focus.

Exit codes: 0 success/no-op, 1 platform error, 64 usage, 69 resident unavailable,
75 timeout/interruption/busy, 77 permission denied, 78 configuration error.
The CLI never starts the resident automatically. Commands send requests to the
resident, which performs Accessibility operations using its own permission; the
CLI client does not need a separate grant.

## Workspace behavior

- Shortcuts follow the **focused window's display**, even if the pointer is elsewhere.
  With no focused window, or if an app's Accessibility window cannot be identified,
  plain switching and listing retain the last focused display; the pointer's display
  is the initial fallback. Carrying still requires an identifiable focused window.
- One action runs per key press. Holding the key does not repeat. Pending actions
  execute in order, up to 32; destinations are resolved when each action starts.
- Left/right follow Mission Control order without wrapping. Ordinary switching
  includes fullscreen Spaces; carrying requires ordinary desktops and a window
  assigned only to the current desktop.
- Carrying moves **one focused window**, observes its membership, follows it, and
  restores focus. It avoids an extra gesture if macOS already followed the move.
  A failed move does not trigger a switch. An interruption after moving reports
  the partial result without trying to undo it.
- Gestures follow the pointer at the macOS level. Spacewing temporarily routes it
  to the chosen display when necessary and restores it unless you moved it yourself.
- The resident stays windowless. Settings runs in an on-demand helper, so empty
  desktop handling does not bring the settings window forward.

The backend sends native Dock gestures and uses WindowServer operations to carry
windows between Spaces. It runs with SIP enabled and needs Accessibility access.
It does not require screen capture access or change window sizes or desktop counts.

## Validation

~~~sh
ctest --test-dir build --output-on-failure
./build/core_tests
./build/packages/workspace/workspace_shortcut_tests
./build/packages/workspace/workspace_tests
./build/packages/switcher/switcher_tests
~~~

Tests exercise command parsing, TOML validation and comment retention, atomic saves,
symlinks, hotkey rollback and held keys, gesture golden bytes, display selection,
unavailable Accessibility focus, move/follow/focus failures, and the bounded action
queue. Switcher tests cover individual-window cycling, wrapping, reverse selection,
commit/cancel, held keys, modifier release order, and host blocking. Configuration
tests cover switcher defaults, scope validation, and settings/comment retention.
The IPC integration test uses temporary local sockets. Tests never post gestures
or move windows.

The integrated release build, standalone workspace and switcher builds, and their
non-interactive test suites pass on this development host, including IPC integration when run with
local socket access. These tests do not install an event tap or activate windows.
The following checks cover live behavior beyond the automated suite:

1. Run the tests in a normal terminal; confirm the IPC test passes rather than skips.
2. Launch the app, grant Accessibility, restart, and run doctor.
3. Test left/right, boundaries, fullscreen neighbors, and rapid separate key presses.
   A held shortcut should move once.
4. Carry the terminal both ways and type immediately after landing. Check visual
   latency and keyboard focus; test two windows of the same app.
5. On multiple displays, leave the pointer away from the focused window. Test both
   separate and shared Spaces, and empty desktops without bounce-back.
6. Change shortcuts through Settings and the file. Try a duplicate binding and an
   invalid edit; confirm the previous shortcuts continue working. Record an already
   assigned chord and confirm that recording does not switch desktops.
7. Keep Settings open on another Space while switching to an empty desktop.
8. Focus Ghidra (or another app with an unsupported Accessibility window) and switch
   both directions. Plain navigation should use the last focused display, or the
   pointer's display after a fresh launch. Carrying an unidentified window should
   report an error without moving anything.

9. Open two windows of one app and another app. Check Option+Tab, repeated Tab,
   Shift+Tab, Option release before Tab release, and Escape without focus changing.
   Check quick taps while discovery is pending and close the selected window before
   releasing Option.
10. Test both scopes with multiple desktops, fullscreen apps, and separate displays.
    Change scope and enabled state through both Settings and the file, including
    while the picker is open. Record a desktop shortcut while Option+Tab is held.
    Check sleep/wake, permission revocation, and an unresponsive app.

The earlier desktop-only AddressSanitizer/UndefinedBehaviorSanitizer build compiled,
but its runs stalled during initialization, including a trivial program before main.
No sanitizer pass is claimed for this change.

The gesture encoding includes the macOS 27 path, covered by byte-level tests.
Actual acceptance of that path needs a macOS 27 desktop; no OS version is blacklisted.

Third-party source attribution and license texts are in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
