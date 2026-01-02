#include "crawler/url_frontier_mt.h"
#include <algorithm>
#include <cctype>
#include <regex>

namespace search {

URLFrontierMT::URLFrontierMT() = default;

void URLFrontierMT::set_default_delay(std::chrono::milliseconds delay) {
    std::lock_guard<std::mutex> lock(domain_mutex_);
    default_delay_ = delay;
}

void URLFrontierMT::set_domain_delay(const std::string& domain, std::chrono::milliseconds delay) {
    std::lock_guard<std::mutex> lock(domain_mutex_);
    domain_delays_[domain] = delay;
}

std::string URLFrontierMT::normalize_url(const std::string& url) {
    if (url.empty()) return "";
    
    std::string normalized = url;
    
    size_t hash_pos = normalized.find('#');
    if (hash_pos != std::string::npos) {
        normalized = normalized.substr(0, hash_pos);
    }
    
    size_t scheme_end = normalized.find("://");
    if (scheme_end == std::string::npos) return "";
    
    std::string scheme = normalized.substr(0, scheme_end);
    std::transform(scheme.begin(), scheme.end(), scheme.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    
    if (scheme != "http" && scheme != "https") return "";
    
    size_t host_start = scheme_end + 3;
    size_t path_start = normalized.find('/', host_start);
    
    std::string host;
    std::string path_and_query;
    
    if (path_start == std::string::npos) {
        host = normalized.substr(host_start);
        path_and_query = "/";
    } else {
        host = normalized.substr(host_start, path_start - host_start);
        path_and_query = normalized.substr(path_start);
    }
    
    std::transform(host.begin(), host.end(), host.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    
    if (scheme == "http" && host.size() > 3) {
        size_t port_pos = host.rfind(":80");
        if (port_pos != std::string::npos && port_pos == host.size() - 3) {
            host = host.substr(0, port_pos);
        }
    } else if (scheme == "https" && host.size() > 4) {
        size_t port_pos = host.rfind(":443");
        if (port_pos != std::string::npos && port_pos == host.size() - 4) {
            host = host.substr(0, port_pos);
        }
    }
    
    if (path_and_query == "/") {
        // Keep it
    } else if (!path_and_query.empty() && path_and_query.back() == '/' &&
               path_and_query.find('?') == std::string::npos) {
        path_and_query.pop_back();
    }
    
    return scheme + "://" + host + path_and_query;
}

std::string URLFrontierMT::extract_domain(const std::string& url) {
    size_t scheme_end = url.find("://");
    if (scheme_end == std::string::npos) return "";
    
    size_t host_start = scheme_end + 3;
    size_t host_end = url.find('/', host_start);
    
    std::string host;
    if (host_end == std::string::npos) {
        host = url.substr(host_start);
    } else {
        host = url.substr(host_start, host_end - host_start);
    }
    
    size_t port_pos = host.find(':');
    if (port_pos != std::string::npos) {
        host = host.substr(0, port_pos);
    }
    
    std::transform(host.begin(), host.end(), host.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    
    return host;
}

bool URLFrontierMT::add(const std::string& url, int priority) {
    std::string normalized = normalize_url(url);
    if (normalized.empty()) return false;
    
    std::string domain = extract_domain(normalized);
    if (domain.empty()) return false;
    
    {
        std::lock_guard<std::mutex> lock(seen_mutex_);
        if (seen_.count(normalized)) {
            stats_.duplicates_rejected++;
            return false;
        }
        seen_.insert(normalized);
    }
    
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        PrioritizedURL purl;
        purl.url = normalized;
        purl.domain = domain;
        purl.priority = priority;
        purl.added_at = std::chrono::steady_clock::now();
        queue_.push(std::move(purl));
        stats_.urls_added++;
    }
    
    queue_cv_.notify_one();
    return true;
}

bool URLFrontierMT::add_batch(const std::vector<std::string>& urls, int priority) {
    bool any_added = false;
    for (const auto& url : urls) {
        if (add(url, priority)) {
            any_added = true;
        }
    }
    return any_added;
}

std::optional<std::string> URLFrontierMT::pop() {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    
    while (true) {
        if (shutdown_) {
            return std::nullopt;
        }
        
        if (queue_.empty()) {
            queue_cv_.wait(lock);
            continue;
        }
        
        PrioritizedURL purl = queue_.top();
        queue_.pop();
        lock.unlock();
        wait_for_domain(purl.domain);
        
        stats_.urls_popped++;
        return purl.url;
    }
}

std::chrono::milliseconds URLFrontierMT::get_domain_delay(const std::string& domain) {
    std::lock_guard<std::mutex> lock(domain_mutex_);
    auto it = domain_delays_.find(domain);
    if (it != domain_delays_.end()) {
        return it->second;
    }
    return default_delay_;
}

std::chrono::steady_clock::time_point URLFrontierMT::get_domain_last_access(const std::string& domain) {
    std::lock_guard<std::mutex> lock(domain_mutex_);
    auto it = domain_last_access_.find(domain);
    if (it != domain_last_access_.end()) {
        return it->second;
    }
    // Never accessed - return epoch
    return std::chrono::steady_clock::time_point{};
}

void URLFrontierMT::mark_domain_crawled(const std::string& domain) {
    std::lock_guard<std::mutex> lock(domain_mutex_);
    domain_last_access_[domain] = std::chrono::steady_clock::now();
}

bool URLFrontierMT::can_crawl_domain(const std::string& domain) {
    auto delay = get_domain_delay(domain);
    auto last_access = get_domain_last_access(domain);
    auto now = std::chrono::steady_clock::now();
    
    return (now - last_access) >= delay;
}

void URLFrontierMT::wait_for_domain(const std::string& domain) {
    auto delay = get_domain_delay(domain);
    auto last_access = get_domain_last_access(domain);
    auto now = std::chrono::steady_clock::now();
    
    auto elapsed = now - last_access;
    if (elapsed < delay) {
        auto wait_time = delay - std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
        std::this_thread::sleep_for(wait_time);
    }
}

bool URLFrontierMT::empty() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return queue_.empty();
}

size_t URLFrontierMT::size() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return queue_.size();
}

size_t URLFrontierMT::seen_count() const {
    std::lock_guard<std::mutex> lock(seen_mutex_);
    return seen_.size();
}

void URLFrontierMT::shutdown() {
    shutdown_ = true;
    queue_cv_.notify_all();
}

bool URLFrontierMT::is_shutdown() const {
    return shutdown_;
}

} // namespace search