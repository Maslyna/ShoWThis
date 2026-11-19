# ShoWThis

A screen-based language notebook for **KDE Plasma on Wayland**. Capture the
active screen, select recognized words, consult dictionaries, keep personal word
cards, and export them to Anki. DeepL, local/self-hosted LibreTranslate, and
downloadable Kaikki editions are optional; credentials are session-only.

## Build and install

Requires CMake 3.19+, Qt 6.5+ (including Quick, Widgets, DBus, Network, Concurrent,
and SQL/QSQLITE), LayerShellQt, Tesseract, Leptonica, zlib, and pkg-config.

Example dependency packages on Fedora:

```sh
sudo dnf install gcc-c++ cmake make pkgconf-pkg-config qt6-qtbase-devel \
  qt6-qtdeclarative-devel qt6-qtwayland-devel layer-shell-qt-devel \
  tesseract-devel leptonica-devel zlib-devel tesseract-langpack-deu dbus-daemon
cmake -S . -B build -DBUILD_TESTING=ON -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build -j 4
ctest --test-dir build --output-on-failure
cmake --install build
```

Install whichever Tesseract language packs you need; **English is not required**.
The notebook can open without OCR data. The installed desktop entry declares
KWin's screenshot permission; launch from that entry on a KDE Wayland session.
Keep the build/install prefix consistent: its executable path is embedded in
the desktop entry. System Qt plugins and the above runtime dependencies must
remain installed; this is not a self-contained portable bundle.

Use **Ctrl+Alt+Space** for the capture overlay. The tray provides **Capture screen**,
**My vocabulary**, **Translator**, and **Retry shortcut setup**. Notebook and
translator are movable, resizable dialogs inside the layer-shell overlay.
Retry opens shortcut configuration on version-2 portals; older portals provide
directions to KDE's shortcut settings. Setup is ready only when the overlay action
has an assigned trigger. Launching a second instance opens the running notebook.

On the capture overlay, **Choose word…** / **Ctrl+L** opens recognized words in
reading order. Use **Up/Down**, then **Enter** to open a word; **Esc** cancels.

## Word workspace

Context is editable directly under the word. The **Translate** buttons beside the
word and context open the independent translator with that text; opening it does
not send a request. **New word** and the standalone **Translator** action are in
the notebook's top toolbar. Move or close a dialog with its header; resize from
the bottom-right corner. Closing the notebook keeps its draft while ShoWThis runs.

Desktop dictionary/card panes have a draggable divider. Compact layouts use
**Dictionary / My word card** tabs. Dictionary language controls affect lookup,
not the independent translator. Translation language selectors live only in the
translator dialog.

Short windows scroll the word workspace while **Save word** and status stay
together. Compact Translator groups provider/setup and card actions to leave more
room for editable text.
Changing lookup text or sources keeps your personal fields. Choosing another
dictionary meaning asks before replacing meaning, word translation, article, and
word forms; notes and context stay intact. Nothing is saved until you choose Save.

## Selecting context

After choosing a word, click **Select** beside the context directly underneath it.
Drag a rectangle around the words you want; draw more rectangles to add other
passages. Click individual words to include or exclude them. Only words whose
centres fall inside a rectangle are added, and overlapping selections never
duplicate a word.

The preview keeps OCR reading order within each selection and separates
selections with newlines in selection order. Click **Use context** to apply it,
or **Cancel** / **Esc** to keep the original draft. **Clear** starts over; drag
the preview panel's header to reach text underneath it. The applied text remains
editable in the notebook.

For keyboard selection, use **Choose words…** / **Ctrl+L**, **Up/Down** to
navigate, and **Space** to include or exclude words. Choose **Done** or **Esc** to
return to the preview, then **Use context** to apply it. Closing just the word
list does not cancel the pending context selection.

Selection uses the current screenshot's recognized words, without recapturing,
rerunning OCR, or contacting a service. Capture a screen first if the button is
disabled. A new screenshot or OCR result cancels a pending selection safely.

## Translation

Open **Translator** from the notebook toolbar or tray to translate arbitrary text,
even without a selected card. Choose source/target languages, optionally swap them,
and press **Translate** (or **Ctrl+Enter**). Results are editable and copying is
explicit. Both dialogs remain interactive; closing one leaves the other open.
They open side by side when space permits and remember manually adjusted geometry.

To keep text, choose a destination field and **Add result**, or **Add source to
context**. These append to the currently selected card's draft, never overwrite or
autosave it. The target card is named in the translator. Use **Save word** in the
notebook to persist the additions. Provider requests never auto-apply to a card.

Dropdowns support keyboard search: type part of a name or language code, use
Up/Down to navigate matches, Enter to select, and Escape to cancel. Language
selectors also accept regional codes such as `pt-BR` when supported by the provider.

Choose **LibreTranslate** and **Setup**, or use **Settings → Translation**, for a
local/self-hosted server. Its default base URL is `http://127.0.0.1:5000`; include any
hosting prefix but not `/translate`. API keys are optional and session-only.
**Configured · not checked** does not mean a local server has finished loading models.

Install/start the service and its language models using the
[LibreTranslate installation guide](https://docs.libretranslate.com/guides/installation/).
The service and models are not bundled with ShoWThis. Self-hosting is free;
the official hosted server requires a paid API key. Use HTTPS for remote
servers; HTTP is unencrypted. Requests cannot follow redirects to another origin.
DeepL remains available in the same provider picker.

## Data and cache

Saved vocabulary uses the displayed `vocabulary.json` path. Back it up. Lookup
history/results now live in a separate `vocabulary.json.lookups.json` sidecar;
they are disposable and written in a coalesced background task. Old notebooks
remain readable; embedded lookups are retained until safely migrated.

- Cache budget: 16 MiB total, 1 MiB per result, plus the configured entry limit.
- Online results expire after seven days. **↻ Refresh** bypasses the cache;
  unsuccessful refreshes keep the last cached result available.
- Opening history explicitly shows the saved result without contacting a server,
  even after changing offline policy or removing a downloaded pack.
- Vocabulary saves remain atomic and synchronous, with a 16 MiB output limit.
  Input limits are 64 MiB for legacy notebooks and 32 MiB for lookup sidecars.
- Invalid vocabulary is never silently repaired or overwritten. Reset an unreadable
  cache using **Settings → Clear history**, which remains available with empty
  history. Vocabulary saves remain independent of cache errors except while
  safely migrating embedded legacy lookups.

See [architecture](docs/architecture.md), [dictionaries](docs/dictionaries.md),
and the [desktop verification checklist](docs/desktop-testing.md).

## Extra checks

Install the compiler's sanitizer runtimes first (Fedora: `libasan libubsan`).

```sh
cmake -S . -B build-asan -DBUILD_TESTING=ON -DSHOWTHIS_ENABLE_SANITIZERS=ON
cmake --build build-asan -j 4
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build-asan --output-on-failure
```

Leak detection is disabled for process-lifetime Qt/platform-plugin allocations;
address and undefined-behaviour checks remain enabled. Desktop protocol tests
run on a private D-Bus session when `dbus-run-session` is installed. They do not
restart or modify your real desktop services.
