# Notebook structure

The notebook has no Python runtime, language models, or lemma-analysis feature.
Words can still be edited normally before dictionary lookup or saving.

## UI

- `LearningPanel.qml` owns navigation, confirmations, and the export dialog.
- `NotebookWordEditor.qml` owns the current draft and lookup workflow. Loading a
  card suppresses dirty tracking; opening another card goes through the panel's
  unsaved-change confirmation.
- `NotebookDictionary.qml` displays dictionary results.
- `NotebookCardEditor.qml` edits the personal meaning, translation, prefix, and notes.
- `NotebookVocabulary.qml` owns saved-word search and export selection.
- `NotebookHistory.qml` displays and searches recent lookups.
- `NotebookSettings.qml` contains history, translation, and storage settings.

Components use explicit properties and signals rather than accessing another
component's internal QML IDs. `ShoWSurface` accepts an optional `LearningService`
for tests; the caller retains ownership of an injected service.

## Core

- `LearningService` exposes notebook operations and observable state to QML and
  coordinates dictionary/translation requests.
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
