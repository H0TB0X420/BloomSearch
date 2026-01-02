// include/crawler/robots_parser.h
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <string_view>
#include <algorithm>

#include "http_fetcher.h"

namespace search {

struct RobotsRule {
    bool allow;           // true = Allow, false = Disallow
    std::string path;     // The path pattern
};

struct AgentRules {
    std::vector<RobotsRule> rules; 
    int crawl_delay = -1;
};

class RobotsParser {
public:
    RobotsParser() = default;
    
    bool parse(const std::string& content);
    
    bool has_agent(std::string agent) const;
    
    size_t agent_count() const { return rules_.size(); }
    
    const AgentRules* get_agent_rules(std::string agent) const;
    
    const AgentRules* get_matching_rules(std::string bot_name = "BloomSearchBot") const;

    bool is_allowed(const std::string& url, const std::string& bot_name = "BloomSearchBot") const;

    bool fetch(const std::string& domain, HTTPFetcher& fetcher);
    
    // Check if URL is allowed (fetches robots.txt if needed)
    bool is_allowed_url(const std::string& url, HTTPFetcher& fetcher,
                        const std::string& bot_name = "BloomSearchBot");
    
    // Get crawl delay for domain (fetches if needed)
    int get_crawl_delay_for(const std::string& url, HTTPFetcher& fetcher,
                            const std::string& bot_name = "BloomSearchBot");

    int get_crawl_delay(const std::string& bot_name = "BloomSearchBot") const {
        const auto* rules = get_matching_rules(bot_name);
        if (rules == nullptr) {
            return -1;  
        }
        return rules->crawl_delay;
    }
private:
    std::unordered_map<std::string, AgentRules> rules_;
    
    struct CachedRobots {
        std::unordered_map<std::string, AgentRules> rules;
        bool fetched = false;
    };
    std::unordered_map<std::string, CachedRobots> domain_cache_;

    static std::string extract_domain(const std::string& url);

    static std::string extract_path(const std::string& url){
        auto scheme_end = url.find("://");
        if (scheme_end != std::string::npos) {
            auto path_start = url.find('/', scheme_end + 3);
            if (path_start != std::string::npos) {
                return url.substr(path_start);
            }
            return "/"; 
        }
        return url;
    }

    static void normalize_agent(std::string& s) {
        std::transform(s.begin(), s.end(), s.begin(), 
                       [](unsigned char c) { return std::tolower(c); });
    }
    
    static std::string_view trim_view(std::string_view sv) {
        auto start = sv.find_first_not_of(" \t\n\r\f\v");
        if (start == std::string_view::npos) return {};
        auto end = sv.find_last_not_of(" \t\n\r\f\v");
        return sv.substr(start, end - start + 1);
    }
};

} // namespace search