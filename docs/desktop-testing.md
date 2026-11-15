# Desktop verification

## Automated

`DesktopIntegrationTest` uses a private D-Bus service and actual Unix screenshot
pipes. It covers malformed metadata, excess/incomplete payloads, stalled writers,
capture recovery, portal responses emitted before method replies, denial,
timeouts, missing/empty/unusable shortcut bindings, explicit v2 configuration,
v1/settings guidance, configuration errors, stale replies, binding changes and
activation filtering. No compositor or live portal is changed.

`OverlayTest` exercises manual context selection on the offscreen/software Qt
surface: additive/reversed rectangles, word toggles, overlap deduplication,
selection ordering, saving, cancel/Escape, stale-image cancellation, scaling,
resizing, and movable preview controls. Optional `SHOWTHIS_TEST_SCREENSHOTS`
captures desktop, compact, and small-screen context-picker views.

OCR uses a 15-second recognition deadline and cooperative shutdown cancellation.
Cancellation is checked before recognition and by Tesseract's progress monitor.
It is not a hard process kill: model initialization and external/native I/O do
not support forcible cancellation. The controller waits for native work to stop
before destroying its engine.

## Manual KDE Wayland check (required before distribution)

These steps require an actual logged-in KDE desktop; unit tests cannot verify
compositor permissions, keyboard grabs, or display geometry.

1. Install to the configured prefix and launch using the installed desktop entry.
   Confirm the global-shortcut permission prompt and bind Ctrl+Alt+Space.
2. Capture recognizable text on each monitor, including mixed scaling. Check
   screenshot/word-outline alignment and that the active monitor is used.
   Select a word, then **Select context**. Add several rectangles and toggle
   individual words; verify highlights match the preview with no overlap
   duplicates. Move the preview panel to select text underneath it. Apply and
   save, then reopen the card to verify its context. Cancel/Esc must preserve
   an unsaved draft; hiding or recapturing during selection must not apply it.
3. Toggle rapidly during capture/OCR. Hide the overlay while work runs; it must
   stay hidden. Quit during recognition and verify cooperative shutdown.
4. Deny shortcut permission. Confirm an actionable warning and working tray capture.
   On a version-2 GlobalShortcuts portal, choose **Retry shortcut setup**: KDE must
   open the application's shortcut settings, not silently repeat BindShortcuts.
   Assign a key to **Show or hide the screen overlay**, apply it, and verify the
   shortcut activates without restarting ShoWThis. Clear the key again: the tray
   retry action must become available and capture must continue working.
   Restart ShoWThis with the denied/empty binding persisted: it must not report
   shortcut readiness or gratuitously open settings. Retry must still open them.
   On a version-1 portal (or when configuration fails), confirm guidance to
   **System Settings > Keyboard > Shortcuts > ShoWThis** and retain tray capture.
   Separately restart the portal service: automatic recovery must not open
   configuration or start another application instance.
5. Open the Anki save dialog: the exclusive overlay must hide, normal keyboard
   focus must return, and accepting/cancelling must restore the notebook.
6. Remove/disable OCR language data in an isolated test environment. The notebook
   must still open and attempted OCR must report how to install/select data.
7. Test a fresh user profile with QSQLITE and all required runtime plugins
   installed. Do not rely on files in a developer build directory.

The protocol behavior follows the [GlobalShortcuts v2 interface](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.GlobalShortcuts.html).
`ConfigureShortcuts` only opens settings; readiness follows a nonempty
`trigger_description` for `toggle-window` in BindShortcuts/ShortcutsChanged.
[KDE's implementation](https://invent.kde.org/plasma/xdg-desktop-portal-kde/-/blob/master/src/globalshortcuts.cpp)
persists denied shortcut IDs with an empty key and does not prompt for those IDs
on later BindShortcuts calls. Keeping the session alive allows settings changes
to repair the shortcut without an application restart.

The application intentionally targets KDE/KWin Wayland, not arbitrary Wayland
compositors, X11, Windows, or macOS. Other platforms need different capture and
window-layer implementations.
