#pragma once

#include <string>
#include <vector>
#include <regex>
#include <algorithm>

namespace search {

/**
 * URLFilter - Filters out low-value or problematic URLs
 * 
 * Rejects:
 * - URLs over 256 characters
 * - Pagination URLs (?page=, &page=, /page/N)
 * - Query-heavy URLs (more than 3 params)
 * - Known low-value patterns (login, logout, print, share)
 * - File extensions we don't want (pdf, zip, exe, etc.)
 */
class URLFilter {
public:
    static constexpr size_t MAX_URL_LENGTH = 256;
    static constexpr int MAX_QUERY_PARAMS = 3;
    
    /**
     * Check if URL should be crawled
     * @return true if URL is acceptable, false if should be skipped
     */
    static bool is_acceptable(const std::string& url) {
        // Length check
        if (url.length() > MAX_URL_LENGTH) {
            return false;
        }
        
        // Must be HTTP(S)
        if (url.find("http://") != 0 && url.find("https://") != 0) {
            return false;
        }
        
        // Skip unwanted file extensions
        if (has_bad_extension(url)) {
            return false;
        }
        
        // Skip pagination URLs
        if (is_pagination_url(url)) {
            return false;
        }
        
        // Skip query-heavy URLs
        if (count_query_params(url) > MAX_QUERY_PARAMS) {
            return false;
        }
        
        // Skip known low-value patterns
        if (has_low_value_pattern(url)) {
            return false;
        }
        
        return true;
    }
    
    /**
     * Get rejection reason for logging
     */
    static std::string rejection_reason(const std::string& url) {
        if (url.length() > MAX_URL_LENGTH) {
            return "too long (" + std::to_string(url.length()) + " chars)";
        }
        if (url.find("http://") != 0 && url.find("https://") != 0) {
            return "not HTTP(S)";
        }
        if (has_bad_extension(url)) {
            return "bad extension";
        }
        if (is_pagination_url(url)) {
            return "pagination";
        }
        if (count_query_params(url) > MAX_QUERY_PARAMS) {
            return "too many query params";
        }
        if (has_low_value_pattern(url)) {
            return "low-value pattern";
        }
        return "unknown";
    }

private:
    static bool has_bad_extension(const std::string& url) {
        // Get path without query string
        size_t query_pos = url.find('?');
        std::string path = (query_pos != std::string::npos) 
                          ? url.substr(0, query_pos) 
                          : url;
        
        // Convert to lowercase for comparison
        std::string lower_path;
        lower_path.reserve(path.size());
        for (char c : path) {
            lower_path += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        
        // Bad extensions
        static const std::vector<std::string> bad_extensions = {
            ".pdf", ".zip", ".tar", ".gz", ".rar", ".7z",
            ".exe", ".dmg", ".msi", ".deb", ".rpm",
            ".jpg", ".jpeg", ".png", ".gif", ".svg", ".ico", ".webp",
            ".mp3", ".mp4", ".avi", ".mov", ".wmv", ".flv",
            ".doc", ".docx", ".xls", ".xlsx", ".ppt", ".pptx",
            ".css", ".js", ".json", ".xml", ".rss", ".atom",
            ".woff", ".woff2", ".ttf", ".eot"
        };
        
        for (const auto& ext : bad_extensions) {
            if (lower_path.length() >= ext.length() &&
                lower_path.substr(lower_path.length() - ext.length()) == ext) {
                return true;
            }
        }
        
        return false;
    }
    
    static bool is_pagination_url(const std::string& url) {
        std::string lower_url;
        lower_url.reserve(url.size());
        for (char c : url) {
            lower_url += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        
        // Query param pagination
        if (lower_url.find("?page=") != std::string::npos ||
            lower_url.find("&page=") != std::string::npos ||
            lower_url.find("?p=") != std::string::npos ||
            lower_url.find("&p=") != std::string::npos ||
            lower_url.find("?offset=") != std::string::npos ||
            lower_url.find("&offset=") != std::string::npos ||
            lower_url.find("?start=") != std::string::npos ||
            lower_url.find("&start=") != std::string::npos) {
            return true;
        }
        
        // Path pagination: /page/2, /pages/3, etc.
        static const std::regex page_pattern(R"(/pages?/\d+)", std::regex::icase);
        if (std::regex_search(lower_url, page_pattern)) {
            return true;
        }
        
        return false;
    }
    
    static int count_query_params(const std::string& url) {
        size_t query_pos = url.find('?');
        if (query_pos == std::string::npos) {
            return 0;
        }
        
        std::string query = url.substr(query_pos + 1);
        if (query.empty()) {
            return 0;
        }
        
        // Count & characters + 1
        int count = 1;
        for (char c : query) {
            if (c == '&') count++;
        }
        
        return count;
    }
    
    static bool has_low_value_pattern(const std::string& url) {
        std::string lower_url;
        lower_url.reserve(url.size());
        for (char c : url) {
            lower_url += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        
        static const std::vector<std::string> low_value_patterns = {
            // Auth/account
            "/login", "/logout", "/signin", "/signout", "/signup",
            "/register", "/password", "/forgot", "/reset",
            "/account", "/profile", "/settings", "/preferences",
            
            // Actions
            "/print", "/share", "/email", "/export", "/download",
            "/subscribe", "/unsubscribe", "/feed",
            
            // GitHub specific
            "/forks", "/stargazers", "/watchers", "/network",
            "/graphs", "/pulse", "/compare", "/commits",
            
            // Wikipedia specific  
            "/wiki/special:", "/wiki/talk:", "/wiki/user:",
            "/wiki/wikipedia:", "/wiki/file:", "/wiki/template:",
            "/wiki/category:", "/wiki/portal:", "/wiki/help:",
            "action=edit", "action=history", "oldid=",
            
            // Social/tracking
            "/share?", "utm_", "fbclid=", "gclid=",
            
            // Misc low-value
            "/search", "/tag/", "/tags/", "/archive/",
            "/comment", "/reply", "/vote"
        };
        
        for (const auto& pattern : low_value_patterns) {
            if (lower_url.find(pattern) != std::string::npos) {
                return true;
            }
        }
        
        return false;
    }
};

} // namespace search