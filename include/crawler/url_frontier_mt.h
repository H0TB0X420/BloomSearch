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

struct PrioritizedURL {
    std::string url;
    std::string domain;
    int priority;
    std::chrono::steady_clock::time_point added_at;
    
    // For priority queue (max-heap, so invert comparison)
    bool operator<(const PrioritizedURL& other) const {
        return priority < other.priority;
    }
};

class URLFrontierMT {
public:
    URLFrontierMT();
    ~URLFrontierMT() = default;
    
    void set_default_delay(std::chrono::milliseconds delay);
    void set_domain_delay(const std::string& domain, std::chrono::milliseconds delay);
    
    bool add(const std::string& url, int priority = 0);
    bool add_batch(const std::vector<std::string>& urls, int priority = 0);
    
    std::optional<std::string> pop();
    
    bool empty() const;
    size_t size() const;
    size_t seen_count() const;
    
    void mark_domain_crawled(const std::string& domain);
    
    void shutdown();
    bool is_shutdown() const;
    
    struct Stats {
        std::atomic<uint64_t> urls_added{0};
        std::atomic<uint64_t> urls_popped{0};
        std::atomic<uint64_t> duplicates_rejected{0};
    };
    const Stats& stats() const { return stats_; }
    
    static std::string normalize_url(const std::string& url);
    static std::string extract_domain(const std::string& url);

private:
    std::priority_queue<PrioritizedURL> queue_;
    
    std::unordered_set<std::string> seen_;
    
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> domain_last_access_;
    
    std::unordered_map<std::string, std::chrono::milliseconds> domain_delays_;
    std::chrono::milliseconds default_delay_{1000};
    
    mutable std::mutex queue_mutex_;
    mutable std::mutex seen_mutex_;
    mutable std::mutex domain_mutex_;
    std::condition_variable queue_cv_;
    
    std::atomic<bool> shutdown_{false};
    
    Stats stats_;
    
    bool is_seen_locked(const std::string& normalized_url);
    void mark_seen_locked(const std::string& normalized_url);
    std::chrono::milliseconds get_domain_delay(const std::string& domain);
    std::chrono::steady_clock::time_point get_domain_last_access(const std::string& domain);
    bool can_crawl_domain(const std::string& domain);
    void wait_for_domain(const std::string& domain);
};

} // namespace search