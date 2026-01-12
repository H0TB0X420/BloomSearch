# BloomSearch

A search engine that filters AI-generated content from results.

The web changed on November 30, 2022. Everything published after that date might be ChatGPT output. BloomSearch crawls the web, indexes content, and lets you filter by era (pre-AI vs post-AI) or by how likely a page is to be AI-generated.

## How it works

```
Crawler → Indexer → Query Engine
   ↓         ↓           ↓
PostgreSQL  RocksDB    BM25 Ranker
   ↓
MinIO (compressed HTML)
```

The crawler is multithreaded with politeness policies. The indexer parses HTML, tokenizes, stems, and builds an inverted index in RocksDB. The query engine ranks with BM25 and filters by AI score.

## AI detection

Three heuristics, weighted into a single percentage:

**Vocabulary (40%)** — Flags phrases like "delve," "tapestry," "it's important to note." The words AI loves.

**Sentence uniformity (35%)** — AI writes sentences of similar length. Humans don't.

**Paragraph starters (25%)** — Counts "Furthermore," "Additionally," "Moreover" openers.

Scores below 20% are likely human. Above 50% is likely AI. The middle is uncertain.

## Quick start

```bash
# Start infrastructure
docker-compose up -d

# Build
docker-compose exec app bash
cd /app/build && cmake .. -G Ninja && ninja

# Crawl 50 pages from Paul Graham's site
./crawler_mt -u "https://www.paulgraham.com/articles.html" -n 50

# Index them
./indexer -n 100

# Search
./query_engine "startup advice" era:pre-ai
./query_engine "productivity" ai:<0.3
```

## Tech stack

C++23, CMake, Ninja, libcurl, Gumbo for HTML parsing, PostgreSQL for metadata, RocksDB for the index, MinIO for compressed HTML storage, Zstandard compression, Docker.

## What's next

Cloud deployment for 24/7 crawling. Results improve with scale. Until a corner of the web is sufficiently explored, queries in that area won't return much. A background ETL process to train a real AI detector instead of heuristics. A web UI that looks like 2005 Google.

---

Built with Claude Opus 4.5. 
[MIT License](LICENSE.md)