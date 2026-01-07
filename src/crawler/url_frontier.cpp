#include "crawler/url_frontier.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <fstream>
#include <string>
#include <string_view>
#include <optional>
#include <algorithm>
#include <ranges>
#include <charconv>

namespace search {

std::string URLNormalizer::decode_unreserved(const std::string& str) {
    // Decode percent-encoded unreserved characters: A-Z a-z 0-9 - . _ ~
    // These are safe to decode per RFC 3986
    std::string result;
    result.reserve(str.size());
    
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '%' && i + 2 < str.size()) {
            // Parse hex digits
            char hex[3] = {str[i + 1], str[i + 2], '\0'};
            char* end;
            long value = std::strtol(hex, &end, 16);
            
            if (end == hex + 2) {  // Successfully parsed 2 hex chars
                char c = static_cast<char>(value);
                // Check if unreserved character
                if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~') {
                    result += c;
                    i += 2;
                    continue;
                }
            }
        }
        result += str[i];
    }
    
    return result;
}

std::optional<URLNormalizer::URLComponents> URLNormalizer::parse(const std::string& url) {
    URLComponents comp;
    
    // 1. Find scheme
    auto scheme_end = url.find("://");
    if (scheme_end == std::string::npos) return std::nullopt;

    comp.scheme = url.substr(0, scheme_end);
    std::ranges::transform(comp.scheme, comp.scheme.begin(), 
                           [](unsigned char c) { return std::tolower(c); });

    if (comp.scheme != "http" && comp.scheme != "https") return std::nullopt;

    // Move past "://"
    std::string_view remaining(url);
    remaining = remaining.substr(scheme_end + 3);

    // 2. Find where authority (host+port) ends
    // Authority ends at '/', '?', '#', or end of string
    auto authority_end = remaining.find_first_of("/?#");
    std::string_view authority = remaining.substr(0, authority_end);

    // 3. Find port separator WITHIN authority
    auto port_sep = authority.find(':');
    if (port_sep != std::string_view::npos) {
        comp.host = std::string(authority.substr(0, port_sep));
        std::string_view port_sv = authority.substr(port_sep + 1);
        if (!port_sv.empty()) {
            std::from_chars(port_sv.data(), port_sv.data() + port_sv.size(), comp.port);
        }
    } else {
        comp.host = std::string(authority);
    }

    std::ranges::transform(comp.host, comp.host.begin(), 
                           [](unsigned char c) { return std::tolower(c); });

    remaining = (authority_end == std::string_view::npos) ? "" : remaining.substr(authority_end);

    // 4. Find path
    if (!remaining.empty() && remaining[0] == '/') {
        auto path_end = remaining.find_first_of("?#");
        comp.path = std::string(remaining.substr(0, path_end));
        remaining = (path_end == std::string_view::npos) ? "" : remaining.substr(path_end);
    } else {
        comp.path = "/";
    }

    // 5. Find query
    if (!remaining.empty() && remaining[0] == '?') {
        auto query_end = remaining.find('#');
        // Substring from index 1 to skip the '?'
        if (query_end == std::string_view::npos) {
            comp.query = std::string(remaining.substr(1));
        } else {
            comp.query = std::string(remaining.substr(1, query_end - 1));
        }
    }

    return comp;
}


std::string URLNormalizer::build(const URLComponents& components) {
    std::ostringstream oss;
    
    oss << components.scheme << "://" << components.host;
    
    if (components.port != -1) {
        bool is_default = (components.scheme == "http" && components.port == 80) ||
                          (components.scheme == "https" && components.port == 443);
        if (!is_default) {
            oss << ":" << components.port;
        }
    }
    
    if (components.path.empty() || components.path[0] != '/') {
        oss << "/";
    }
    oss << components.path;
    
    if (!components.query.empty()) {
        oss << "?" << components.query;
    }
    
    return oss.str();
}

std::string URLNormalizer::normalize(const std::string& url) {
    // 1. Parse the URL into components
    auto components = parse(url); // Using the parser implemented earlier
    if (!components) {
        return "";
    }

    // 2. Decode unreserved percent-encoded characters in path
    components->path = decode_unreserved(components->path);

    // 3. Remove empty query string 
    // (Handled by checking .empty() in build(), but we ensure it's clean here)
    if (components->query.empty()) {
        components->query = "";
    }
    // 4. Build normalized URL from components
    return build(*components);
}

std::string URLNormalizer::extract_domain(const std::string& url) {
    auto components = parse(url);
    if (!components) {
        return "";
    }
    return components->host;
}


bool URLFrontier::add(const std::string& url, Priority priority) {
    // 1. Normalize the URL
    std::string normalized = URLNormalizer::normalize(url);
    if (normalized.empty()) {
        return false;  // Invalid URL
    }
    
    // 2. Check for duplicate
    if (seen_urls_.contains(normalized)) {
        return false;  // Already seen
    }
    
    // 3. Extract domain
    std::string domain = URLNormalizer::extract_domain(normalized);
    
    // 4. Add to seen set
    seen_urls_.insert(normalized);
    
    // 5. Add to priority queue
    queue_.push(URLEntry{normalized, domain, priority});
    
    return true;
}

size_t URLFrontier::add_batch(const std::vector<std::string>& urls, Priority priority) {
    size_t count = 0;
    for (const auto& url : urls) {
        if (add(url, priority)) {
            ++count;
        }
    }
    return count;
}

std::optional<std::string> URLFrontier::get_next() {
    if (queue_.empty()) {
        return std::nullopt;
    }
    
    // Temporary storage for URLs we skip due to rate limiting
    std::vector<URLEntry> skipped;
    std::optional<std::string> result;
    
    while (!queue_.empty()) {
        URLEntry entry = queue_.top();
        queue_.pop();
        
        if (can_crawl_domain(entry.domain)) {
            update_domain_time(entry.domain);
            result = entry.url;
            break;
        } else {
            skipped.push_back(std::move(entry));
        }
    }
    
    // Put skipped URLs back in the queue
    for (auto& entry : skipped) {
        queue_.push(std::move(entry));
    }
    
    return result;
}

void URLFrontier::set_crawl_delay(const std::string& domain, int delay_ms) {
    domain_states_[domain].crawl_delay_ms = delay_ms;
}

bool URLFrontier::load_seeds(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return false;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        
        size_t end = line.find_last_not_of(" \t\r\n");
        line = line.substr(start, end - start + 1);
        
        if (line.empty() || line[0] == '#') continue;
        
        add(line, Priority::SEED);
    }
    
    return true;
}

size_t URLFrontier::pending_count() const {
    return queue_.size();
}

bool URLFrontier::empty() const {
    return queue_.empty();
}

bool URLFrontier::can_crawl_domain(const std::string& domain) const {
    auto it = domain_states_.find(domain);
    if (it == domain_states_.end()) {
        return true;  // Never seen = can crawl
    }
    
    auto now = std::chrono::steady_clock::now();
    return now >= it->second.next_allowed_time;
}

void URLFrontier::update_domain_time(const std::string& domain) {
    auto& state = domain_states_[domain];
    auto now = std::chrono::steady_clock::now();
    
    int delay_ms = (state.crawl_delay_ms >= 0) ? state.crawl_delay_ms : DEFAULT_CRAWL_DELAY_MS;
    
    state.next_allowed_time = now + std::chrono::milliseconds(delay_ms);
}

} // namespace search