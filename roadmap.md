# Bloom Search - 4-Week Development Roadmap

**Project Goal**: Production-ready CLI search engine that ranks pages by AI-generation likelihood

**Timeline**: 4 weeks (Dec 16, 2024 - Jan 13, 2025)

**Completed**: 
- [x] Project scaffolding and Docker environment
- [x] Common utilities (Logger, Config)
- [x] HTTPFetcher with retry logic, SSL verification, redirect handling

---

## Week 1: Crawler Foundation + Storage Layer
**Focus**: Complete the web crawler and establish database connections

### Task 1.1: Robots.txt Parser (4 hours)
Parse and respect robots.txt directives for ethical crawling.

| Subtask | Description | Completion Criteria | Time |
|---------|-------------|---------------------|------|
| 1.1.1 | Basic parser structure | Class compiles, can load robots.txt content from string | 45 min |
| 1.1.2 | User-agent matching | Correctly identifies rules for BloomSearchBot and wildcard (*) | 45 min |
| 1.1.3 | Disallow/Allow rules | `is_allowed(url)` returns correct bool for test cases | 45 min |
| 1.1.4 | Crawl-delay extraction | `get_crawl_delay()` returns delay in seconds or default | 30 min |
| 1.1.5 | Integration with HTTPFetcher | Fetches robots.txt from domain, caches result | 45 min |
| 1.1.6 | Tests | 10+ test cases covering edge cases (empty, malformed, multiple agents) | 30 min |

**Deliverable**: `RobotsParser` class that fetches and interprets robots.txt

---

### Task 1.2: URL Frontier (4 hours)
Priority queue managing URLs to crawl with politeness policies.

| Subtask | Description | Completion Criteria | Time |
|---------|-------------|---------------------|------|
| 1.2.1 | URL normalization | Canonicalizes URLs (lowercase host, remove fragments, default ports) | 45 min |
| 1.2.2 | Domain extraction | Extracts domain from URL for politeness grouping | 30 min |
| 1.2.3 | Priority queue structure | URLs ordered by priority score, FIFO within same priority | 45 min |
| 1.2.4 | Deduplication | Tracks seen URLs, rejects duplicates | 30 min |
| 1.2.5 | Per-domain rate limiting | Enforces minimum delay between requests to same domain | 45 min |
| 1.2.6 | Seed URL loading | Loads initial URLs from file or config | 30 min |
| 1.2.7 | Tests | Queue ordering, dedup, rate limiting verified | 30 min |

**Deliverable**: `URLFrontier` class managing crawl queue with politeness

---

### Task 1.3: PostgreSQL Client (3 hours)
Store page metadata (URL, crawl time, status, content hash).

| Subtask | Description | Completion Criteria | Time |
|---------|-------------|---------------------|------|
| 1.3.1 | Connection management | Connect/disconnect, connection pooling via libpqxx | 45 min |
| 1.3.2 | Schema initialization | Creates tables if not exist on startup | 30 min |
| 1.3.3 | Page metadata CRUD | Insert/update/query page records | 45 min |
| 1.3.4 | Crawl state persistence | Save/restore frontier state for crash recovery | 30 min |
| 1.3.5 | Tests | CRUD operations verified against test database | 30 min |

**Deliverable**: `PostgresClient` class for metadata storage

---

### Task 1.4: MinIO/S3 Client (2 hours)
Store raw HTML content with Zstandard compression.

| Subtask | Description | Completion Criteria | Time |
|---------|-------------|---------------------|------|
| 1.4.1 | Connection setup | Connect to MinIO using AWS SDK or direct API | 30 min |
| 1.4.2 | Zstd compression | Compress content before storage, decompress on retrieval | 30 min |
| 1.4.3 | Store/retrieve content | `put(url_hash, content)` and `get(url_hash)` work | 30 min |
| 1.4.4 | Tests | Round-trip storage verified, compression ratio logged | 30 min |

**Deliverable**: `S3Client` class for compressed HTML storage

---

### Task 1.5: Crawler Main Loop (3 hours)
Orchestrate components into working crawler.

| Subtask | Description | Completion Criteria | Time |
|---------|-------------|---------------------|------|
| 1.5.1 | Component integration | Main loop uses Frontier, Fetcher, RobotsParser, Storage | 45 min |
| 1.5.2 | Graceful shutdown | SIGINT/SIGTERM triggers clean shutdown, state saved | 30 min |
| 1.5.3 | Progress logging | Logs pages/minute, queue size, errors every N seconds | 30 min |
| 1.5.4 | Link extraction (basic) | Extracts <a href> links, adds to frontier | 45 min |
| 1.5.5 | End-to-end test | Crawls 100 pages from seed URL successfully | 30 min |

**Deliverable**: `main_crawler` executable that crawls the web

---

## Week 2: Indexer Pipeline
**Focus**: Parse HTML, tokenize text, build searchable index

### Task 2.1: HTML Parser (3 hours)
Extract text content and metadata from HTML.

| Subtask | Description | Completion Criteria | Time |
|---------|-------------|---------------------|------|
| 2.1.1 | Library integration | Integrate Gumbo or lexbor HTML parser | 30 min |
| 2.1.2 | Text extraction | Extracts visible text, ignoring script/style/nav | 45 min |
| 2.1.3 | Title extraction | Extracts <title> content | 15 min |
| 2.1.4 | Meta tag extraction | Extracts description, keywords, author, date | 30 min |
| 2.1.5 | Link extraction | Extracts all <a href> with anchor text | 30 min |
| 2.1.6 | Tests | Parses 5 real-world pages correctly | 30 min |

**Deliverable**: `HTMLParser` class extracting structured data from HTML

---

### Task 2.2: Tokenizer (2.5 hours)
Convert text to searchable tokens.

| Subtask | Description | Completion Criteria | Time |
|---------|-------------|---------------------|------|
| 2.2.1 | Basic tokenization | Split on whitespace and punctuation | 30 min |
| 2.2.2 | Case normalization | Lowercase all tokens | 15 min |
| 2.2.3 | Stop word removal | Filter common words (the, is, at, etc.) | 30 min |
| 2.2.4 | Stemming (Porter) | Reduce words to stems (running -> run) | 45 min |
| 2.2.5 | N-gram support | Generate bigrams for phrase search capability | 30 min |
| 2.2.6 | Tests | Tokenizes sample documents correctly | 15 min |

**Deliverable**: `Tokenizer` class producing search-ready tokens

---

### Task 2.3: RocksDB Client (2 hours)
Persistent key-value store for inverted index.

| Subtask | Description | Completion Criteria | Time |
|---------|-------------|---------------------|------|
| 2.3.1 | Database setup | Open/close RocksDB, configure compression | 30 min |
| 2.3.2 | Basic operations | Put/Get/Delete with string keys and values | 30 min |
| 2.3.3 | Batch writes | Accumulate writes, flush in batches for performance | 30 min |
| 2.3.4 | Prefix iteration | Iterate all keys with given prefix (for term lookups) | 30 min |

**Deliverable**: `RocksDBClient` class for index storage

---

### Task 2.4: Index Builder (4 hours)
Build inverted index from parsed documents.

| Subtask | Description | Completion Criteria | Time |
|---------|-------------|---------------------|------|
| 2.4.1 | Posting list structure | Define format: term -> [(doc_id, frequency, positions)] | 30 min |
| 2.4.2 | Document processing | Parse HTML, tokenize, count term frequencies | 45 min |
| 2.4.3 | Index writing | Write posting lists to RocksDB | 45 min |
| 2.4.4 | Document metadata | Store doc_id -> (url, title, snippet) mapping | 30 min |
| 2.4.5 | TF-IDF preparation | Store document frequencies for scoring | 30 min |
| 2.4.6 | Incremental updates | Add new documents without full rebuild | 45 min |
| 2.4.7 | Tests | Index 50 documents, verify term lookups work | 30 min |

**Deliverable**: `IndexBuilder` class creating searchable inverted index

---

### Task 2.5: Redis Client (1.5 hours)
Caching layer for frequently accessed data.

| Subtask | Description | Completion Criteria | Time |
|---------|-------------|---------------------|------|
| 2.5.1 | Connection setup | Connect to Redis, handle reconnection | 30 min |
| 2.5.2 | Cache operations | Get/Set with TTL, Delete | 30 min |
| 2.5.3 | Integration points | Cache robots.txt, hot posting lists | 30 min |

**Deliverable**: `RedisClient` class for caching

---

### Task 2.6: Indexer Main Loop (2 hours)
Process crawled pages into searchable index.

| Subtask | Description | Completion Criteria | Time |
|---------|-------------|---------------------|------|
| 2.6.1 | Batch processing | Reads pages from S3, processes in batches | 45 min |
| 2.6.2 | Progress tracking | Tracks indexed vs pending pages in Postgres | 30 min |
| 2.6.3 | Error handling | Logs and skips malformed pages | 30 min |
| 2.6.4 | End-to-end test | Indexes crawled pages, verifies search works | 15 min |

**Deliverable**: `main_indexer` executable building search index

---

## Week 3: AI Detection + Query Engine
**Focus**: Score content for AI likelihood, enable searching

### Task 3.1: AI Detector - Era Classification (2 hours)
Classify content by publication date relative to AI era.

| Subtask | Description | Completion Criteria | Time |
|---------|-------------|---------------------|------|
| 3.1.1 | Date extraction | Extract dates from meta tags, URL patterns, page content | 45 min |
| 3.1.2 | Era classification | Pre-AI (<Nov 2022), Transition (Nov 2022-2023), AI-Era (2024+) | 30 min |
| 3.1.3 | Confidence scoring | Score 0-1 based on date source reliability | 30 min |
| 3.1.4 | Tests | Correctly classifies 20 sample pages | 15 min |

**Deliverable**: Era-based classification component

---

### Task 3.2: AI Detector - Heuristic Signals (3 hours)
Fast heuristics for AI content likelihood.

| Subtask | Description | Completion Criteria | Time |
|---------|-------------|---------------------|------|
| 3.2.1 | Vocabulary analysis | Detect AI-typical phrases ("delve", "tapestry", "in conclusion") | 45 min |
| 3.2.2 | Sentence structure | Measure sentence length variance (AI tends toward uniformity) | 30 min |
| 3.2.3 | Paragraph patterns | Detect formulaic intro/body/conclusion structures | 30 min |
| 3.2.4 | Punctuation analysis | AI often underuses semicolons, dashes, parentheticals | 30 min |
| 3.2.5 | Combined scoring | Weighted combination of signals into 0-1 score | 30 min |
| 3.2.6 | Tests | Scores 10 known-human vs 10 known-AI samples distinctly | 15 min |

**Deliverable**: Heuristic-based AI likelihood scorer

---

### Task 3.3: AI Detector Integration (1.5 hours)
Combine signals into final AI score stored with index.

| Subtask | Description | Completion Criteria | Time |
|---------|-------------|---------------------|------|
| 3.3.1 | Score aggregation | Combine era + heuristics with configurable weights | 30 min |
| 3.3.2 | Index integration | Store AI score in document metadata | 30 min |
| 3.3.3 | Batch processing | Score all indexed documents | 30 min |

**Deliverable**: `AIDetector` class integrated with indexer

---

### Task 3.4: Query Parser (2 hours)
Parse user search queries into structured form.

| Subtask | Description | Completion Criteria | Time |
|---------|-------------|---------------------|------|
| 3.4.1 | Basic tokenization | Split query into terms using same tokenizer | 30 min |
| 3.4.2 | Phrase queries | Support "quoted phrases" as single unit | 30 min |
| 3.4.3 | Filter syntax | Parse filters: era:pre-ai, ai:<0.3, site:example.com | 45 min |
| 3.4.4 | Tests | Parses 15 query variations correctly | 15 min |

**Deliverable**: `QueryParser` class producing structured queries

---

### Task 3.5: Ranker (3 hours)
Score and rank search results.

| Subtask | Description | Completion Criteria | Time |
|---------|-------------|---------------------|------|
| 3.5.1 | TF-IDF scoring | Calculate relevance score from term frequencies | 45 min |
| 3.5.2 | BM25 implementation | Implement BM25 for better relevance (optional upgrade) | 45 min |
| 3.5.3 | AI score integration | Boost/penalize results based on AI likelihood | 30 min |
| 3.5.4 | Era filtering | Filter results by era when requested | 30 min |
| 3.5.5 | Result merging | Combine scores from multiple query terms | 30 min |

**Deliverable**: `Ranker` class scoring and ordering results

---

### Task 3.6: Result Formatter (1.5 hours)
Format search results for CLI display.

| Subtask | Description | Completion Criteria | Time |
|---------|-------------|---------------------|------|
| 3.6.1 | Basic formatting | Display title, URL, snippet for each result | 30 min |
| 3.6.2 | Snippet generation | Extract relevant text snippet with query highlighting | 30 min |
| 3.6.3 | AI indicators | Show AI likelihood score and era for each result | 15 min |
| 3.6.4 | Pagination | Support --page and --limit flags | 15 min |

**Deliverable**: `ResultFormatter` class for CLI output

---

### Task 3.7: Query Engine Main (2 hours)
CLI interface for searching.

| Subtask | Description | Completion Criteria | Time |
|---------|-------------|---------------------|------|
| 3.7.1 | Argument parsing | Parse search query and flags from command line | 30 min |
| 3.7.2 | Query execution | Wire parser -> index lookup -> ranker -> formatter | 45 min |
| 3.7.3 | Interactive mode | Optional REPL for multiple queries | 30 min |
| 3.7.4 | End-to-end test | Search returns relevant results from crawled content | 15 min |

**Deliverable**: `main_query` executable for searching

---

## Week 4: Integration, Polish, Documentation
**Focus**: Make it production-ready and portfolio-worthy

### Task 4.1: End-to-End Integration (3 hours)

| Subtask | Description | Completion Criteria | Time |
|---------|-------------|---------------------|------|
| 4.1.1 | Full pipeline test | Crawl -> Index -> Search works for 500+ pages | 1 hr |
| 4.1.2 | Docker compose validation | All services start correctly, data persists | 45 min |
| 4.1.3 | Error recovery | Restart after crash continues from last state | 45 min |
| 4.1.4 | Performance baseline | Document pages/second for crawl, index, query | 30 min |

---

### Task 4.2: CLI Polish (2 hours)

| Subtask | Description | Completion Criteria | Time |
|---------|-------------|---------------------|------|
| 4.2.1 | Help text | --help shows all commands and options clearly | 30 min |
| 4.2.2 | Progress indicators | Crawl and index show progress bars or status | 30 min |
| 4.2.3 | Error messages | User-friendly error messages for common issues | 30 min |
| 4.2.4 | Configuration | Config file or environment variable support | 30 min |

---

### Task 4.3: Documentation (3 hours)

| Subtask | Description | Completion Criteria | Time |
|---------|-------------|---------------------|------|
| 4.3.1 | README | Clear project overview, setup instructions, usage examples | 1 hr |
| 4.3.2 | Architecture doc | System design diagram and component descriptions | 45 min |
| 4.3.3 | API documentation | Document all public class interfaces | 45 min |
| 4.3.4 | Demo script | Scripted demo showing key features | 30 min |

---

### Task 4.4: Testing & Quality (3 hours)

| Subtask | Description | Completion Criteria | Time |
|---------|-------------|---------------------|------|
| 4.4.1 | Test coverage | All major components have unit tests | 1 hr |
| 4.4.2 | Integration tests | Automated tests for full pipeline | 1 hr |
| 4.4.3 | Code cleanup | Remove debug code, consistent formatting | 30 min |
| 4.4.4 | Memory/leak check | Valgrind or ASan shows no leaks | 30 min |

---

### Task 4.5: Portfolio Preparation (2 hours)

| Subtask | Description | Completion Criteria | Time |
|---------|-------------|---------------------|------|
| 4.5.1 | GitHub repo polish | Clean commit history, proper .gitignore, LICENSE | 30 min |
| 4.5.2 | Screenshots/GIFs | Visual demos of search in action | 30 min |
| 4.5.3 | Technical blog post | Write-up of design decisions and challenges | 45 min |
| 4.5.4 | Resume bullet points | Quantified achievements ready for resume | 15 min |

---

## Time Summary

| Week | Focus | Estimated Hours |
|------|-------|-----------------|
| 1 | Crawler + Storage | 16 hours |
| 2 | Indexer Pipeline | 15 hours |
| 3 | AI Detection + Query | 15 hours |
| 4 | Integration + Polish | 13 hours |
| **Total** | | **59 hours** |

---

**If ahead of schedule, add**:
1. Web UI (simple HTML/JS frontend)
2. More sophisticated AI detection
3. PageRank-style link analysis
4. Distributed crawling
