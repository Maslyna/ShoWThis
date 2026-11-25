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
Workspace tests cover the in-overlay notebook, inline context, divider dragging,
compact Dictionary/Card geometry, screenshot-selection round trips, and preserved
drafts. `TranslatorDialogTest` covers standalone operation in the same surface,
dialog movement/resizing/focus, system palettes, explicit append/save and credentials.
`LanguagePickerTest` covers typed filtering, arrows/Enter/Escape, regional codes,
stable model indices and a virtualized long language list.

UI regression coverage also includes the 460×360 notebook, visible translator
actions during scrolling, preserved personal fields and confirmed sense
replacement, compact OCR errors, keyboard OCR/context selection, named editable
picker inputs, and explicit installed-dictionary activation.
Layout checks also cover scrollable empty states and confirmations at 460×360,
plus compact translator focus/actions with a larger desktop font and long card names.

OCR uses a 15-second recognition deadline and cooperative shutdown cancellation.
Cancellation is checked before recognition and by Tesseract's progress monitor.
It is not a hard process kill: model initialization and external/native I/O do
not support forcible cancellation. The controller waits for native work to stop
before destroying its engine.

`MotionPreferencesTest` runs on a private D-Bus session and verifies the native
reduced-motion preference, live changes, malformed/missing values, stale replies
and portal restarts. It also checks that disabling motion clears active feedback.
Language and translator tests cover committed-versus-provisional selection,
immediate editable results, exact clipboard contents, and resettable copy feedback.
The identity regression fixture also checks settings-first and concurrent startup,
then shortcut activation after portal restart, with per-peer identity caching.

## Manual KDE Wayland check (required before distribution)

These steps require an actual logged-in KDE desktop; unit tests cannot verify
compositor permissions, keyboard grabs, or display geometry.

1. Install to the configured prefix and launch using the installed desktop entry.
   Confirm the global-shortcut permission prompt and bind Ctrl+Alt+Space.
2. Capture recognizable text on each monitor, including mixed scaling. Check
   screenshot/word-outline alignment and that the active monitor is used.
   Select a word, then **Select** beside its inline context. Add rectangles and toggle
   individual words; verify highlights match the preview with no overlap
   duplicates. Move the preview panel to select text underneath it. Apply and
   save, then reopen the card to verify its context. Cancel/Esc must preserve
   an unsaved draft; hiding or recapturing during selection must not apply it.
   Repeat word/context selection using **Ctrl+L**, **Up/Down**, **Enter** or
   **Space**. Confirm the focused word and checked state are announced by the
   desktop screen reader. Escape closes the word list before cancelling context.
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
5. Open the notebook and Translator inside the layer-shell overlay. Confirm no
   separate desktop windows appear. Drag either header and resize from its
   bottom-right corner; verify geometry stays within the screen and survives
   reopening. Both dialogs must remain interactive, with side-by-side initial
   placement where space permits. Confirm translator colors follow the desktop
   palette. Translate independently, switch cards, then explicitly add source/result
   text; only the named card draft changes and Save is still required.
   Search language dropdowns by typing; Escape must cancel the popup before closing
   its dialog. Close either dialog without closing its peer. Select screenshot
   context: both dialogs temporarily hide, then return without losing the standalone
   session. Open Anki export: the overlay must hide while the standard save dialog
   owns keyboard focus, then restore unless explicitly dismissed in the meantime.
   At the notebook's minimum size, scroll to a meaning, edit it and save without
   a clipped footer. Change lookup text and sources: personal fields must remain.
   Choose another dictionary meaning: Cancel keeps the draft; Replace fields
   changes only the named fields. In compact Translator, request/cancel and
   add-to-card actions must remain visible while text scrolls.
   Under Offline dictionaries, activate an installed pack with **Use for lookup**;
   verify the matching languages and effective network policy in the dictionary.
6. Remove/disable OCR language data in an isolated test environment. The notebook
   must still open and attempted OCR must report how to install/select data.
7. Test a fresh user profile with QSQLITE and all required runtime plugins
   installed. Do not rely on files in a developer build directory.
8. With desktop motion enabled, hover/select OCR words and resize without moving
   the pointer: highlight and word boxes must stay aligned, including repeated
   identical words. Commit a language change, receive a translation and copy it.
   Results must appear immediately and stay editable; **Copied** must match the
   real desktop clipboard and clear after an edit or a short acknowledgment.
   Toggle reduced motion while feedback is active: the tint must stop immediately
   while selection, **Translation ready**, copy feedback and empty-state actions
   remain usable. Repeat with keyboard navigation and the desktop screen reader.

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
