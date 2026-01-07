#include "crawler/http_fetcher.h"
#include "crawler/robots_parser.h"
#include "crawler/url_frontier.h"
#include "storage/postgres_client.h"
#include "storage/s3_client.h"
#include "common/logger.h"

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <atomic>
#include <csignal>
#include <regex>
#include <sstream>

namespace search {

std::atomic<bool> g_shutdown_requested{false};

void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        std::cout << "\n[CRAWLER] Shutdown requested (signal " << signum << ")...\n";
        g_shutdown_requested = true;
    }
}

class Crawler {
public:
    Crawler() = default;
    
    bool initialize();
    void run(int max_pages = 100);
    void shutdown();
    
    int pages_crawled() const { return pages_crawled_; }
    int pages_failed() const { return pages_failed_; }
    
private:
    HTTPFetcher fetcher_;
    RobotsParser robots_;
    URLFrontier frontier_;
    PostgresClient db_;
    S3Client storage_;
    
    int pages_crawled_ = 0;
    int pages_failed_ = 0;
    std::chrono::steady_clock::time_point start_time_;
    
    std::string user_agent_ = "BloomSearchBot/1.0";
    int default_crawl_delay_ms_ = 1000;  // 1 second default politeness
    
    bool crawl_url(const std::string& url);
    std::vector<std::string> extract_links(const std::string& html, const std::string& base_url);
    std::string normalize_url(const std::string& url, const std::string& base_url);
    std::string get_domain(const std::string& url);
    void log_progress();
    void load_seed_urls();
};

bool Crawler::initialize() {
    std::cout << "[CRAWLER] Initializing components...\n";
    
    std::cout << "[CRAWLER] HTTPFetcher configured\n";
    
    if (!db_.connect()) {
        std::cerr << "[CRAWLER] ERROR: Failed to connect to PostgreSQL\n";
        return false;
    }
    std::cout << "[CRAWLER] PostgreSQL connected\n";
    
    if (!db_.initialize_schema()) {
        std::cerr << "[CRAWLER] ERROR: Failed to initialize database schema\n";
        return false;
    }
    std::cout << "[CRAWLER] Database schema initialized\n";
    
    if (!storage_.connect()) {
        std::cerr << "[CRAWLER] ERROR: Failed to connect to MinIO: " << storage_.last_error() << "\n";
        return false;
    }
    std::cout << "[CRAWLER] MinIO/S3 connected\n";
    
    load_seed_urls();
    
    std::cout << "[CRAWLER] Initialization complete. Frontier size: " << frontier_.pending_count() << "\n";
    return true;
}

void Crawler::load_seed_urls() {
    std::vector<std::string> seeds = {
        "https://example.com",
        "https://www.wikipedia.org",
        "https://news.ycombinator.com"
    };
    
    const char* env_seeds = std::getenv("SEED_URLS");
    if (env_seeds) {
        seeds.clear();
        std::istringstream iss(env_seeds);
        std::string url;
        while (std::getline(iss, url, ',')) {
            if (!url.empty()) {
                seeds.push_back(url);
            }
        }
    }
    
    for (const auto& url : seeds) {
        frontier_.add(url, URLFrontier::Priority::SEED);
        std::cout << "[CRAWLER] Seed URL: " << url << "\n";
    }
}

void Crawler::run(int max_pages) {
    std::cout << "[CRAWLER] Starting crawl (max " << max_pages << " pages)...\n";
    start_time_ = std::chrono::steady_clock::now();
    
    while (!g_shutdown_requested && pages_crawled_ < max_pages) {
        auto next_url = frontier_.get_next();
        
        if (!next_url.has_value()) {
            if (frontier_.empty()) {
                std::cout << "[CRAWLER] Frontier empty, stopping.\n";
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        
        if (crawl_url(*next_url)) {
            pages_crawled_++;
        } else {
            pages_failed_++;
        }
        
        if ((pages_crawled_ + pages_failed_) % 10 == 0) {
            log_progress();
        }
    }
    
    log_progress();
    
    if (g_shutdown_requested) {
        std::cout << "[CRAWLER] Shutdown requested, saving state...\n";
    }
}

bool Crawler::crawl_url(const std::string& url) {
    std::string domain = get_domain(url);
    
    if (!robots_.is_allowed_url(url, fetcher_, user_agent_)) {
        std::cout << "[CRAWLER] Blocked by robots.txt: " << url << "\n";
        return false;
    }
    
    int crawl_delay = robots_.get_crawl_delay_for(url, fetcher_, user_agent_);
    if (crawl_delay <= 0) {
        crawl_delay = default_crawl_delay_ms_ / 1000;
    }
    
    frontier_.set_crawl_delay(domain, crawl_delay * 1000);
    
    std::string content;
    if (!fetcher_.fetch(url, content)) {
        std::cout << "[CRAWLER] Fetch failed: " << url << "\n";
        return false;
    }
    
    int status_code = 200;

    std::string content_key = S3Client::url_to_key(url);
    if (!storage_.put(content_key, content, true)) {
        std::cerr << "[CRAWLER] Failed to store content: " << storage_.last_error() << "\n";
    }
    
    PageRecord record;
    record.url = url;
    record.url_hash = content_key;
    record.domain = domain;
    record.status_code = status_code;
    record.content_size = static_cast<int64_t>(content.size());
    record.crawled_at = std::chrono::system_clock::now();
    
    if (!db_.upsert_page(record)) {
        std::cerr << "[CRAWLER] Failed to store metadata\n";
    }
    
    auto links = extract_links(content, url);
    int new_links = 0;
    for (const auto& link : links) {
        if (frontier_.add(link, URLFrontier::Priority::NORMAL)) {
            new_links++;
        }
    }
    
    std::cout << "[CRAWLER] OK: " << url << " (" << content.size() << " bytes, " 
              << new_links << " new links)\n";
    
    return true;
}

std::vector<std::string> Crawler::extract_links(const std::string& html, const std::string& base_url) {
    std::vector<std::string> links;
    
    // Simple regex to find href attributes
    // Note: A production crawler would use a proper HTML parser
    std::regex href_regex(R"(href\s*=\s*["']([^"']+)["'])", std::regex::icase);
    
    auto begin = std::sregex_iterator(html.begin(), html.end(), href_regex);
    auto end = std::sregex_iterator();
    
    for (auto it = begin; it != end; ++it) {
        std::string href = (*it)[1].str();
        
        if (href.empty() || href[0] == '#' || 
            href.starts_with("javascript:") ||
            href.starts_with("mailto:")) {
            continue;
        }
        
        std::string absolute_url = normalize_url(href, base_url);
        
        if (!absolute_url.empty()) {
            links.push_back(absolute_url);
        }
    }
    
    return links;
}

std::string Crawler::normalize_url(const std::string& url, const std::string& base_url) {
    if (url.starts_with("http://") || url.starts_with("https://")) {
        return url;
    }
    
    std::string base_scheme, base_host, base_path;
    
    auto scheme_end = base_url.find("://");
    if (scheme_end == std::string::npos) return "";
    
    base_scheme = base_url.substr(0, scheme_end);
    size_t host_start = scheme_end + 3;
    size_t path_start = base_url.find('/', host_start);
    
    if (path_start == std::string::npos) {
        base_host = base_url.substr(host_start);
        base_path = "/";
    } else {
        base_host = base_url.substr(host_start, path_start - host_start);
        base_path = base_url.substr(path_start);
    }
    
    if (url.starts_with("//")) {
        return base_scheme + ":" + url;
    }
    
    if (url[0] == '/') {
        return base_scheme + "://" + base_host + url;
    }
    
    size_t last_slash = base_path.rfind('/');
    std::string base_dir = (last_slash != std::string::npos) 
                           ? base_path.substr(0, last_slash + 1) 
                           : "/";
    
    return base_scheme + "://" + base_host + base_dir + url;
}

std::string Crawler::get_domain(const std::string& url) {
    auto scheme_end = url.find("://");
    if (scheme_end == std::string::npos) return url;
    
    size_t host_start = scheme_end + 3;
    size_t host_end = url.find('/', host_start);
    
    if (host_end == std::string::npos) {
        return url.substr(host_start);
    }
    return url.substr(host_start, host_end - host_start);
}

void Crawler::log_progress() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count();
    
    double pages_per_min = (elapsed > 0) ? (pages_crawled_ * 60.0 / elapsed) : 0;
    
    std::cout << "\n========================================\n";
    std::cout << "[PROGRESS] Pages crawled: " << pages_crawled_ << "\n";
    std::cout << "[PROGRESS] Pages failed:  " << pages_failed_ << "\n";
    std::cout << "[PROGRESS] Queue size:    " << frontier_.pending_count() << "\n";
    std::cout << "[PROGRESS] Elapsed:       " << elapsed << "s\n";
    std::cout << "[PROGRESS] Rate:          " << std::fixed << std::setprecision(1) 
              << pages_per_min << " pages/min\n";
    std::cout << "========================================\n\n";
}

void Crawler::shutdown() {
    std::cout << "[CRAWLER] Shutting down...\n";
    
    std::cout << "[CRAWLER] URLs remaining in frontier: " << frontier_.pending_count() << "\n";
    
    std::cout << "[CRAWLER] Shutdown complete.\n";
}

} // namespace search

int main(int argc, char* argv[]) {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "    Bloom Search Crawler v0.1                               \n";
    std::cout << "============================================================\n\n";
    
    // Parse command line args
    int max_pages = 100;
    if (argc > 1) {
        max_pages = std::atoi(argv[1]);
        if (max_pages <= 0) max_pages = 100;
    }
    
    // Set up signal handlers
    std::signal(SIGINT, search::signal_handler);
    std::signal(SIGTERM, search::signal_handler);
    
    // Create and run crawler
    search::Crawler crawler;
    
    if (!crawler.initialize()) {
        std::cerr << "[CRAWLER] Initialization failed!\n";
        return 1;
    }
    
    crawler.run(max_pages);
    crawler.shutdown();
    
    std::cout << "\n[CRAWLER] Final stats: " << crawler.pages_crawled() << " crawled, "
              << crawler.pages_failed() << " failed\n\n";
    
    return 0;
}