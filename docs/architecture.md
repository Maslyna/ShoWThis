# Notebook structure

The notebook has no Python runtime or predictive language models. It follows
explicit dictionary links from inflected words to headwords; words can also be
edited normally before lookup or saving.

## UI

- `LearningPanel.qml` owns navigation, confirmations, and the export dialog.
- `NotebookWordEditor.qml` owns the current draft and lookup workflow. Loading a
  card suppresses dirty tracking; opening another card goes through the panel's
  unsaved-change confirmation.
- `NotebookDictionary.qml` displays dictionary results.
- `NotebookDictionaryOptions.qml` shows a compact source summary and a staged
  Apply/Cancel picker. `NotebookDictionarySource.qml` renders each registered
  source's languages and fallback role from metadata.
- `NotebookCardEditor.qml` edits the personal meaning, translation, prefix, and notes.
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
- `DictionaryData` normalizes the providers' formats and explicit form-of links.
- `TranslationService` owns provider selection, configuration, request lifecycle,
  and translated text. It defaults to no translator. Translation fields and
  actions are hidden until the selected provider is configured.
- `LearningStore` owns JSON validation, legacy migrations, history trimming,
  owner-only permissions, and atomic writes. Service state is committed only
  after a successful save.
- `NotebookData` provides dictionary parsing, language-code normalization, and
  the shared article/prefix formatter used by the UI and exports.
- `AnkiExporter` formats cards and validates/writes exports without overwriting
  the notebook.

Existing version-1 notebooks remain compatible. Unknown root and card fields,
including metadata left by the removed analysis feature, are preserved during
saves and edits but are not displayed or included in Anki exports. New cards do
not create analysis metadata. Removing a card still removes the complete card.

## Optional translators

DeepL is an optional implementation of `TranslationProvider`, not part of the
dictionary workflow. Select it in **Settings → Translation** and apply an API
key to enable translation. Credentials are session-only; provider selection and
non-secret options are saved under `translation/`. There is no migration from
the former DeepL-specific settings or environment variable.

To add a translator:

1. Implement `TranslationProvider`: identifier, display name, configuration-field
   descriptors, request construction, and response/error decoding.
2. Mark credential fields as `secret`. Only non-secret fields are persisted or
   returned to QML. Required fields determine whether translation is configured;
   providers that need no credentials can expose no fields.
3. Declare whether the provider supports a separate context input. Context is not
   sent to providers that do not support it; the selected word is not silently
   replaced with a full-sentence request.
4. Register the implementation in `TranslationProviders.cpp` and add its sources
   to `showthis_core`. Generic settings and word-card UI need no changes.

Changing a provider, its settings, credentials, word language, or target language
cancels an outstanding translation and clears its result. Configuring a provider
never sends text: only the explicit Translate action does. Disabling translation
also clears the draft's translation field; saving without a translator uses only
the visible meaning/notes fields. No compatibility layer for saved translations
is provided.

## Build and tests

`showthis_core` and `showthis_ui` are shared internal libraries used by the app
and tests. QML resources are initialized explicitly when linked from the static
UI library.

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build -j 4
ctest --test-dir build --output-on-failure
```

The overlay tests use Qt's offscreen/software backend. Actual compositor screen
capture, global shortcuts, and live network services require manual integration
testing; the automated tests use simulated capture and network replies.
