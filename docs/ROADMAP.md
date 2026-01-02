# Bloom Search - MVP Development Roadmap

## Overview

This roadmap outlines the development plan to take Bloom Search from a working foundation to a production-ready search engine with AI content detection capabilities. The project is structured in 6 phases over approximately 6 weeks.

## Current Status: Phase 1 Complete

**Completed:**
- Docker development environment operational
- All services running (PostgreSQL, MinIO, Redis, RocksDB)
- Database schema deployed with 9 tables
- Project compiles successfully with C++20
- Core infrastructure and placeholder implementations in place

---

## Phase 2: HTTP Crawler

**Timeline:** Week 1-2  
**Goal:** Fetch real web pages and store them in the database and S3

### 2.1 HTTP Fetcher with libcurl

**Tasks:**
- Implement real HTTP/HTTPS request functionality using libcurl
- Handle HTTP redirects with maximum limit of 3 hops
- Set appropriate timeouts for requests
- Handle common HTTP errors including 404, 500, and timeout scenarios
- Return HTTP status codes for error handling
- Add proper request headers including User-Agent and Accept
- Implement retry logic with exponential backoff for failed requests

**Testing Requirements:**
- Verify successful fetching of known good URLs
- Test handling of 404 errors
- Test timeout behavior with slow-responding servers
- Validate retry logic with intermittent failures

### 2.2 robots.txt Parser

**Tasks:**
- Implement robots.txt fetching and parsing for each domain
- Parse User-agent, Disallow, and Allow directives
- Handle crawl-delay directive for rate limiting
- Implement in-memory caching of robots.txt files with 24-hour TTL
- Respect crawl-delay between requests to the same domain

**Testing Requirements:**
- Verify disallowed paths are blocked
- Confirm allowed paths are accessible
- Test crawl-delay enforcement

### 2.3 URL Frontier Enhancements

**Tasks:**
- Implement priority queue with configurable breadth-first or depth-first crawling
- Add domain-based rate limiting to track last crawl time per domain
- Enforce minimum delay between requests to same domain
- Implement URL deduplication using hash set
- Add persistence layer to save frontier state to database on shutdown
- Implement frontier recovery on startup

**Testing Requirements:**
- Verify rate limiting between domain requests
- Test deduplication prevents duplicate URLs
- Validate priority queue ordering
- Test persistence and recovery after restart

### 2.4 S3/MinIO Integration

**Tasks:**
- Implement upload functionality with proper S3 authentication signatures
- Handle multipart uploads for large files
- Integrate zstd compression before uploading HTML content
- Implement download functionality with decompression
- Create required buckets on startup if they don't exist
- Add error handling for storage failures

**Testing Requirements:**
- Upload and download test files successfully
- Verify compression reduces storage size
- Test multipart upload for large files
- Validate bucket creation logic

### 2.5 Integration: Full Crawler Loop

**Tasks:**
- Load seed URLs from database on startup
- Extract links from fetched HTML pages
- Add discovered URLs to frontier with appropriate depth
- Update database with crawl status and timestamps
- Implement statistics logging for pages per second and error rates
- Add graceful shutdown handling

**Acceptance Criteria:**
- Successfully crawl 100 real web pages without crashes
- Store all HTML content in MinIO
- Run continuously for 1 hour without memory leaks
- Respect robots.txt for all domains tested
- Handle network errors gracefully without terminating

---

## Phase 3: Indexer & AI Detection

**Timeline:** Week 2-3  
**Goal:** Parse HTML, detect AI-generated content, build searchable inverted index

### 3.1 HTML Parser

**Tasks:**
- Integrate HTML parsing library (lexbor or gumbo-parser)
- Extract clean text from body, paragraphs, and headings
- Remove scripts, styles, and comments from extracted text
- Strip all HTML tags from output
- Decode HTML entities to proper characters
- Extract metadata including title, description, and language
- Parse links for crawler to add to frontier

**Testing Requirements:**
- Verify text extraction from various HTML structures
- Confirm scripts and styles are removed
- Test HTML entity decoding
- Validate metadata extraction

### 3.2 Tokenizer

**Tasks:**
- Implement text normalization with lowercase conversion
- Remove punctuation from tokens
- Split text on whitespace boundaries
- Implement stop word filtering for common words
- Integrate Porter stemmer for word normalization
- Handle special cases including URLs, emails, and numbers

**Testing Requirements:**
- Verify stop words are removed
- Confirm stemming reduces words to root forms
- Test tokenization of complex text with punctuation
- Validate handling of special characters

### 3.3 AI Detector

**Tasks:**
- Implement temporal analysis to detect pre-November 2022 content
- Build repetition detection for repeated phrases
- Create formality scoring to detect overly formal language patterns
- Implement generic statement identification
- Add grammar perfection detection for suspiciously error-free text
- Combine all heuristics into final score between 0.0 and 1.0
- Tune weights for each detection heuristic

**Heuristic Components:**
- Repetition detection (weight: 0.2)
- Formality measurement (weight: 0.15)
- Generic content counting (weight: 0.15)
- Grammar perfection analysis (weight: 0.1)
- Temporal markers (weight: 0.4)

**Testing Requirements:**
- Test with known human-written content (casual, informal)
- Test with known AI-generated content (formal, repetitive)
- Validate score falls between 0.0 and 1.0
- Compare scores across diverse content types

### 3.4 Inverted Index Builder

**Tasks:**
- Create inverted index mapping terms to document IDs
- Store term frequency for each term in each document
- Record document length for normalization in ranking
- Store token positions for phrase query support
- Write index entries to RocksDB with structured format
- Implement batch writing for performance optimization

**RocksDB Schema Design:**
- Term-to-document mapping with frequency and positions
- Document metadata with URL, length, and AI score
- Efficient key structure for fast lookups

**Testing Requirements:**
- Verify index entries created for all terms
- Test retrieval of term frequencies
- Validate document metadata storage
- Check batch write performance

### 3.5 Batch Processing Pipeline

**Tasks:**
- Implement polling of PostgreSQL for uncrawled pages
- Fetch HTML content from MinIO for processing
- Execute pipeline: Parse, Tokenize, Detect AI, Index
- Update database with calculated AI scores
- Process documents in configurable batch sizes
- Add error handling and retry logic for failed documents

**Acceptance Criteria:**
- Successfully index 1,000 real web pages
- AI detection scores populated in database for all pages
- RocksDB contains complete searchable inverted index
- Achieve indexing rate of 10-50 pages per second
- No data loss or corruption during processing

---

## Phase 4: Query Engine

**Timeline:** Week 3-4  
**Goal:** Search indexed pages and return ranked results with AI scores

### 4.1 Query Parser

**Tasks:**
- Tokenize search queries using same logic as document tokenization
- Implement boolean operator handling for AND, OR, NOT
- Add phrase query support with exact matching
- Apply stemming to query terms for consistency
- Handle query syntax errors gracefully

**Testing Requirements:**
- Verify multi-term queries parse correctly
- Test boolean operators function properly
- Validate phrase queries preserve word order
- Confirm query stemming matches document stemming

### 4.2 BM25 Ranking Algorithm

**Tasks:**
- Implement BM25 algorithm with standard parameters
- Calculate term frequency-inverse document frequency scores
- Apply document length normalization
- Set tuning parameters: k1=1.5, b=0.75
- Integrate AI score to boost human-written content
- Return top 100 results sorted by relevance score
- Handle edge cases for terms not in index

**Ranking Formula Components:**
- Term frequency calculation
- Inverse document frequency calculation
- Document length normalization
- AI score boost (20% preference for human content)

**Testing Requirements:**
- Verify results sorted by relevance
- Test AI score affects ranking appropriately
- Validate scoring for single and multi-term queries
- Check edge cases with missing terms

### 4.3 Result Formatter

**Tasks:**
- Format search results with rank, URL, title, and snippet
- Display AI likelihood as human-readable percentage
- Implement query term highlighting in snippets
- Truncate snippets to appropriate length
- Add metadata display for publication date when available
- Format output for CLI readability

**Output Requirements:**
- Clear visual hierarchy for results
- Prominent AI score badge
- Highlighted query terms
- Properly truncated snippets

**Testing Requirements:**
- Verify all fields display correctly
- Test term highlighting in various contexts
- Validate AI score badge format
- Check snippet truncation

### 4.4 Redis Caching

**Tasks:**
- Integrate hiredis library for Redis connectivity
- Implement cache storage with structured keys
- Set appropriate TTL for cached query results
- Design cache key format using stemmed query terms
- Implement cache invalidation when index updates
- Add cache hit/miss statistics logging

**Testing Requirements:**
- Verify cache stores and retrieves results
- Test TTL expiration removes old entries
- Validate cache hit improves query speed
- Check invalidation on index updates

### 4.5 CLI Interface Improvements

**Tasks:**
- Display query execution time for each search
- Implement pagination with configurable results per page
- Show result count statistics
- Add filtering by AI score threshold
- Improve command parsing and error messages
- Add help text for available commands

**Acceptance Criteria:**
- Search returns relevant results in under 100ms average latency
- AI score badge displays correctly for all results
- Results properly ranked by relevance and AI score
- Cache demonstrably improves repeat query performance
- CLI is intuitive and responsive to user input

---

## Phase 5: Testing & Validation

**Timeline:** Week 4-5  
**Goal:** Comprehensive testing and performance validation

### 5.1 End-to-End Integration Tests

**Tasks:**
- Create test suite for full crawl-index-query pipeline
- Seed database with diverse test URLs
- Execute complete workflow from crawling to search
- Verify data consistency across all components
- Test error handling and recovery scenarios

**Test Scenarios:**
- Complete pipeline with real websites
- Network failure during crawling
- Database connection loss and recovery
- RocksDB corruption handling
- S3 storage failures

### 5.2 Load Testing

**Tasks:**
- Index 10,000+ diverse web pages
- Execute 100 concurrent query requests
- Measure latency percentiles (p50, p95, p99)
- Monitor memory usage under load
- Check for memory leaks using valgrind
- Test sustained operation over 24+ hours
- Profile CPU usage and optimize hotspots

**Performance Targets:**
- Query latency: p95 < 100ms
- Indexing rate: 10-50 pages/second
- Memory usage: stable over 24 hours
- Zero memory leaks detected
- CPU usage: reasonable under concurrent load

### 5.3 Data Validation

**Tasks:**
- Verify all crawled pages recorded in database
- Check AI scores are within valid range (0.0-1.0)
- Confirm inverted index consistency and completeness
- Test database backup and restore procedures
- Validate data integrity after crashes
- Check for orphaned data in S3

**Validation Checks:**
- Database referential integrity
- Index completeness for all documents
- AI score distribution analysis
- Storage efficiency metrics

### 5.4 AI Detection Accuracy Testing

**Tasks:**
- Create test set of known human-written content
- Create test set of known AI-generated content
- Calculate accuracy, precision, and recall metrics
- Tune detection heuristics based on results
- Document detection methodology and accuracy

**Target Metrics:**
- Overall accuracy: 80%+
- False positive rate: <20%
- Ability to distinguish pre-2022 vs post-2022 content

---

## Phase 6: MVP Launch

**Timeline:** Week 5-6  
**Goal:** Production deployment and demo preparation

### 6.1 Production Deployment

**Tasks:**
- Provision DigitalOcean droplet with appropriate resources
- Configure managed PostgreSQL database
- Set up DigitalOcean Spaces for S3-compatible storage
- Configure all environment variables for production
- Enable SSL/HTTPS for secure connections
- Set up automated backups
- Configure monitoring and alerting
- Document deployment procedures

**Infrastructure:**
- Droplet: 4GB RAM, 2 vCPU
- Managed PostgreSQL: 1GB RAM
- Spaces: S3-compatible object storage
- Block storage volume for RocksDB
- Estimated cost: $50-100/month

### 6.2 Documentation

**Tasks:**
- Update README with demo screenshots and examples
- Write comprehensive deployment guide
- Document AI detection algorithm and heuristics
- Create performance benchmarks report
- Document API and CLI commands
- Write troubleshooting guide
- Add architecture diagrams

**Documentation Sections:**
- Getting started guide
- Architecture overview
- Deployment instructions
- AI detection methodology
- Performance benchmarks
- Troubleshooting common issues

### 6.3 Demo Preparation

**Tasks:**
- Seed production database with interesting URLs
- Focus on tech blogs, news sites, and diverse content
- Create demo queries showcasing AI detection
- Record demo video showing key features
- Take high-quality screenshots of results
- Prepare comparison showing AI vs human scores
- Document interesting findings

**Demo Showcase:**
- Live search demonstration
- AI detection in action
- Performance metrics
- Scale metrics (pages indexed, query speed)
- Unique value proposition (AI filtering)

### 6.4 Final Polish

**Tasks:**
- Review and fix any known bugs
- Optimize query performance
- Clean up logging output
- Improve error messages
- Add usage statistics
- Final code review and cleanup

---

## MVP Success Criteria

At the end of 6 weeks, Bloom Search must achieve:

**Functionality:**
- Crawl and index 10,000+ real web pages
- Return search results in under 100ms average latency
- Display AI likelihood score for each result
- Show measurable and accurate difference between AI and human content

**Performance:**
- Handle 100+ queries per second
- Run stably for 24+ hours without crashes
- Maintain consistent memory usage
- Process 10-50 pages per second during indexing

**Deployment:**
- Be deployable to production using Docker
- Include comprehensive documentation
- Have automated deployment scripts
- Include monitoring and backup procedures

**Quality:**
- No critical bugs
- Graceful error handling
- Clean, maintainable code
- Professional user interface

---

## Development Priorities

**Critical Path (Must Have):**
1. HTTP Fetcher - blocks all crawling
2. HTML Parser - blocks all indexing
3. Tokenizer - blocks search functionality
4. Inverted Index - core search feature
5. BM25 Ranking - required for relevance

**Important (Should Have):**
1. AI Detection - unique differentiator
2. Redis Caching - performance optimization
3. robots.txt Parser - responsible crawling
4. Result formatting - user experience

**Nice to Have (Could Have):**
1. Advanced query syntax
2. Phrase queries
3. PageRank integration
4. Advanced AI detection heuristics

---
