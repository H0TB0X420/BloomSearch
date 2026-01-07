#include "common/config.h"
#include "common/logger.h"
#include "storage/postgres_client.h"
#include "storage/s3_client.h"
#include "crawler/url_frontier_mt.h"
#include "crawler/http_fetcher.h"
#include "crawler/robots_parser.h"
#include "crawler/url_filter.h"
#include "indexer/html_parser.h"

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <unordered_map>
#include <unordered_set>

using namespace search;

std::atomic<bool> g_shutdown_requested{false};

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        Logger::info("Shutdown requested...");
        g_shutdown_requested = true;
    }
}

struct CrawlerStats {
    std::atomic<uint64_t> pages_crawled{0};
    std::atomic<uint64_t> pages_failed{0};
    std::atomic<uint64_t> pages_filtered{0};
    std::atomic<uint64_t> bytes_downloaded{0};
    std::atomic<uint64_t> links_extracted{0};
    std::atomic<uint64_t> links_accepted{0};
    std::chrono::steady_clock::time_point start_time;
    
    void start() {
        start_time = std::chrono::steady_clock::now();
    }
    
    void log_progress() const {
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
        
        double rate = seconds > 0 ? 
            static_cast<double>(pages_crawled.load()) / seconds * 60.0 : 0;
        
        Logger::info("========================================");
        Logger::info("[PROGRESS] Pages crawled:  " + std::to_string(pages_crawled.load()));
        Logger::info("[PROGRESS] Pages failed:   " + std::to_string(pages_failed.load()));
        Logger::info("[PROGRESS] Pages filtered: " + std::to_string(pages_filtered.load()));
        Logger::info("[PROGRESS] Bytes: " + std::to_string(bytes_downloaded.load() / 1024) + " KB");
        Logger::info("[PROGRESS] Links found: " + std::to_string(links_extracted.load()) + 
                     ", accepted: " + std::to_string(links_accepted.load()));
        Logger::info("[PROGRESS] Elapsed: " + std::to_string(seconds) + "s");
        Logger::info("[PROGRESS] Rate: " + std::to_string(rate) + " pages/min");
        Logger::info("========================================");
    }
};

// Domain limiter to prevent getting trapped in single sites
class DomainLimiter {
public:
    static constexpr int MAX_PAGES_PER_DOMAIN = 50;  // Max pages from any single domain
    
    bool can_crawl(const std::string& domain) {
        std::lock_guard<std::mutex> lock(mutex_);
        return domain_counts_[domain] < MAX_PAGES_PER_DOMAIN;
    }
    
    void record_crawl(const std::string& domain) {
        std::lock_guard<std::mutex> lock(mutex_);
        domain_counts_[domain]++;
    }
    
    int get_count(const std::string& domain) {
        std::lock_guard<std::mutex> lock(mutex_);
        return domain_counts_[domain];
    }
    
    void log_top_domains(int n = 10) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::vector<std::pair<std::string, int>> sorted;
        for (const auto& [domain, count] : domain_counts_) {
            sorted.emplace_back(domain, count);
        }
        
        std::sort(sorted.begin(), sorted.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });
        
        Logger::info("Top " + std::to_string(n) + " domains:");
        for (int i = 0; i < std::min(n, static_cast<int>(sorted.size())); ++i) {
            Logger::info("  " + sorted[i].first + ": " + std::to_string(sorted[i].second));
        }
    }

private:
    std::unordered_map<std::string, int> domain_counts_;
    std::mutex mutex_;
};

// Thread-safe robots.txt cache
class RobotsCache {
public:
    bool is_allowed(const std::string& url, const std::string& domain, HTTPFetcher& fetcher) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (cache_.find(domain) == cache_.end()) {
            cache_.try_emplace(domain);
            std::string robots_url = "https://" + domain + "/robots.txt";
            std::string robots_content;
            if (fetcher.fetch(robots_url, robots_content)) {
                cache_[domain].parse(robots_content);
            }
        }
        
        return cache_[domain].is_allowed(url, "BloomSearchBot");
    }
    
    std::chrono::milliseconds get_crawl_delay(const std::string& domain) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cache_.find(domain);
        if (it != cache_.end()) {
            int delay = it->second.get_crawl_delay();
            if (delay > 0) {
                return std::chrono::milliseconds(delay * 1000);
            }
        }
        return std::chrono::milliseconds(1000);  // Default 1 second
    }

private:
    std::unordered_map<std::string, RobotsParser> cache_;
    std::mutex mutex_;
};

void crawler_worker(
    int worker_id,
    URLFrontierMT& frontier,
    RobotsCache& robots_cache,
    DomainLimiter& domain_limiter,
    PostgresClient& db,
    S3Client& s3,
    CrawlerStats& stats
) {
    HTTPFetcher fetcher;
    HTMLParser parser;
    
    Logger::info("[Worker " + std::to_string(worker_id) + "] Started");
    
    while (!g_shutdown_requested && !frontier.is_shutdown()) {
        auto url_opt = frontier.pop();
        if (!url_opt) {
            break;
        }
        
        std::string url = *url_opt;
        std::string domain = URLFrontierMT::extract_domain(url);
        
        // Check URL filter
        if (!URLFilter::is_acceptable(url)) {
            stats.pages_filtered++;
            continue;
        }
        
        // Check domain limit
        if (!domain_limiter.can_crawl(domain)) {
            stats.pages_filtered++;
            continue;
        }
        
        // Check robots.txt
        if (!robots_cache.is_allowed(url, domain, fetcher)) {
            stats.pages_filtered++;
            continue;
        }
        
        auto crawl_delay = robots_cache.get_crawl_delay(domain);
        frontier.set_domain_delay(domain, crawl_delay);
        
        // Fetch the page
        std::string content;
        bool fetch_success = fetcher.fetch(url, content);
        
        frontier.mark_domain_crawled(domain);
        
        if (!fetch_success || content.empty()) {
            stats.pages_failed++;
            db.mark_crawled(url, 0, "", 0);
            continue;
        }
        
        // Parse HTML
        auto doc = parser.parse(content, url);
        
        // Store content in S3
        std::string content_hash = std::to_string(std::hash<std::string>{}(content));
        
        if (!s3.put(content_hash, content)) {
            Logger::info("[Worker " + std::to_string(worker_id) + "] S3 store failed: " + url);
            stats.pages_failed++;
            continue;
        }
        
        // Store metadata in DB
        if (!db.mark_crawled(url, 200, content_hash, content.size())) {
            Logger::info("[Worker " + std::to_string(worker_id) + "] DB update failed: " + url);
            stats.pages_failed++;
            continue;
        }
        
        // Record domain crawl
        domain_limiter.record_crawl(domain);
        
        // Extract and filter links
        std::vector<std::string> new_urls;
        for (const auto& link : doc.links) {
            if (link.url.empty()) continue;
            if (!link.url.starts_with("http://") && !link.url.starts_with("https://")) continue;
            
            // Apply URL filter to outgoing links too
            if (URLFilter::is_acceptable(link.url)) {
                new_urls.push_back(link.url);
                stats.links_accepted++;
            }
        }
        
        stats.links_extracted += doc.links.size();
        frontier.add_batch(new_urls, 0);
        
        stats.pages_crawled++;
        stats.bytes_downloaded += content.size();
    }
    
    Logger::info("[Worker " + std::to_string(worker_id) + "] Stopped");
}

void print_usage(const char* program) {
    std::cout << "Usage: " << program << " [options]\n"
              << "\nOptions:\n"
              << "  -s, --seeds FILE       File containing seed URLs (one per line)\n"
              << "  -u, --url URL          Add a single seed URL\n"
              << "  -n, --max-pages N      Stop after crawling N pages\n"
              << "  -t, --threads N        Number of worker threads (default: 4)\n"
              << "  -d, --delay MS         Default delay between requests in ms (default: 1000)\n"
              << "  --test                 Run in test mode (verify connections)\n"
              << "  -h, --help             Show this help message\n"
              << "\nEnvironment variables:\n"
              << "  POSTGRES_CONNECTION    PostgreSQL connection string\n"
              << "  S3_ENDPOINT            MinIO/S3 endpoint URL\n"
              << "  S3_ACCESS_KEY          MinIO/S3 access key\n"
              << "  S3_SECRET_KEY          MinIO/S3 secret key\n"
              << "  S3_BUCKET              S3 bucket name\n"
              << std::endl;
}

int main(int argc, char** argv) {
    std::vector<std::string> seed_urls;
    std::string seeds_file;
    int64_t max_pages = -1;
    int num_threads = 4;
    int default_delay_ms = 1000;
    bool test_mode = false;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--test") {
            test_mode = true;
        } else if ((arg == "-s" || arg == "--seeds") && i + 1 < argc) {
            seeds_file = argv[++i];
        } else if ((arg == "-u" || arg == "--url") && i + 1 < argc) {
            seed_urls.push_back(argv[++i]);
        } else if ((arg == "-n" || arg == "--max-pages") && i + 1 < argc) {
            max_pages = std::stoll(argv[++i]);
        } else if ((arg == "-t" || arg == "--threads") && i + 1 < argc) {
            num_threads = std::stoi(argv[++i]);
        } else if ((arg == "-d" || arg == "--delay") && i + 1 < argc) {
            default_delay_ms = std::stoi(argv[++i]);
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            print_usage(argv[0]);
            return 1;
        }
    }
    
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "    Bloom Search - Multi-threaded Crawler                   \n";
    std::cout << "============================================================\n\n";
    
    try {
        Logger::info("Starting Bloom Search Crawler");
        Logger::info("Version 1.0.0");
        
        if (test_mode) {
            Logger::info("Running in test mode");
            
            PostgresClient db;
            if (!db.connect()) {
                Logger::error("PostgreSQL connection failed");
                return 1;
            }
            Logger::info("PostgreSQL: OK");
            
            S3Client s3;
            const char* s3_endpoint = std::getenv("S3_ENDPOINT");
            const char* s3_access_key = std::getenv("S3_ACCESS_KEY");
            const char* s3_secret_key = std::getenv("S3_SECRET_KEY");
            const char* s3_bucket = std::getenv("S3_BUCKET");
            
            if (!s3.connect(
                s3_endpoint ? s3_endpoint : "http://minio:9000",
                s3_access_key ? s3_access_key : "minioadmin",
                s3_secret_key ? s3_secret_key : "minioadmin",
                s3_bucket ? s3_bucket : "bloom-content"
            )) {
                Logger::error("S3/MinIO connection failed");
                return 1;
            }
            Logger::info("S3/MinIO: OK");
            
            Logger::info("Test passed!");
            return 0;
        }
        
        // Load seeds from file
        if (!seeds_file.empty()) {
            std::ifstream file(seeds_file);
            if (file.is_open()) {
                std::string line;
                while (std::getline(file, line)) {
                    size_t start = line.find_first_not_of(" \t\r\n");
                    if (start == std::string::npos) continue;
                    size_t end = line.find_last_not_of(" \t\r\n");
                    line = line.substr(start, end - start + 1);
                    
                    if (!line.empty() && line[0] != '#') {
                        seed_urls.push_back(line);
                    }
                }
                file.close();
            } else {
                Logger::error("Failed to open seeds file: " + seeds_file);
                return 1;
            }
        }
        
        // Default diverse seed list
        if (seed_urls.empty()) {
            seed_urls = {
                // General knowledge
                "https://www.wikipedia.org/",
                "https://www.britannica.com/",
                
                // Tech news & blogs
                "https://news.ycombinator.com/",
                "https://www.wired.com/",
                "https://arstechnica.com/",
                "https://www.theverge.com/",
                
                // Programming
                "https://stackoverflow.com/",
                "https://dev.to/",
                "https://www.freecodecamp.org/",
                
                // News
                "https://www.reuters.com/",
                "https://www.bbc.com/news",
                "https://www.npr.org/",
                
                // Science
                "https://www.nature.com/",
                "https://www.sciencedaily.com/",
                
                // Reference
                "https://www.merriam-webster.com/",
                "https://www.investopedia.com/"
            };
        }
        
        Logger::info("Seed URLs: " + std::to_string(seed_urls.size()));
        Logger::info("Initializing components...");
        
        // PostgreSQL
        PostgresClient db;
        if (!db.connect()) {
            Logger::error("Failed to connect to PostgreSQL: " + db.last_error());
            return 1;
        }
        db.initialize_schema();
        Logger::info("Connected to PostgreSQL");
        
        // S3
        S3Client s3;
        const char* s3_endpoint = std::getenv("S3_ENDPOINT");
        const char* s3_access_key = std::getenv("S3_ACCESS_KEY");
        const char* s3_secret_key = std::getenv("S3_SECRET_KEY");
        const char* s3_bucket = std::getenv("S3_BUCKET");
        
        if (!s3.connect(
            s3_endpoint ? s3_endpoint : "http://minio:9000",
            s3_access_key ? s3_access_key : "minioadmin",
            s3_secret_key ? s3_secret_key : "minioadmin",
            s3_bucket ? s3_bucket : "bloom-content"
        )) {
            Logger::error("Failed to connect to S3/MinIO: " + s3.last_error());
            return 1;
        }
        Logger::info("Connected to MinIO/S3");
        
        // URL Frontier (thread-safe)
        URLFrontierMT frontier;

        frontier.set_default_delay(std::chrono::milliseconds(default_delay_ms));
        
        // Load saved frontier if exists (for resuming interrupted crawls)
        auto saved_frontier = db.load_frontier();
        if (!saved_frontier.empty()) {
            std::vector<std::string> urls;
            for (const auto& entry : saved_frontier) {
                urls.push_back(entry.url);
            }
            frontier.import_urls(urls);
            db.clear_frontier();
            Logger::info("Resumed crawl with " + std::to_string(urls.size()) + " URLs from saved frontier");
        }
        
        // Add seed URLs with high priority (only if we have new seeds)
        if (!seed_urls.empty() && seed_urls[0] != "https://www.wikipedia.org/") {
            for (const auto& url : seed_urls) {
                frontier.add(url, 100);
            }
            Logger::info("Added " + std::to_string(seed_urls.size()) + " seed URLs");
        } else if (saved_frontier.empty()) {
            // Only add defaults if no saved frontier and no explicit seeds
            for (const auto& url : seed_urls) {
                frontier.add(url, 100);
            }
            Logger::info("Added " + std::to_string(seed_urls.size()) + " seed URLs");
        }

        
        // Shared resources
        RobotsCache robots_cache;
        DomainLimiter domain_limiter;
        CrawlerStats stats;
        stats.start();
        
        Logger::info("Configuration:");
        Logger::info("  Threads: " + std::to_string(num_threads));
        Logger::info("  Max pages: " + (max_pages > 0 ? std::to_string(max_pages) : "unlimited"));
        Logger::info("  Default delay: " + std::to_string(default_delay_ms) + "ms");
        Logger::info("  Max per domain: " + std::to_string(DomainLimiter::MAX_PAGES_PER_DOMAIN));
        Logger::info("  Max URL length: " + std::to_string(URLFilter::MAX_URL_LENGTH));
        
        // Start worker threads
        std::vector<std::thread> workers;
        for (int i = 0; i < num_threads; ++i) {
            workers.emplace_back(
                crawler_worker,
                i,
                std::ref(frontier),
                std::ref(robots_cache),
                std::ref(domain_limiter),
                std::ref(db),
                std::ref(s3),
                std::ref(stats)
            );
        }
        
        Logger::info("Started " + std::to_string(num_threads) + " worker threads");
        
        // Monitor progress
        int progress_interval = 30;  // seconds
        while (!g_shutdown_requested) {
            std::this_thread::sleep_for(std::chrono::seconds(progress_interval));
            
            if (g_shutdown_requested) break;
            
            stats.log_progress();
            
            // Check max pages
            if (max_pages > 0 && stats.pages_crawled.load() >= static_cast<uint64_t>(max_pages)) {
                Logger::info("Reached max pages limit (" + std::to_string(max_pages) + ")");
                frontier.shutdown();
                break;
            }
            
            // Check if frontier is empty
            if (frontier.empty() && stats.pages_crawled.load() > 0) {
                Logger::info("Frontier empty, shutting down...");
                frontier.shutdown();
                break;
            }
        }
        
        Logger::info("Shutting down...");
        
        // Save frontier state before shutdown (for resuming later)
        auto pending = frontier.export_pending();
        if (!pending.empty()) {
            Logger::info("Saving " + std::to_string(pending.size()) + " pending URLs to database...");
            std::vector<FrontierEntry> entries;
            for (const auto& url : pending) {
                FrontierEntry entry;
                entry.url = url;
                entry.domain = URLFrontierMT::extract_domain(url);
                entry.priority = 0;
                entries.push_back(entry);
            }
            if (db.save_frontier(entries)) {
                Logger::info("Frontier saved successfully - crawl can be resumed");
            } else {
                Logger::error("Failed to save frontier: " + db.last_error());
            }
        }
        
        frontier.shutdown();
        
        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        
        // Final stats
        Logger::info("Crawler stopped");
        stats.log_progress();
        domain_limiter.log_top_domains();
        
    } catch (const std::exception& e) {
        Logger::error("Fatal error: " + std::string(e.what()));
        return 1;
    }
    
    return 0;
}