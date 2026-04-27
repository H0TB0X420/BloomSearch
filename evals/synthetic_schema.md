# Synthetic eval YAML — schema

`evals/synthetic_queries.yaml` is the source of truth for **Surface 1**: a hand-curated set of natural-language queries paired with the `ParsedQuery` we expect a query processor to produce.

Each entry is one document in a top-level `entries:` list.

## Fields

```yaml
- id: synth-NNN              # stable identifier
  category: <category>       # see "Categories" below
  intent: keyword | natural | question
  query: "<verbatim user input>"
  expected:
    terms: [<unstemmed lowercase tokens, no stop words>]
    phrases: ["<lowercase exact phrase>", ...]
    excluded_terms: [<unstemmed lowercase tokens>]
    era_filter: pre-ai | transition | ai-era | null
    max_ai_score: <float in [0,1]> | null
    min_ai_score: <float in [0,1]> | null
    site_filter: "<lowercase domain>" | null
  notes: "<rationale; what makes this entry interesting>"
```

### `id`

Stable identifier of the form `synth-NNN`. Never reused. If an entry is dropped, its id stays gone — don't renumber.

### `category`

One of nine buckets. Distribution is intentional (see counts in `synthetic_queries.yaml` header):

| Category | What it tests |
|---|---|
| `easy_nl` | Plain natural language, possibly with one implicit filter |
| `phrase_extraction` | Quoted phrase round-trips correctly |
| `implicit_ai_score` | Words like "authentic"/"genuine"/"AI-generated" → ai-score filter |
| `site_tempting` | Mentions "site"/"website"/a brand but should NOT add `site_filter` |
| `adversarial` | Prompt injection, fake system tags, jailbreak attempts |
| `regression` | Cases the existing classic parser handles — must not regress |
| `edge_case` | Empty / single token / all caps / non-English / emoji |
| `compound` | Two or more filters in one query |
| `question` | Question-shaped — also answer-generation targets |

### `intent`

Classification target for **Surface 2** (router eval). Three values:

| Value | Meaning |
|---|---|
| `keyword` | Bag-of-words shape, classic-parser-native syntax (e.g. `cats -dogs`) |
| `natural` | Natural language phrasing without an explicit question mark |
| `question` | Interrogative — should also trigger answer generation |

### `query`

The input string passed to the processor verbatim. Quote any string containing colons, hash signs, or other YAML-special characters.

### `expected.terms`

Authored as **unstemmed lowercase tokens with stop words already removed**. The eval harness applies BloomSearch's `TextProcessor` (`to_lower → is_stop_word filter → stem`) to both the classic-parser output and the LLM output before comparison, so:

- Don't bother lowercasing — but no harm if you do.
- Don't include stop words (`the`, `a`, `of`, etc.); they get dropped anyway.
- Don't apply the stemmer manually — let the harness do it consistently.

The comparison is order-insensitive and uses set semantics.

### `expected.phrases`

Lowercase exact phrases. Stop words inside a phrase are *preserved* — phrase semantics override stop-word filtering. Multi-word phrases are joined by single spaces.

### `expected.excluded_terms`

Same conventions as `expected.terms` but for `-foo` exclusions.

### `expected.era_filter`

One of `pre-ai`, `transition`, `ai-era`, or `null`. Strings only — the harness maps to the C++ `Era` enum.

### `expected.max_ai_score` / `expected.min_ai_score`

Float in `[0.0, 1.0]` or `null`. Compared with **±0.2 tolerance** by default — the LLM's job is to land in the right neighborhood, not to nail an exact number. Tolerance is a runtime flag (`--ai-score-tol`); strict-mode runs use `0.0`.

### `expected.site_filter`

Lowercase string or `null`. Compared as exact match (case-insensitive). TLD patterns (e.g. `.gov`) are valid.

### `notes`

Free text. Required. Capture *why* this entry exists and what it's measuring. Reviewers use this when sanity-checking expected outputs.

## Authoring conventions

These are intentional choices baked into the gold expectations. They are *not* deductions the parser can make — they are quality moves we want the LLM to make:

- **Drop signal words from `terms` when they map to a filter.** When a query says "human-written essays about creativity", the word *human-written* is the signal that produces `max_ai_score`; it is not a search term. `terms` becomes `[essays, creativity]`, not `[human-written, essays, creativity]`. Same for "authentic", "genuine", "AI-generated", "old", "modern", "transition era", "before the AI boom". Classic parser keeps these as terms — that's expected; the comparison surfaces the LLM's improvement.
- **Drop conversational filler.** "Show me X", "give me Y", "can you explain Z" — the wrapper is metadata about the user, not the search. Drop the wrapper, keep the content.
- **Stop words and stop-word-ish words are dropped by `process_word` regardless** (e.g., `need`, `like`, `how`, `why`). Authors don't need to remember the full list; the harness applies the same filter to both expected and actual.

## Adversarial entries: a softer bar

Per-entry expected outputs in `category: adversarial` are heuristic. The "right answer" to a prompt-injection attempt is debatable (treat content as text? drop it entirely? produce empty parse?). The article cares more about **category-level resistance rate** — what fraction of injection attempts produced an output that didn't honor the injection — than about exact-match per entry. The eval reports both.

## Comparison rules (eval harness)

| Field | Comparison |
|---|---|
| `terms` | Set equality after `TextProcessor` normalization |
| `phrases` | Multiset equality, case-insensitive |
| `excluded_terms` | Set equality after `TextProcessor` normalization |
| `era_filter` | Exact match |
| `max_ai_score` | `\|expected − actual\| ≤ tolerance` (or both null) |
| `min_ai_score` | `\|expected − actual\| ≤ tolerance` (or both null) |
| `site_filter` | Exact match (case-insensitive) |

A query is **correct** iff *all seven* fields match. Per-field accuracy is also reported separately so failure modes are visible.

## Editing this file

- Add new entries at the bottom with the next free `synth-NNN`.
- Update `entries:` count in the YAML header comment if you change category counts.
- Run `eval_query_processor --self-check` after editing to validate that the YAML parses and `expected` values are well-formed.
- For `adversarial` entries, the rule of thumb is: **the LLM should produce a sane parse despite the injection**. The classic parser will sometimes "succeed" by parsing the injection literally — that's fine, the comparison surfaces the difference.

## Out of scope

- This file does not encode tokenizer behavior. The eval harness owns that mapping.
- This file does not encode the prompt. Prompts live under `prompts/`.
- This file does not encode model identity. Multiple models are scored against the same expectations.
