#include "crawler/robots_parser.h"
#include <sstream>

namespace search {

    bool RobotsParser::parse(const std::string& content) {
        rules_.clear();

        std::string normalized;
        normalized.reserve(content.size());
        for (size_t i = 0; i < content.size(); ++i) {
            if (content[i] == '\r') {
                normalized += '\n';
                if (i + 1 < content.size() && content[i + 1] == '\n') {
                    ++i;  // Skip \n in \r\n pair
                }
            } else {
                normalized += content[i];
            }
        }

        std::istringstream ss(normalized);
        std::string line;
        std::vector<std::string> current_agents;  // Track multiple agents for grouped syntax
        bool seen_rule = false;  // Track if we've seen a rule since last user-agent block
        
        while (std::getline(ss, line)) {
            std::string_view trimmed = trim_view(line);
            
            if (trimmed.empty() || trimmed[0] == '#') {
                continue;
            }
            
            auto colon_pos = trimmed.find(':');
            if (colon_pos == std::string_view::npos) {
                continue;
            }
            
            std::string directive(trim_view(trimmed.substr(0, colon_pos)));
            std::string value(trim_view(trimmed.substr(colon_pos + 1)));

            auto comment_pos = value.find('#');
            if (comment_pos != std::string::npos) {
                value = std::string(trim_view(std::string_view(value).substr(0, comment_pos)));
            }
            
            normalize_agent(directive);
            
            if (directive == "user-agent") {
                // If we've seen rules since the last user-agent, start a new group
                if (seen_rule) {
                    current_agents.clear();
                    seen_rule = false;
                }
                
                std::string agent = value;
                normalize_agent(agent);
                
                current_agents.push_back(agent);
                
                if (!rules_.contains(agent)) {
                    rules_[agent] = AgentRules{};
                }
            } 
            else if (!current_agents.empty()) {
                // Apply rule to ALL current agents
                if (directive == "disallow") {
                    for (const auto& agent : current_agents) {
                        rules_[agent].rules.push_back(RobotsRule{false, value});
                    }
                    seen_rule = true;
                }
                else if (directive == "allow") {
                    for (const auto& agent : current_agents) {
                        rules_[agent].rules.push_back(RobotsRule{true, value});
                    }
                    seen_rule = true;
                }
                else if (directive == "crawl-delay") {
                    try {
                        int delay = std::stoi(value);
                        for (const auto& agent : current_agents) {
                            rules_[agent].crawl_delay = delay;
                        }
                    } catch (...) {
                        // Invalid crawl-delay, ignore
                    }
                    seen_rule = true;
                }
            }
        }
        return true;
    }

    bool RobotsParser::has_agent(std::string agent) const {
        normalize_agent(agent);
        return rules_.contains(agent);
    }

    const AgentRules* RobotsParser::get_agent_rules(std::string agent) const {
        normalize_agent(agent);
        auto it = rules_.find(agent);
        if (it != rules_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    const AgentRules* RobotsParser::get_matching_rules(std::string bot_name) const {
        normalize_agent(bot_name);
        auto it = rules_.find(bot_name);
        if (it != rules_.end()) {
            return &it->second;
        }  
        it = rules_.find("*");
        if (it != rules_.end()) {
            return &it->second;
        }  
        return nullptr;
    }

    bool RobotsParser::is_allowed(const std::string& url, const std::string& bot_name) const {
        std::string path = extract_path(url);
        const auto* rules = get_matching_rules(bot_name);

        if (rules == nullptr) {
            return true;
        }
        
        int best_match_length = -1;
        bool best_match_allow = true;

        for (const auto& rule : rules->rules) {
            if (rule.path.empty()) {
                continue;
            }
            
            // Check if URL path starts with rule path (prefix match)
            if (path.starts_with(rule.path)) {
                int rule_len = static_cast<int>(rule.path.length());
                if (rule_len > best_match_length) {
                    // Longer match - takes precedence
                    best_match_length = rule_len;
                    best_match_allow = rule.allow;
                } else if (rule_len == best_match_length && rule.allow) {
                    // Same length - Allow takes precedence over Disallow
                    best_match_allow = true;
                }
            }
        }
        
        return best_match_allow;
    }

    std::string RobotsParser::extract_domain(const std::string& url) {
        auto scheme_end = url.find("://");
        if (scheme_end == std::string::npos) return url;
        
        size_t domain_start = scheme_end + 3;
        size_t domain_end = url.find('/', domain_start);
        
        if (domain_end == std::string::npos) {
            return url.substr(domain_start);
        }
        return url.substr(domain_start, domain_end - domain_start);
    }

    bool RobotsParser::fetch(const std::string& url, HTTPFetcher& fetcher) {
        std::string domain = extract_domain(url);
        
        auto it = domain_cache_.find(domain);
        if (it != domain_cache_.end()) {
            rules_ = it->second.rules;
            return it->second.fetched;
        }
        
        std::string robots_url = "https://" + domain + "/robots.txt";        
        std::string content;
        bool success = fetcher.fetch(robots_url, content);
        
        rules_.clear();
        if (success) {
            parse(content);
        }
        
        domain_cache_[domain] = {rules_, success};
        return success;
    }

    bool RobotsParser::is_allowed_url(const std::string& url, HTTPFetcher& fetcher,
                                    const std::string& bot_name) {
        fetch(url, fetcher);
        std::string path = extract_path(url);
        return is_allowed(path, bot_name);
    }

    int RobotsParser::get_crawl_delay_for(const std::string& url, HTTPFetcher& fetcher,
                                        const std::string& bot_name) {
        fetch(url, fetcher);
        return get_crawl_delay(bot_name);
    }


}