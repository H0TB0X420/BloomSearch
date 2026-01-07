#pragma once

#include <string>
#include <optional>
#include <queue>
#include <unordered_set>
#include <unordered_map>
#include <chrono>

namespace search {

class URLNormalizer {
public:
    static std::string normalize(const std::string& url);
    
    struct URLComponents {
        std::string scheme;    // "http" or "https"
        std::string host;      // "example.com"
        int port = -1;         // -1 means not specified
        std::string path;      // "/page/file.html"
        std::string query;     // "q=search" (without ?)
    };
    
    static std::optional<URLComponents> parse(const std::string& url);
    static std::string build(const URLComponents& components);    
    static std::string extract_domain(const std::string& url);

private:
    static std::string decode_unreserved(const std::string& str);
};

class URLFrontier {
public:
    URLFrontier() = default;
    
    // Priority levels (lower = higher priority)
    enum class Priority : int {
        SEED = 0,       // Starting URLs
        HIGH = 1,       // Homepage, important pages
        NORMAL = 2,     // Regular pages
        LOW = 3         // Deep links, pagination
    };
    
    bool add(const std::string& url, Priority priority = Priority::NORMAL);
    
    size_t add_batch(const std::vector<std::string>& urls, Priority priority = Priority::NORMAL);
    
    std::optional<std::string> get_next();
    
    void set_crawl_delay(const std::string& domain, int delay_ms);
    
    bool load_seeds(const std::string& filepath);
    
    size_t pending_count() const;
    size_t seen_count() const { return seen_urls_.size(); }
    bool empty() const;

    std::vector<std::string> get_all_urls() const {
        std::vector<std::string> urls;
        
        auto queue_copy = queue_;
        while (!queue_copy.empty()) {
            urls.push_back(queue_copy.top().url);
            queue_copy.pop();
        }
        
        return urls;
    }
private:
    struct URLEntry {
        std::string url;
        std::string domain;
        Priority priority;
        
        bool operator<(const URLEntry& other) const {
            return static_cast<int>(priority) > static_cast<int>(other.priority);
        }
    };
    
    std::priority_queue<URLEntry> queue_;
    
    std::unordered_set<std::string> seen_urls_;
    
    struct DomainState {
        std::chrono::steady_clock::time_point next_allowed_time;
        int crawl_delay_ms = -1;  // Default 1 second
    };
    std::unordered_map<std::string, DomainState> domain_states_;
    
    static constexpr int DEFAULT_CRAWL_DELAY_MS = 1000;
    
    bool can_crawl_domain(const std::string& domain) const;
    
    void update_domain_time(const std::string& domain);
};

} // namespace search