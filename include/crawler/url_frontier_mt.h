#pragma once

#include <string>
#include <queue>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <atomic>

namespace search {

//=============================================================================
// URL with priority for queue ordering
//=============================================================================
struct PrioritizedURL {
    std::string url;
    std::string domain;
    int priority;  // Higher = more important
    std::chrono::steady_clock::time_point added_at;
    
    // For priority queue (max-heap, so invert comparison)
    bool operator<(const PrioritizedURL& other) const {
        return priority < other.priority;  // Higher priority first
    }
};

//=============================================================================
// Thread-safe URL Frontier
// Manages crawl queue with deduplication and per-domain rate limiting
//=============================================================================
class URLFrontierMT {
public:
    URLFrontierMT();
    ~URLFrontierMT() = default;
    
    // Configuration
    void set_default_delay(std::chrono::milliseconds delay);
    void set_domain_delay(const std::string& domain, std::chrono::milliseconds delay);
    
    // Add URLs (thread-safe)
    bool add(const std::string& url, int priority = 0);
    bool add_batch(const std::vector<std::string>& urls, int priority = 0);
    
    // Get next URL to crawl (blocks if queue empty, returns nullopt on shutdown)
    std::optional<std::string> pop();
    
    // Non-blocking check
    bool empty() const;
    size_t size() const;
    size_t seen_count() const;
    
    // Mark domain as crawled (updates last access time for rate limiting)
    void mark_domain_crawled(const std::string& domain);
    
    // Shutdown signal
    void shutdown();
    bool is_shutdown() const;
    
    // Statistics
    struct Stats {
        std::atomic<uint64_t> urls_added{0};
        std::atomic<uint64_t> urls_popped{0};
        std::atomic<uint64_t> duplicates_rejected{0};
    };
    const Stats& stats() const { return stats_; }
    
    // URL normalization (public for testing)
    static std::string normalize_url(const std::string& url);
    static std::string extract_domain(const std::string& url);

private:
    // The priority queue
    std::priority_queue<PrioritizedURL> queue_;
    
    // Deduplication set
    std::unordered_set<std::string> seen_;
    
    // Per-domain last access time (for rate limiting)
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> domain_last_access_;
    
    // Per-domain crawl delays
    std::unordered_map<std::string, std::chrono::milliseconds> domain_delays_;
    std::chrono::milliseconds default_delay_{1000};  // 1 second default
    
    // Thread synchronization
    mutable std::mutex queue_mutex_;
    mutable std::mutex seen_mutex_;
    mutable std::mutex domain_mutex_;
    std::condition_variable queue_cv_;
    
    // Shutdown flag
    std::atomic<bool> shutdown_{false};
    
    // Stats
    Stats stats_;
    
    // Internal helpers
    bool is_seen_locked(const std::string& normalized_url);
    void mark_seen_locked(const std::string& normalized_url);
    std::chrono::milliseconds get_domain_delay(const std::string& domain);
    std::chrono::steady_clock::time_point get_domain_last_access(const std::string& domain);
    bool can_crawl_domain(const std::string& domain);
    void wait_for_domain(const std::string& domain);
};

} // namespace search