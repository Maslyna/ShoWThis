# Desktop verification

## Automated

`DesktopIntegrationTest` uses a private D-Bus service and actual Unix screenshot
pipes. It covers malformed metadata, excess/incomplete payloads, stalled writers,
capture recovery, portal responses emitted before method replies, denial,
timeouts, and explicit retry. No compositor or live portal is changed.

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
3. Toggle rapidly during capture/OCR. Hide the overlay while work runs; it must
   stay hidden. Quit during recognition and verify cooperative shutdown.
4. Deny shortcut permission. Confirm an actionable warning, tray capture, and
   successful manual retry. Separately verify recovery after the portal service
   restarts, without starting another application instance.
5. Open the Anki save dialog: the exclusive overlay must hide, normal keyboard
   focus must return, and accepting/cancelling must restore the notebook.
6. Remove/disable OCR language data in an isolated test environment. The notebook
   must still open and attempted OCR must report how to install/select data.
7. Test a fresh user profile with QSQLITE and all required runtime plugins
   installed. Do not rely on files in a developer build directory.

The application intentionally targets KDE/KWin Wayland, not arbitrary Wayland
compositors, X11, Windows, or macOS. Other platforms need different capture and
window-layer implementations.
