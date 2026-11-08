# Dictionary lookup and base forms

The word editor has independent choices for:

- **Word language**: the language of the captured/typed word, such as German.
- **Sources & languages…**: select any number of dictionary sources and one or
  more definition languages for each. For example, select Russian and German
  from Kaikki alongside English from FreeDictionaryAPI.

Regional or script-qualified word-language codes such as `de-DE` and `pt-BR`
use the dictionary's base language (`de`, `pt`), including offline packs and
cache keys. Translator target variants remain independent and are retained.

The editor shows a compact summary of the active sources and languages. The
picker stages edits until **Apply**; **Cancel** or Esc discards them. Applying
settings cancels outdated results but preserves typed word-card fields. Lookup
is still an explicit action; editing source preferences sends no text.

FreeDictionaryAPI currently supplies definitions from English Wiktionary; choosing
Russian for Kaikki does not translate FreeDictionary's definitions. Results show
their provider, definition language, and headword explicitly. When enabling a
source, its initial language is the system language if supported, otherwise the
source's first language. Multiple languages can then be selected independently.
Coverage differs between editions and word languages; missing entries are not
silently replaced with English Kaikki results.

## Parallel and fallback sources

- All selected primary source/language pairs start concurrently.
- **Use only as fallback** defers a source's selected languages until the primary
  sources have no full meanings, even after following their form links.
  Previously discovered base-form links are replayed into newly started fallback
  sources, with the same traversal-depth, word-count, and cycle limits.
- Only explicitly selected sources are contacted. There is no implicit “other
  source” and no hardcoded two-provider search mode.
- A failed pair does not discard successful results from other pairs.
  Transport/parse failures are reported; incomplete results are not cached.

Changing source/edition/form settings cancels the old requests. Cached queries
and history are separated by word language, word, the complete source/language
selection, fallback roles, and form-following policy. Selection order does not
change the cache key. History and saved cards retain the lookup configuration.
The former fixed-mode settings are not migrated.

## Inflections

With **Look up base forms too** enabled, structured `form_of` / `formOf` links
are followed automatically. FreeDictionary's explicitly tagged definitions, such
as “nominative/accusative/genitive plural of Haus”, are also treated as references.
Ordinary definitions containing “of” are not treated as headword links.

When several source/language pairs participate, references can be shared: an English
FreeDictionary inflection entry can identify `Haus`, allowing Kaikki's Russian
edition to supply Russian meanings even if it lacks a separate `Häuser` page.
An initial missing inflection page is not reported as a failure if the headword
was subsequently found in that provider.

Original inflection entries and resolved headword meanings remain separate
results. Clicking a resolved meaning explicitly selects its headword for the
word card. **Look up base form** buttons also allow manually following links
when automatic following is disabled. Multiple candidates are not guessed away;
there is no stemming or statistical disambiguation.

### Forms on saved cards

Selecting a meaning fills the card's **Word forms** section with the dictionary
headword (or its explicit base-form link), citation plural forms, and the base
article/prefix when supplied or supported by a language profile. For example,
German Kaikki's `Haus` entry supplies **das Haus** and **Häuser**. Oblique forms
such as `Häusern` are not used as the citation plural.

An inflection can inherit this metadata from its fetched headword in the same
source, definition language, and part of speech. Unavailable or ambiguous values
stay blank. The fields are editable; multiple plurals can be separated with `/`.
They are retained when reopening a saved card, shown in the vocabulary list,
and included in Anki exports. Hiding **Word forms** does not erase the values.

### Language profiles and number availability

English and German noun profiles are currently bundled in
`data/grammar-profiles.json`. These are word-language rules, not fixed learning
pairs: Russian/Ukrainian definitions or translator targets do not change them,
and other word and definition languages remain selectable.

- **German nouns:** known dictionary gender suggests the nominative definite
  article (`der`, `die`, `das`); known plural nouns use `die`. No gender is guessed
  from spelling. Ambiguous gender is left for the user. Explicit dictionary
  articles take precedence. Suggestions are identified in the editor and can be
  corrected or removed; manual edits survive saving/reopening.
- **English nouns:** no automatic `the` or `a/an`. A missing plural is not
  generated by appending `s`.
- **No singular / No plural** means an unqualified dictionary restriction such
  as plural-only, singular-only, uncountable, or an explicit missing-form marker.
  These restrictions apply to the selected sense: a shared entry-level table
  for a different countable sense does not override them. For example, `Eltern`
  is plural-only, while an uncountable sense of `advice` has no plural.
- **Singular/Plural not listed** means insufficient dictionary data, not that
  the form cannot exist. `countable + uncountable` and qualifiers such as
  `usually` do not establish an absolute absence.

The card stores singular forms, number availability, and article origins, and
the vocabulary list and Anki export include explicit absence/missing-data labels.
Number fields apply to nouns rather than presenting verb agreement as noun
plurals. Other languages use a conservative generic fallback: explicit forms
and restrictions are retained, but there is no invented language-specific article.

To add a similarly structured language, add its part-of-speech rules to the JSON
profile data. Provider implementations and QML need no language-name branches.
Tests inject an additional profile to exercise this extension point.

Reference traversal is bounded to two hops and five distinct words per source/language pair,
with cycle detection. Word forms which have neither a dictionary entry nor a
discoverable form-of reference can still require manual correction.

## Kaikki transport

Kaikki is a dataset publisher, not a conventional dictionary REST API. ShoWThis
uses its published, edition-specific static search indexes to discover exact
word pages, then reads the corresponding per-word JSONL files. No complete
dictionary download, Python runtime, or NLP model is required.

Only HTTPS links on `kaikki.org` within the selected edition's published word
tree are followed. Responses have timeouts and an 8 MiB limit. The static site
layout may change; failures are surfaced rather than scraping arbitrary HTML.

See [Kaikki](https://kaikki.org/) and
[FreeDictionaryAPI](https://freedictionaryapi.com/) for availability and usage
limits. Normalized results and saved cards retain per-sense Wiktionary URLs,
provider identity, and licence information; Anki exports use the selected
meaning's actual provider attribution.

## Downloaded Kaikki editions

In **Settings → Offline dictionaries**:

1. Choose the **word language** and **definitions in** language, then **Check
   size**. For example, **German → Русский definitions** means German words
   explained in Russian; **English → Русский definitions** is a separate pack.
2. Select **Download / update…** and confirm the transfer and disk-space warning.
3. Enable Kaikki and the matching definition language in **Sources & languages…**.
4. Choose how Kaikki uses its downloaded data:
   - **When online lookup cannot connect**: try the online service first, then
      downloaded data after a connection failure, timeout, rate limit, or server
      failure. A normal online “entry not found” does not trigger this fallback.
      Connection failures after receiving HTTP headers also trigger fallback;
      malformed dictionary responses and explicit cancellation do not.
   - **Prefer downloaded data**: search the local index first; use online lookup
     if the local entry is missing or unreadable.
   - **Downloaded data only**: do not send Kaikki lookup requests, even if an
     edition or word is missing. This avoids waiting for an online timeout.
   - **Online only**: retain installed editions but do not query them.

This is about internet access, whether wired or Wi-Fi—not specifically the
presence of an Ethernet adapter. Other selected providers can still send online
requests. For entirely local dictionary lookup, select only Kaikki and choose
**Downloaded data only**. Translation remains a separate online operation.

Each transfer is the full compressed raw Wiktextract edition, but the local
index keeps only the chosen word language. **All word languages** can be selected
explicitly to keep the complete edition and avoid downloading the same edition
again for another word language. Existing all-language packs remain usable.
The app uses the published
`raw-wiktextract-data.jsonl.gz` files rather than deprecated postprocessed bulk
files. Editions vary greatly in size; the English edition can require several
GB to download and significantly more free disk space to install. The size
check is only a HEAD request; no bulk download happens without confirmation.

Downloads stream to temporary files. Gzip decompression and SQLite indexing run
in a worker thread with bounded memory. Records are compressed individually;
lookups use word-language/exact-word indexes with a Unicode case-insensitive
fallback. There is no full dataset scan on each lookup. Downloaded entries use
the same normalization, grammar profiles, form-following, and attribution as
online entries, and results are marked **offline**.

Raw Wiktextract files also contain `hard-redirect` records with `title` and
`redirect` instead of `word` and `lang_code`. These are indexed as redirects,
not rejected as invalid JSONL; redirect traversal is bounded and cycle-checked.
Unknown valid non-entry objects are counted and skipped. Actual JSON syntax
errors report the line number and byte offset, while missing coverage reports
which word language had no entries. Both leave existing packs intact.

The settings page shows progress, cancellation, installed sizes, and the storage
directory (`kaikki/` beside the notebook by default). A new edition is published
atomically only after the gzip stream, records, and index have been validated.
Failed or cancelled updates leave the previous edition usable. Temporary
downloads are removed after completion/cancellation; only the indexed database
is retained. Removing an edition does not remove saved cards or lookup history.
Installed revisions and access policy separate lookup cache keys so an update
does not silently keep returning results from an older pack.

Installation currently requires Qt SQL's **QSQLITE** driver and zlib. Safety
limits are 32 GiB compressed per download, 128 GiB expanded per edition, and
8 MiB per record/lookup response. Downloads are opt-in and are never scheduled
automatically by looking up a word. Publisher URLs or schemas may change;
installation errors are surfaced without replacing a working edition.

## Adding a dictionary source

1. Implement `DictionaryProvider`: stable ID, name, description, website,
   supported definition languages, request construction, and response decoding.
2. Return normalized dictionary documents with entries, word-language codes,
   senses, and per-entry attribution. A response can return a next request for
   multi-stage protocols, as Kaikki does. The scheduler bounds request stages.
3. Register it in `default_dictionary_providers()` in
   `src/DictionaryProviders.cpp` and add the implementation to `showthis_core`.

No picker or scheduler changes are needed. The UI lists registered providers and
their languages automatically; configuration is validated atomically and capped
at 32 source/language pairs. Tests inject a third provider with two languages to
exercise this without adding another production network service.

Providers with local datasets can additionally expose an access policy and a
thread-safe local lookup job. The generic scheduler runs those jobs off the UI
thread, ignores cancelled results, and applies the same form-following rules.
