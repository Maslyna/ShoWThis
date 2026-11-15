# Notebook structure

The notebook has no Python runtime or predictive language models. It follows
explicit dictionary links from inflected words to headwords; words can also be
edited normally before lookup or saving.

## UI

- `Overlay.qml` hosts notebook and translator dialogs in the existing layer-shell
  surface. `OverlayDialog.qml` supplies bounded dragging, resizing, focus and
  stacking without creating native windows. `NotebookDialog.qml` hosts
  `LearningPanel.qml`, which owns navigation and confirmations; its header contains
  New word and the standalone Translator action.
- `NotebookWordEditor.qml` owns the current draft and lookup workflow, including
  a snapshot of the chosen dictionary sense and its attribution. Saving passes
  this snapshot, not an index into mutable lookup results. Refreshing or changing
  sources preserves it; editing the word or its language clears it with the copied
  fields. Loading a card suppresses dirty tracking; opening another card goes
  through the panel's unsaved-change confirmation.
  Context is editable immediately under the word, with explicit translator launch
  buttons beside both inputs. The desktop workspace is a resizable `SplitView`;
  compact screens show Dictionary/Card tabs. There is no context drawer or
  page-level scroller around both panes.
- `TranslatorDialog.qml` is an independent, nonmodal item in the same overlay.
  Its controls and dialog chrome use the system palette. Its source/target
  language settings remain under `translator_window/`, separately from
  dictionary/card preferences. Word/context actions seed its input without
  networking; a separate Translate action sends. Results never auto-apply.
  Explicit append actions target the currently selected draft through
  `ShoWSurface` and mark it dirty without saving unrelated edits. Reopening
  without seed text preserves its input/results and manually adjusted geometry.
- `SearchableComboBox.qml` keeps stable source-model indices while filtering a
  virtualized popup. Typing searches display names and values; arrows preview,
  Enter commits, and Escape cancels before dialog shortcuts. The language picker
  additionally accepts valid custom language-code syntax.
- `ContextSelection.qml` owns temporary manual context selection over the current
  screenshot. Rectangle drags add words by box centre; clicks toggle individual
  words. Word indices prevent overlap duplicates without removing repeated text
  at distinct positions. First-selection groups preserve OCR order within each
  rectangle and selection order between rectangles; re-including a word restores
  its original group position. A movable panel previews the combined text.
  `Overlay.qml` temporarily hides both dialogs during selection and restores
  their visibility/focus afterward. Only acceptance updates the notebook context.
  Replacing the image/words, starting OCR, or hiding the window cancels selection;
  resizing preserves committed selections but abandons an in-flight drag.
- `NotebookDictionary.qml` displays dictionary results and owns source selection
  within its pane rather than consuming space in the word toolbar.
- Anki export uses Qt's standard `QFileDialog`, with native dialogs enabled.
  The overlay temporarily hides to release its exclusive keyboard grab while the
  application-modal save dialog is open. Capture shortcuts respect that dialog;
  closing it restores the latest requested overlay state. There is no custom browser.
- `NotebookDictionaryOptions.qml` shows a compact source summary and a staged
  Apply/Cancel picker. `NotebookDictionarySource.qml` renders each registered
  source's languages and fallback role from metadata.
- `NotebookCardEditor.qml` edits meaning, manual word/context translations, prefix,
  forms and notes. Translation language selection and machine results are confined
  to the independent translator; only explicit Add actions copy machine results into a card.
- `NotebookVocabulary.qml` owns saved-word search and export selection.
- `NotebookHistory.qml` displays and searches recent lookups.
- `NotebookSettings.qml` contains history, translation, and storage settings.
- `NotebookTranslatorSettings.qml` renders provider configuration from field
  descriptors; it has no provider-specific controls.

Components use explicit properties and signals rather than accessing another
component's internal QML IDs. `ShoWSurface` accepts an optional `LearningService`
for tests; the caller retains ownership of an injected service.

## Core

- `LearningService` exposes notebook operations and observable state to QML and
  coordinates dictionary requests and optional translation.
- `DictionaryLookup` schedules registered source/language pairs concurrently,
  follows bounded dictionary references, and cancels obsolete requests.
- `DictionaryProvider` owns source metadata and the request/response protocol.
  Kaikki and FreeDictionaryAPI are registered in `DictionaryProviders.cpp`;
  neither the scheduler nor QML needs provider-name branches.
  Providers initialize and own their optional local state, expose settings
  component/state metadata, and calculate revisions for a selected word/definition
  language pair. The scheduler subscribes to the generic state notifier and
  supplies a shared cancellation token to local jobs. Kaikki's worker checks it
  between redirect hops and result records. The legacy typed offline accessor
  remains only for compatibility; `LearningService` no longer creates the store.
- `DictionaryData` normalizes the providers' formats and explicit form-of links.
- `KaikkiOfflineStore` manages opt-in gzip downloads, background SQLite indexing,
  word-language filtering, raw redirect records, atomic pack updates,
  cancellation, and removal. `NotebookOfflineDictionaries`
  exposes download confirmation and local/online policy in Settings. Providers
  can expose local lookup jobs; the generic scheduler runs them in workers and
  falls back according to each provider's access policy.
- `GrammarProfile` interprets normalized tags using bundled declarative profiles
  in `data/grammar-profiles.json`. Rules depend on word language and part of
  speech, never on the definition/translation language or dictionary provider.
  Article suggestions retain their dictionary/profile/manual origin; number
  availability distinguishes explicit absence from missing data.
- `TranslationService` owns provider selection, configuration, request lifecycle,
  and translated text. It defaults to no translator. Only the online Translate
  action is hidden until the selected provider is configured; personal and
  dictionary-supplied translations remain editable without a provider.
- `LearningStore` owns JSON validation, legacy migrations, history trimming,
  owner-only permissions, and atomic vocabulary writes. Vocabulary state is
  committed only after a successful save. Cards require unique non-empty IDs and non-empty
  words; known optional fields are type-checked before migration or writing.
  A malformed card makes the notebook read-only rather than silently repairing
  or dropping data. Missing optional legacy fields and unknown metadata remain
  supported, including the null grammar profile written by older manual cards.
- `LookupCacheStore` separately persists disposable history/results to an
  owner-only sidecar. Snapshots are coalesced and serialized/written by a worker;
  shutdown flushes the latest snapshot. Cache failures do not block vocabulary
  saves, except when flushing is needed to safely migrate an embedded legacy
  cache. Explicit history clearing/limit changes wait for successful persistence.
  Clear history remains available with empty history so an unreadable sidecar can
  be reset explicitly, including before a blocked legacy migration.
  Limits are 16 MiB total and 1 MiB per result, in addition to the entry count.
- `NotebookListModel` exposes typed identity/word roles with precomputed C++ search
  text for vocabulary and history. QML no longer copies and filters full arrays
  on every keystroke. `CardForms` centralizes validation in typed number-status
  and article-origin structures; maps remain the legacy/extension boundary.
- `NotebookData` provides dictionary parsing, language-code normalization, and
  the shared article/prefix formatter used by the UI and exports.
- `AnkiExporter` formats cards and validates/writes exports without overwriting
  the notebook.

Existing version-1 notebooks remain compatible. Unknown root and card fields,
including metadata left by the removed analysis feature, are preserved during
saves and edits but are not displayed or included in Anki exports. New cards do
not create analysis metadata. Removing a card still removes the complete card.

Vocabulary JSON saves are synchronous and capped at 16 MiB, but lookups never
rewrite that file. Legacy input is capped at 64 MiB and cache input at 32 MiB
before `readAll()`. Legacy embedded caches remain intact until a sidecar has
been successfully written. Backups of the vocabulary alone retain personal
cards, but not disposable lookup history.

Opening saved cards/history applies a transient draft configuration rather than
writing global defaults. A new draft restores persisted defaults; explicit
choices on a new draft may update them. Cache keys use only selected providers'
relevant data revisions. Online results expire after seven days, with an
explicit Refresh action and stale-result fallback after failed refreshes.
History opening uses its exact stored key and never silently starts networking.

## Optional translators

DeepL and LibreTranslate implement `TranslationProvider`, independently of the
dictionary workflow. Choose either in **Settings → Translation**; no translator
is selected by default. DeepL requires an API key. LibreTranslate defaults to
`http://127.0.0.1:5000` and has an optional API key for servers that require one.
Its base URL supports hosting prefixes; the provider appends `/translate`.
The separately installed LibreTranslate service owns its runtime and models;
ShoWThis does not start it, download models, or add an embedded Python runtime.
Word/context card jobs and the independent session have separate cancellation,
results and errors. Notebook draft/language changes clear only card jobs; they
cannot interrupt the standalone session or block saving. Provider/configuration/
credential changes clear every job. The session sends its primary text with empty
auxiliary context for every provider. The optional `context_translation` card
field remains validated, persisted, searchable and exported.
URL options reject credentials, queries, fragments, non-HTTP(S)
schemes, and malformed addresses before persistence. Same-origin redirect policy
prevents forwarding the word or optional key to another server.

Credentials are session-only; provider selection and non-secret options are
saved under `translation/`. Changing providers preserves each provider's settings
within the session. There is no migration from the former DeepL-specific settings
or environment variable.

To add a translator:

1. Implement `TranslationProvider`: identifier, display name, configuration-field
   descriptors, request construction, and response/error decoding.
2. Mark credential fields as `secret`. Only non-secret fields are persisted or
   returned to QML. Required fields determine whether translation is configured;
   providers that need no credentials can expose no fields. Override `option_error`
   to validate provider-specific option values before saving them and when checking
   readiness after loading settings. Optional field descriptions appear as help
   text in the generic settings editor.
3. Declare whether the provider supports a separate context input. Context is not
   sent to providers that do not support it; the selected word is not silently
   replaced with a full-sentence request.
4. Register the implementation in `TranslationProviders.cpp` and add its sources
   to `showthis_core`. Generic settings and word-card UI need no changes.

Changing provider settings or credentials cancels outstanding jobs. Changing the
translator dialog's source or target language cancels its session; editing or
switching notebook cards does not. Configuring or opening a translator never sends
text: only Translate does. Personal translations survive provider changes and
restarts. Appending translated text explicitly updates the card target language;
source/context/notes additions do not. The original input and previous field text
remain intact.

## Build and tests

`showthis_core`, `showthis_ui`, `showthis_ocr`, and `showthis_desktop` are internal libraries
used by the app and tests. QML resources are initialized explicitly when linked
from the static UI library.

OCR language discovery initializes only Tesseract's data path, not an English
recognition model. Selected models are loaded in the OCR worker on demand;
missing or unreadable models are reported as recoverable scan errors and do not
prevent opening the notebook. Pixel conversion explicitly packs Leptonica RGB
values rather than depending on the host's byte order. `OcrEngineTest` checks
pixel channels, discovery without English, and missing models; an additional
real-model smoke test runs when non-English tessdata is installed.

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build -j 4
ctest --test-dir build --output-on-failure
```

Offline Kaikki additionally uses Qt6 Sql, its QSQLITE driver plugin, and zlib.
The offline tests use tiny generated gzip dictionaries and simulated network
errors; they do not download full editions.

The overlay tests use Qt's offscreen/software backend. Actual compositor screen
capture, global shortcuts, and live network services require manual integration
testing. `DesktopIntegrationTest` exercises actual private-D-Bus messages and
Unix pipes without contacting the user's compositor. `NotebookArchitectureTest`
covers cache limits, separate persistence, refresh/expiry, provider-scoped
revisions, offline history, transient drafts, a 10,000-entry model, typed forms,
and cooperative local-job cancellation. See [desktop-testing.md](desktop-testing.md)
for the remaining manual KDE checks. CI also configures an ASan/UBSan build.
