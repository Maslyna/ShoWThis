# Dictionary lookup and base forms

The word editor has independent choices for:

- **Word language**: the language of the captured/typed word, such as German.
- **Sources & languages…**: select any number of dictionary sources and one or
  more definition languages for each. For example, select Russian and German
  from Kaikki alongside English from FreeDictionaryAPI.

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
