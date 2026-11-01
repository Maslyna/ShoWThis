# Dictionary lookup and base forms

The word editor has independent choices for:

- **Word language**: the language of the captured/typed word, such as German.
- **Search**: Kaikki, FreeDictionaryAPI, or **Both at once**.
- **Kaikki in**: the Wiktionary edition supplying definitions, such as Russian,
  German, French, or English. This is not the word language or translator target.

FreeDictionaryAPI currently supplies definitions from English Wiktionary; choosing
Russian for Kaikki does not translate FreeDictionary's definitions. Results show
their provider, definition language, and headword explicitly. Kaikki's edition
defaults to the system language if supported and can be changed independently.
Coverage differs between editions and word languages; missing entries are not
silently replaced with English Kaikki results.

## Search modes

- **Kaikki / FreeDictionaryAPI** queries only that source by default.
- **Try the other source if needed** also searches the other provider if the
  preferred source has no full meanings, even after following its form links.
- **Both at once** starts both providers concurrently. A failed provider does not
  discard successful results from the other provider. Transport/parse failures
  are reported; incomplete results are not cached as a successful combined lookup.
- **Search both** in the result pane reruns the current word using both providers.

Changing source/edition/form settings cancels the old requests. Cached queries
and history are separated by word language, word, source mode, definition
language, fallback policy, and form-following policy. History remembers these
settings when reopening a lookup.

## Inflections

With **Look up base forms too** enabled, structured `form_of` / `formOf` links
are followed automatically. FreeDictionary's explicitly tagged definitions, such
as “nominative/accusative/genitive plural of Haus”, are also treated as references.
Ordinary definitions containing “of” are not treated as headword links.

When both sources participate, references can be shared: an English
FreeDictionary inflection entry can identify `Haus`, allowing Kaikki's Russian
edition to supply Russian meanings even if it lacks a separate `Häuser` page.
An initial missing inflection page is not reported as a failure if the headword
was subsequently found in that provider.

Original inflection entries and resolved headword meanings remain separate
results. Clicking a resolved meaning explicitly selects its headword for the
word card. **Look up base form** buttons also allow manually following links
when automatic following is disabled. Multiple candidates are not guessed away;
there is no stemming or statistical disambiguation.

Reference traversal is bounded to two hops and five distinct words per provider,
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
