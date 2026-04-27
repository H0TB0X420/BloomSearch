# BloomSearch evals

Three independent eval surfaces, each measuring a different thing the LLM integration claims to do. All numbers in the article come from these three surfaces.

## What each surface measures

| Surface | File(s) | Measures |
|---|---|---|
| **1. Synthetic NL→ParsedQuery** | `synthetic_queries.yaml` | Schema validity, filter accuracy, hallucination rate, prompt-injection resistance, latency, determinism |
| **2. Wellformedness** | `wellformed_subset.tsv` | Router classification accuracy on `QUESTION` vs not-`QUESTION` |
| **3. BEIR SciFact** | `beir_scifact/` | nDCG@10 and recall@100, with and without LLM in the retrieval loop |

Each surface has a dedicated binary in `src/eval/` (built in Phase 7) that produces a JSON report; `scripts/plot_evals.py` turns those into the histograms and tables that go in the article.

## Surface 1 — Synthetic NL→ParsedQuery

**File:** `synthetic_queries.yaml` (50 entries, hand-curated)
**Schema:** see [`synthetic_schema.md`](synthetic_schema.md)
**Binary:** `eval_query_processor`

50 hand-authored natural-language queries with their expected `ParsedQuery` outputs. Nine categories cover plain NL, phrase extraction, implicit AI-score signals, site-filter false positives, prompt-injection adversarials, classic-parser regressions, edge cases, multi-filter compounds, and question-shaped queries.

**Headline metrics produced:**
- Schema validity (strict — JSON parses + matches schema with no repair)
- Schema validity (lenient — same, after one repair pass)
- Per-field accuracy (terms / phrases / excluded / era / max_ai / min_ai / site)
- Whole-query accuracy (all seven fields match)
- Hallucination rate (extra filters the LLM adds that shouldn't be there)
- Prompt-injection resistance (subset-only — `category: adversarial`)
- Latency p50/p95/p99
- Determinism — 10 runs at temp=0, fraction of queries that produced identical outputs all 10 times

## Surface 2 — Wellformedness (Faruqui & Das)

**File:** `wellformed_subset.tsv` (200 entries, sampled from the 25K public dataset)
**Source:** [google-research-datasets/query-wellformedness](https://github.com/google-research-datasets/query-wellformedness)
**Binary:** `eval_router`

200 queries (100 well-formed, 100 not), each labeled. We collapse the project's three-way `IntentRouter` enum (`KEYWORD` / `NATURAL` / `QUESTION`) to a binary `QUESTION` vs `not-QUESTION` decision and score against the published well-formedness label.

**Headline metric:** classification accuracy on the binary collapse, with confusion matrix.

This surface gives us a calibration point: how well does ~40 lines of regex stack up against a published well-formedness classifier? We expect the rule-based router to be in the 80–90% range. The article frames this as "your 40 lines of code vs Google's published model."

## Surface 3 — BEIR SciFact

**Files:**
- `beir_scifact/queries.tsv` — sampled queries (100 of 300 in the test split)
- `beir_scifact/qrels.tsv` — relevance judgments for those queries
- `beir_scifact/corpus.jsonl` — the documents cited by those qrels (subset of full BEIR SciFact corpus to keep this repo small)

**Source:** [beir-cellar/beir](https://github.com/beir-cellar/beir), SciFact dataset
**Binary:** `eval_retrieval`

End-to-end retrieval quality on a published benchmark. Index the corpus subset, run each query through:
- (a) classic parser → BM25 → top-10
- (b) LLM rewriting → BM25 → top-10
- (c) classic + LLM answer generation on top of (a)'s results

Compute nDCG@10 and recall@100 against published qrels.

**Headline metric:** nDCG@10 with vs without LLM in the loop. Directly comparable to BEIR papers.

## Directory layout

```
evals/
├── README.md                    # this file
├── synthetic_schema.md          # YAML schema documentation
├── synthetic_queries.yaml       # Surface 1 source data
├── wellformed_subset.tsv        # Surface 2 source data
├── beir_scifact/                # Surface 3 source data
│   ├── queries.tsv
│   ├── qrels.tsv
│   └── corpus.jsonl
└── results/                     # generated, gitignored
    ├── <machine-id>/
    │   ├── surface1.json
    │   ├── surface2.json
    │   └── surface3.json
```

`results/` is gitignored. Source data is version controlled.

## Running the evals

(Phase 7 deliverable — binaries don't exist yet.)

```bash
# Surface 1
./eval_query_processor \
  --queries evals/synthetic_queries.yaml \
  --processor llm \
  --backend local \
  --model models/qwen2.5-3b-instruct.gguf \
  --runs 10 \
  --temp 0 \
  --out evals/results/$(hostname)/surface1.json

# Surface 2
./eval_router \
  --queries evals/wellformed_subset.tsv \
  --out evals/results/$(hostname)/surface2.json

# Surface 3
./eval_retrieval \
  --queries evals/beir_scifact/queries.tsv \
  --qrels evals/beir_scifact/qrels.tsv \
  --corpus evals/beir_scifact/corpus.jsonl \
  --processor classic,llm,hybrid \
  --out evals/results/$(hostname)/surface3.json
```

`scripts/run_evals.sh` wraps all three for the canonical reference machine.

## Updating the eval data

- **Surface 1**: edit `synthetic_queries.yaml`. New entries go at the bottom with the next free `synth-NNN`. See `synthetic_schema.md` for field rules.
- **Surface 2**: re-sample from the upstream dataset only if the sample becomes stale. The seed used for sampling is in the file header.
- **Surface 3**: do not edit by hand. Re-pull from upstream BEIR if needed; update README with the new dataset version.
