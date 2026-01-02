#include "common/utils.h"
#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>

namespace search {
namespace utils {

std::string trim(const std::string& str) {
    auto start = str.begin();
    while (start != str.end() && std::isspace(*start)) {
        ++start;
    }
    
    auto end = str.end();
    do {
        --end;
    } while (std::distance(start, end) > 0 && std::isspace(*end));
    
    return std::string(start, end + 1);
}

std::string to_lower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    return result;
}

std::vector<std::string> split(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(str);
    while (std::getline(tokenStream, token, delimiter)) {
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }
    return tokens;
}

bool is_valid_url(const std::string& url) {
    std::regex url_regex(R"(^https?://[^\s]+$)");
    return std::regex_match(url, url_regex);
}

std::string normalize_url(const std::string& url) {
    std::string normalized = trim(url);
    
    if (!normalized.empty() && normalized.back() == '/') {
        normalized.pop_back();
    }
    
    normalized = to_lower(normalized);
    
    return normalized;
}

std::string extract_domain(const std::string& url) {
    std::regex domain_regex(R"(^https?://([^/]+))");
    std::smatch match;
    if (std::regex_search(url, match, domain_regex) && match.size() > 1) {
        return match.str(1);
    }
    return "";
}

} // namespace utils
} // namespace search
