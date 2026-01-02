#pragma once

#include <string>
#include <vector>

namespace search {
namespace utils {

// String utilities
std::string trim(const std::string& str);
std::string to_lower(const std::string& str);
std::vector<std::string> split(const std::string& str, char delimiter);

// URL utilities
bool is_valid_url(const std::string& url);
std::string normalize_url(const std::string& url);
std::string extract_domain(const std::string& url);

} // namespace utils
} // namespace search
