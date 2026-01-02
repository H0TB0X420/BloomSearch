-- Bloom Search Database Schema
-- This file is run on first startup by Docker PostgreSQL

-- Pages table - stores metadata for all crawled pages
CREATE TABLE IF NOT EXISTS pages (
    id SERIAL PRIMARY KEY,
    url TEXT UNIQUE NOT NULL,
    url_hash TEXT NOT NULL,
    domain TEXT NOT NULL,
    crawled_at TIMESTAMP,
    status_code INTEGER,
    content_hash TEXT,
    content_size BIGINT DEFAULT 0,
    ai_score FLOAT,
    era TEXT,  -- 'pre-ai', 'transition', 'ai-era'
    created_at TIMESTAMP DEFAULT NOW()
);

-- Indexes for efficient lookups
CREATE INDEX IF NOT EXISTS idx_pages_url_hash ON pages(url_hash);
CREATE INDEX IF NOT EXISTS idx_pages_domain ON pages(domain);
CREATE INDEX IF NOT EXISTS idx_pages_unindexed ON pages(crawled_at) WHERE ai_score IS NULL;
CREATE INDEX IF NOT EXISTS idx_pages_era ON pages(era);

-- Frontier state table - for crash recovery
CREATE TABLE IF NOT EXISTS frontier_state (
    id SERIAL PRIMARY KEY,
    url TEXT NOT NULL,
    domain TEXT NOT NULL,
    priority INTEGER DEFAULT 0,
    added_at TIMESTAMP DEFAULT NOW()
);

-- Crawl statistics table
CREATE TABLE IF NOT EXISTS crawl_stats (
    id SERIAL PRIMARY KEY,
    recorded_at TIMESTAMP DEFAULT NOW(),
    pages_crawled INTEGER DEFAULT 0,
    pages_indexed INTEGER DEFAULT 0,
    pages_failed INTEGER DEFAULT 0,
    bytes_downloaded BIGINT DEFAULT 0
);

-- Domain-specific settings (robots.txt cache, crawl delays)
CREATE TABLE IF NOT EXISTS domain_settings (
    domain TEXT PRIMARY KEY,
    robots_txt TEXT,
    robots_fetched_at TIMESTAMP,
    crawl_delay_ms INTEGER DEFAULT 1000,
    last_crawled_at TIMESTAMP
);

-- Print confirmation
DO $$
BEGIN
    RAISE NOTICE 'Bloom Search schema initialized successfully';
END $$;