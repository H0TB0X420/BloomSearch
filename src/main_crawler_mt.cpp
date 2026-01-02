#include "common/config.h"
#include "common/logger.h"
#include "crawler/http_fetcher.h"
#include "crawler/robots_parser.h"
#include "crawler/url_frontier_mt.h"
#include "indexer/html_parser.h"
#include "storage/postgres_client.h"
#include "storage/s3_client.h"

#include <iostream>
#include <fstream>
#include <thread>
#include <vector>
#include <atomic>
#include <csignal>
#include <chrono>
#include <mutex>
#include <functional>

using namespace search;

std::atomic<bool> g_shutdown_requested{false};

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        Logger::info("Shutdown requested (signal " + std::to_string(signal) + ")");
        g_shutdown_requested = true;
    }
}

struct CrawlerStats {
    std::atomic<uint64_t> pages_crawled{0};
    std::atomic<uint64_t> pages_failed{0};
    std::atomic<uint64_t> bytes_downloaded{0};
    std::atomic<uint64_t> links_extracted{0};
    std::chrono::steady_clock::time_point start_time;
    
    void start() {
        start_time = std::chrono::steady_clock::now();
    }
    
    void log_progress(size_t queue_size) const {
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
        
        double rate = seconds > 0 ? 
            static_cast<double>(pages_crawled) / seconds * 60.0 : 0;
        
        Logger::info("========================================");
        Logger::info("[PROGRESS] Pages crawled: " + std::to_string(pages_crawled.load()));
        Logger::info("[PROGRESS] Pages failed:  " + std::to_string(pages_failed.load()));
        Logger::info("[PROGRESS] Queue size:    " + std::to_string(queue_size));
        Logger::info("[PROGRESS] Elapsed:       " + std::to_string(seconds) + "s");
        Logger::info("[PROGRESS] Rate:          " + std::to_string(rate) + " pages/min");
        Logger::info("========================================");
    }
};

class RobotsCache {
public:
    bool is_allowed(const std::string& url, const std::string& domain, HTTPFetcher& fetcher) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = cache_.find(domain);
            if (it != cache_.end()) {
                return it->second.is_allowed(url);
            }
        }
        
        std::string robots_url = "https://" + domain + "/robots.txt";
        std::string robots_content;
        bool success = fetcher.fetch(robots_url, robots_content);
        
        RobotsParser parser;
        if (success && !robots_content.empty()) {
            parser.parse(robots_content);
        }
        // If fetch fails, assume allowed (be permissive)
        
        {
            std::lock_guard<std::mutex> lock(mutex_);
            cache_[domain] = std::move(parser);
            return cache_[domain].is_allowed(url);
        }
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
    PostgresClient& db,
    S3Client& s3,
    CrawlerStats& stats
) {
    // Each worker has its own fetcher and parser (thread-local)
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
        
        if (!robots_cache.is_allowed(url, domain, fetcher)) {
            continue;
        }
        
        auto crawl_delay = robots_cache.get_crawl_delay(domain);
        frontier.set_domain_delay(domain, crawl_delay);
        
        std::string content;
        bool fetch_success = fetcher.fetch(url, content);
        
        frontier.mark_domain_crawled(domain);
        
        if (!fetch_success || content.empty()) {
            stats.pages_failed++;
            db.mark_crawled(url, 0, "", 0);
            continue;
        }
        
        auto doc = parser.parse(content, url);
        
        std::string content_hash = std::to_string(std::hash<std::string>{}(content));
        
        if (!s3.put(content_hash, content)) {
            Logger::info("[Worker " + std::to_string(worker_id) + "] S3 store failed: " + url);
            stats.pages_failed++;
            continue;
        }
        
        if (!db.mark_crawled(url, 200, content_hash, content.size())) {
            Logger::info("[Worker " + std::to_string(worker_id) + "] DB update failed: " + url);
            stats.pages_failed++;
            continue;
        }
        
        std::vector<std::string> new_urls;
        for (const auto& link : doc.links) {
            if (!link.url.empty() && 
                (link.url.find("http://") == 0 || link.url.find("https://") == 0)) {
                new_urls.push_back(link.url);
            }
        }
        frontier.add_batch(new_urls, 0);
        
        stats.pages_crawled++;
        stats.bytes_downloaded += content.size();
        stats.links_extracted += doc.links.size();
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
    std::cout << "    Bloom Search - Multithreaded Crawler                    \n";
    std::cout << "============================================================\n\n";
    
    try {
        Logger::info("Starting Bloom Search Crawler (MT)");
        Logger::info("Threads: " + std::to_string(num_threads));
        Logger::info("Default delay: " + std::to_string(default_delay_ms) + "ms");
        
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
        
        if (seed_urls.empty()) {
            seed_urls = {
                "https://www.wikipedia.org/",
                "https://news.ycombinator.com/",
                "https://example.com/"
            };
        }
        
        Logger::info("Seed URLs: " + std::to_string(seed_urls.size()));
        Logger::info("Initializing components...");
        
        // PostgreSQL (shared, thread-safe with connection per operation)
        PostgresClient db;
        if (!db.connect()) {
            Logger::error("Failed to connect to PostgreSQL: " + db.last_error());
            return 1;
        }
        db.initialize_schema();
        Logger::info("Connected to PostgreSQL");
        
        // S3 (shared, thread-safe)
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
        Logger::info("Connected to S3/MinIO");
        
        // URL Frontier (thread-safe)
        URLFrontierMT frontier;
        frontier.set_default_delay(std::chrono::milliseconds(default_delay_ms));
        
        // Add seed URLs with high priority
        for (const auto& url : seed_urls) {
            frontier.add(url, 100);
        }
        Logger::info("Added " + std::to_string(seed_urls.size()) + " seed URLs");
        
        RobotsCache robots_cache;
        
        CrawlerStats stats;
        stats.start();
        
        Logger::info("Starting " + std::to_string(num_threads) + " worker threads...");
        
        std::vector<std::thread> workers;
        for (int i = 0; i < num_threads; ++i) {
            workers.emplace_back(crawler_worker, 
                                 i, 
                                 std::ref(frontier),
                                 std::ref(robots_cache),
                                 std::ref(db),
                                 std::ref(s3),
                                 std::ref(stats));
        }
        
        Logger::info("Crawler running. Press Ctrl+C to stop.");
        
        auto last_progress = std::chrono::steady_clock::now();
        
        while (!g_shutdown_requested) {
            std::this_thread::sleep_for(std::chrono::seconds(1));            
            if (max_pages > 0 && stats.pages_crawled >= static_cast<uint64_t>(max_pages)) {
                Logger::info("Reached max pages limit (" + std::to_string(max_pages) + ")");
                break;
            }
            
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - last_progress).count() >= 10) {
                stats.log_progress(frontier.size());
                last_progress = now;
            }
            
            if (frontier.empty() && stats.pages_crawled > 0) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                if (frontier.empty()) {
                    Logger::info("Queue empty, stopping...");
                    break;
                }
            }
        }
        
        Logger::info("Shutting down...");
        frontier.shutdown();
        
        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }        
        stats.log_progress(frontier.size());
        
        Logger::info("Crawler stopped");
        Logger::info("Total pages crawled: " + std::to_string(stats.pages_crawled.load()));
        Logger::info("Total bytes: " + std::to_string(stats.bytes_downloaded.load() / 1024) + " KB");
        
    } catch (const std::exception& e) {
        Logger::error("Fatal error: " + std::string(e.what()));
        return 1;
    }
    
    return 0;
}