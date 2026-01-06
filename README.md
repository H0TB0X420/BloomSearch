# 🌸 BloomSearch

**A search engine that filters AI-generated content from results.**

BloomSearch is a command-line search engine built in modern C++ that crawls the web, indexes content, and ranks results while distinguishing between pre-AI and post-AI era content. It uses heuristic analysis to score documents for AI likelihood, helping users find authentic human-written content.

---

## Features

- **Full-text search** with BM25 ranking
- **Era classification** - Distinguishes pre-AI (before Nov 2022) vs post-AI content
- **AI detection heuristics** - Vocabulary analysis, sentence uniformity, paragraph patterns
- **Multi-format date extraction** - Meta tags, URL patterns, content parsing
- **Query filters** - `era:pre-ai`, `ai:<0.3`, `site:example.com`
- **Multithreaded crawler** - Respects robots.txt and crawl delays
- **Compressed storage** - Zstandard compression for 67% size reduction

---

## Architecture

```
┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│   Crawler   │────▶│   Indexer   │────▶│Query Engine │
│  (MT, 4+)   │     │             │     │   (CLI)     │
└──────┬──────┘     └──────┬──────┘     └──────┬──────┘
       │                   │                   │
       ▼                   ▼                   ▼
┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│ PostgreSQL  │     │   RocksDB   │     │   Ranker    │
│  (metadata) │     │   (index)   │     │   (BM25)    │
└─────────────┘     └─────────────┘     └─────────────┘
       │
       ▼
┌─────────────┐
│ MinIO/S3    │
│ (HTML+Zstd) │
└─────────────┘
```

### Components

| Component | Description |
|-----------|-------------|
| **Crawler** | Multithreaded web crawler with politeness policies, URL filtering, domain limits |
| **Indexer** | HTML parsing, tokenization, stemming, inverted index construction |
| **Query Engine** | Query parsing, BM25 ranking, result formatting, interactive CLI |
| **AI Detector** | Heuristic scoring based on vocabulary, sentence variance, repetition patterns |
| **Date Extractor** | Multi-source date extraction for era classification |

---

## Quick Start

### Prerequisites

- Docker & Docker Compose
- 4GB+ RAM recommended

### 1. Start Infrastructure

```bash
# Clone the repository
git clone https://github.com/yourusername/bloomsearch.git
cd bloomsearch

# Start PostgreSQL, MinIO, and Redis
docker-compose up -d

# Verify services are running
docker-compose ps
```

### 2. Build the Project

```bash
# Enter the development container
docker-compose exec app bash

# Build with CMake and Ninja
cd /app/build
cmake .. -G Ninja
ninja
```

### 3. Crawl Some Pages

```bash
# Crawl with seed URL (multithreaded)
./crawler_mt -u "https://www.paulgraham.com/articles.html" -n 50

# Or use a seeds file
./crawler_mt -s seeds.txt -n 100 -t 4
```

### 4. Build the Index

```bash
# Index all crawled pages
./indexer -n 100
```

### 5. Search

```bash
# Interactive mode
./query_engine

# Direct query
./query_engine "machine learning"

# With filters
./query_engine "startup advice" era:pre-ai
./query_engine "productivity tips" ai:<0.3
```

---

## Usage Examples

### Basic Search

```
$ ./query_engine startup

Found 23 results (12 ms)
────────────────────────────────────────

1. How to Start a Startup
   https://www.paulgraham.com/start.html
   [Mar 15, 2005 · Pre-AI ✓]
   You need three things to create a successful startup: to start with good...

2. Ideas for Startups
   https://www.paulgraham.com/ideas.html
   [Oct 15, 2005 · Pre-AI ✓]
   The way to get startup ideas is not to try to think of startup ideas...
```

### Filter by Era

```bash
# Only pre-AI content (before Nov 30, 2022)
./query_engine "web development" era:pre-ai

# Only post-AI content
./query_engine "ChatGPT" era:post-ai
```

### Filter by AI Score

```bash
# Low AI likelihood (likely human-written)
./query_engine "programming tutorial" ai:<0.3

# High AI likelihood
./query_engine "productivity" ai:>0.5
```

### Filter by Site

```bash
./query_engine "essays" site:paulgraham.com
```

### Interactive Commands

```
search> startup advice       # Search
search> next                 # Next page
search> prev                 # Previous page
search> limit 5              # Results per page
search> stats                # Index statistics
search> help                 # Show commands
search> quit                 # Exit
```

### CLI Options

```bash
./query_engine --help
./query_engine -n 5 "query"      # Limit to 5 results
./query_engine -p 2 "query"      # Page 2
```

---

## Tech Stack

| Layer | Technology |
|-------|------------|
| Language | C++23 (g++-12) |
| Build | CMake + Ninja |
| HTTP | libcurl |
| HTML Parsing | Gumbo |
| Metadata DB | PostgreSQL + libpqxx |
| Index Storage | RocksDB |
| Object Storage | MinIO (S3-compatible) |
| Compression | Zstandard |
| Containerization | Docker + docker-compose |

---

## Project Structure

```
bloomsearch/
├── CMakeLists.txt
├── docker-compose.yml
├── Dockerfile
├── README.md
├── include/
│   ├── common/
│   │   ├── date_utils.h
│   │   ├── logger.h
│   │   └── search_types.h
│   ├── crawler/
│   │   ├── http_fetcher.h
│   │   ├── robots_parser.h
│   │   └── url_frontier.h
│   ├── indexer/
│   │   ├── ai_detector.h
│   │   ├── html_parser.h
│   │   ├── index_builder.h
│   │   └── tokenizer.h
│   ├── query/
│   │   ├── query_parser.h
│   │   ├── ranker.h
│   │   └── result_formatter.h
│   └── storage/
│       ├── postgres_client.h
│       ├── rocksdb/
│       └── s3_client.h
├── src/
│   ├── main_crawler.cpp
│   ├── main_crawler_mt.cpp
│   ├── main_indexer.cpp
│   ├── main_query.cpp
│   └── ... (implementations)
└── tests/
    └── ... (test files)
```

---

## AI Detection

BloomSearch uses three heuristics to estimate AI likelihood:

### 1. Vocabulary Analysis (40%)
Detects overused AI phrases like "delve", "tapestry", "it's important to note", "in conclusion".

### 2. Sentence Uniformity (35%)
Measures variance in sentence length. AI tends toward uniform sentences; humans write with more variation.

### 3. Paragraph Repetition (25%)
Checks for formulaic paragraph starters ("Furthermore", "Additionally", "Moreover").

**Score interpretation:**
- `0-20%` - Likely human-written
- `20-50%` - Mixed signals
- `50%+` - Likely AI-generated

---

## Configuration

### Environment Variables

```bash
# PostgreSQL
POSTGRES_HOST=localhost
POSTGRES_PORT=5432
POSTGRES_DB=searchengine
POSTGRES_USER=searchuser
POSTGRES_PASSWORD=searchpass

# MinIO/S3
S3_ENDPOINT=http://localhost:9000
S3_ACCESS_KEY=minioadmin
S3_SECRET_KEY=minioadmin
S3_BUCKET=bloom-content

# RocksDB
ROCKSDB_PATH=/data/rocksdb
```

---

## Performance

- **Crawl rate**: ~10-20 pages/second (politeness-limited)
- **Index size**: ~1KB per document
- **Query latency**: <50ms typical
- **Storage**: ~30% of raw HTML after Zstd compression

---

## AI Disclosure

This project was developed with AI assistance using **Claude Opus 4.5**.

---

## License

MIT License - See [LICENSE](LICENSE) for details.

---

## Acknowledgments

- [RocksDB](https://rocksdb.org/) - Embedded key-value store
- [Gumbo](https://github.com/google/gumbo-parser) - HTML5 parser
- [libcurl](https://curl.se/libcurl/) - HTTP client
- [Zstandard](https://facebook.github.io/zstd/) - Compression
