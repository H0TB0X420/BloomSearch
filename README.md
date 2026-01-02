# Bloom Search

A production-ready command-line search engine that ranks web pages based on the likelihood of AI-generated content. Bloom Search helps users identify and filter out AI-generated spam by analyzing content characteristics and leveraging November 2022 (ChatGPT's release) as a temporal cutoff for distinguishing between pre-AI and post-AI era content.

## Project Goals

- Master C++20 modern features and best practices
- Understand search engine architecture from first principles
- Build a production-grade distributed system with cloud deployment
- Develop expertise in information retrieval and ranking algorithms
- Create a portfolio project demonstrating full-stack systems engineering

## Key Features

- **AI Content Detection** - Sophisticated heuristics and temporal analysis to identify AI-generated content
- **Distributed Architecture** - Decoupled crawler, indexer, and query engine for independent scaling
- **Production-Ready** - Containerized deployment, persistent storage, cloud-native design
- **Modern C++20** - Leveraging concepts, ranges, coroutines, and modules
- **Inverted Index** - Fast full-text search using RocksDB with compression
- **Scalable Storage** - Multi-tier storage strategy with PostgreSQL, RocksDB, S3, and Redis
- **CLI Interface** - Clean command-line search experience with ranked results

## Architecture

```
┌──────────┐      ┌──────────┐      ┌─────────────┐
│ Crawler  │─────▶│ Indexer  │─────▶│ Query Engine│
└──────────┘      └──────────┘      └─────────────┘
     │                  │                    │
     ▼                  ▼                    ▼
┌──────────────────────────────────────────────────┐
│  PostgreSQL  │  RocksDB  │  MinIO/S3  │  Redis  │
└──────────────────────────────────────────────────┘
```

### Components

- **Crawler**: Fetches web pages, respects robots.txt, stores raw HTML in S3
- **Indexer**: Parses HTML, detects AI content, builds inverted index
- **Query Engine**: Processes queries, ranks results, formats output
- **Storage Layer**: PostgreSQL (metadata), RocksDB (inverted index), S3 (raw HTML), Redis (cache)

---

## Technology Stack

| Layer | Technology | Purpose |
|-------|-----------|---------|
| **Language** | C++20 | Core implementation |
| **Build** | CMake 3.20+, Ninja | Build system |
| **Databases** | PostgreSQL 15, RocksDB 6.11 | Metadata & indexing |
| **Storage** | MinIO/S3 | Object storage |
| **Cache** | Redis 7 | Query caching |
| **HTTP** | libcurl | Web crawling |
| **Containerization** | Docker, Docker Compose | Development & deployment |
| **Cloud** | DigitalOcean | Production hosting |

## Development Phases

### Phase 1: Foundation (COMPLETE)
- Architecture design & planning
- Docker setup (multi-stage, development & production)
- Project structure & build system (CMake)
- Database schema (9 tables, indexes)
- Core utilities (config, logging, URL parsing)
- Database clients (PostgreSQL, RocksDB)
- Component skeletons (all files compile)
- Three working executables with basic CLI

**Status:** Compiling codebase, working Docker environment, functional CLI

### Phase 2: Crawler Implementation
- HTTP fetcher with libcurl (redirects, timeouts, error handling)
- HTML parser (text extraction, link parsing, cleanup)
- robots.txt parser (crawl-delay, disallow rules)
- URL frontier improvements (priority queue, rate limiting, deduplication)
- S3/MinIO integration (upload raw HTML, compression with zstd)

**Deliverables:** Functional crawler that fetches and stores real web pages

### Phase 3: Indexing & AI Detection
- Tokenizer (normalization, stop words, stemming)
- AI detection heuristics (repetitive phrases, formal patterns, grammar perfection)
- Inverted index builder (term frequency, position tracking, RocksDB writes)
- Batch processing pipeline (poll database, fetch from S3, process and index)

**Deliverables:** Working indexer that builds searchable index with AI scores

### Phase 4: Query Engine
- Query parser (tokenization, boolean operators, phrase queries)
- Ranking algorithm BM25 (TF-IDF, length normalization, AI score boost)
- Result formatter (snippets, AI badges, term highlighting)
- Redis caching layer (frequent queries, TTL, invalidation)

**Deliverables:** Functional search with ranked results and AI indicators

### Phase 5: Optimization & Scale
- Performance optimization (RocksDB tuning, query optimization, batching)
- PageRank implementation (link graph, iterative calculation)
- Compression everywhere (HTML, index, logs)
- Monitoring & metrics (crawl rate, index size, query latency)
- Load testing (10,000+ documents, 100+ queries/second)

**Deliverables:** Optimized system ready for production workloads

### Phase 6: Polish & Deploy
- Production deployment (DigitalOcean droplet, managed PostgreSQL, Spaces)
- Documentation (API docs, deployment guide, architecture diagrams)
- Performance benchmarks and metrics
- Demo preparation (seed URLs, search examples, screenshots)

**Deliverables:** Live demo, polished repository, production deployment

## Target Metrics

- **Index Size:** 100,000+ web pages
- **Query Speed:** Sub-100ms average latency
- **AI Detection:** 80%+ accuracy on test set
- **Crawl Rate:** 10-50 pages/second
- **Storage:** Under 50GB for 100K pages with compression
- **Cost:** Under $100/month on DigitalOcean

## Project Structure

```
bloom-search/
├── Dockerfile                  # Multi-stage build
├── docker-compose.yml          # Development environment
├── docker-compose.prod.yml     # Production deployment
├── CMakeLists.txt              # Build configuration
├── Makefile                    # Common commands
├── sql/init.sql                # Database schema
├── src/
│   ├── main_crawler.cpp        # Crawler entry point
│   ├── main_indexer.cpp        # Indexer entry point
│   ├── main_query.cpp          # Query engine CLI
│   ├── common/                 # Config, logging, utils
│   ├── storage/                # Database clients
│   ├── crawler/                # Web crawling logic
│   ├── indexer/                # Text processing & AI detection
│   └── query/                  # Search and ranking
└── include/                    # Header files (mirrors src/)
```
