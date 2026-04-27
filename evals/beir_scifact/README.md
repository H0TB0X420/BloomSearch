# BEIR SciFact subset (Surface 3)

Source: [BEIR SciFact](https://public.ukp.informatik.tu-darmstadt.de/thakur/BEIR/datasets/scifact.zip), packaged by [beir-cellar/beir](https://github.com/beir-cellar/beir).

## Files

| File | Source | Notes |
|---|---|---|
| `queries.tsv` | sampled from upstream `queries.jsonl` ∩ `qrels/test.tsv` | 100 query-ids, seed=42 |
| `qrels.tsv` | filtered upstream `qrels/test.tsv` | judgments for the sampled 100 queries only |
| `corpus.jsonl` | upstream `corpus.jsonl` (full) | full SciFact corpus — required for honest recall@100 |

## Sampling rule

```
seed = 42
unique_qids = sorted(set(query-id for q,_,_ in test_qrels))   # 300 ids
sampled    = random.sample(unique_qids, 100)
```

Sampling is on `query-id` (not on (query, doc) pairs), so all judgments for a sampled query make it into `qrels.tsv`. The 100 sampled queries produced 114 judgments — most queries have one positive judgment, a few have multiple.

## Why the full corpus, not a subset

A subset (only docs cited in sampled qrels) would short-circuit retrieval — recall@100 against ~110 docs is meaningless. Retrieval has to run against the full ~5K-doc corpus to produce numbers that compare to published BEIR results. The 8 MB cost of tracking the full corpus is acceptable for that.

## Re-fetching from upstream

```bash
curl -fsSL -o /tmp/scifact.zip \
  https://public.ukp.informatik.tu-darmstadt.de/thakur/BEIR/datasets/scifact.zip
unzip -o /tmp/scifact.zip -d /tmp/
```
Then re-run the sampling block in `scripts/sample_evals.py` (Phase 7) with the same seed.
