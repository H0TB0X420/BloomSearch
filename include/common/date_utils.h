#pragma once

#include <string>
#include <cstdint>
#include <optional>
#include <regex>
#include <ctime>

namespace search {

/**
 * DateUtils - Parse dates and classify eras
 * 
 * Pre-AI Era:  Before November 30, 2022 (ChatGPT release)
 * Post-AI Era: November 30, 2022 and after
 */
class DateUtils {
public:
    // Nov 30, 2022 00:00:00 UTC as Unix timestamp
    static constexpr int64_t AI_ERA_CUTOFF = 1669766400;
    
    /**
     * Parse date string into Unix timestamp
     * Supports: ISO 8601, common web formats, URL patterns
     * Returns 0 if parsing fails
     */
    static int64_t parse_date(const std::string& date_str) {
        if (date_str.empty()) return 0;
        
        // Try ISO 8601: 2024-01-15T10:00:00Z or 2024-01-15
        if (auto ts = parse_iso8601(date_str); ts > 0) return ts;
        
        // Try common formats: Jan 15, 2024 / January 15, 2024
        if (auto ts = parse_text_date(date_str); ts > 0) return ts;
        
        // Try URL pattern: /2024/01/15/ or /2024-01-15/
        if (auto ts = parse_url_date(date_str); ts > 0) return ts;
        
        return 0;
    }
    
    /**
     * Extract date from URL path patterns
     * e.g., /2023/05/article-title or /blog/2022-11-15-post
     */
    static int64_t extract_date_from_url(const std::string& url) {
        // Pattern: /YYYY/MM/ or /YYYY/MM/DD/
        static const std::regex url_pattern1(R"(/(\d{4})/(\d{1,2})(?:/(\d{1,2}))?/)");
        std::smatch match;
        if (std::regex_search(url, match, url_pattern1)) {
            int year = std::stoi(match[1]);
            int month = std::stoi(match[2]);
            int day = match[3].matched ? std::stoi(match[3]) : 1;
            return make_timestamp(year, month, day);
        }
        
        // Pattern: YYYY-MM-DD in URL
        static const std::regex url_pattern2(R"((\d{4})-(\d{2})-(\d{2}))");
        if (std::regex_search(url, match, url_pattern2)) {
            int year = std::stoi(match[1]);
            int month = std::stoi(match[2]);
            int day = std::stoi(match[3]);
            return make_timestamp(year, month, day);
        }
        
        return 0;
    }
    
    /**
     * Classify era based on timestamp
     */
    static bool is_pre_ai(int64_t timestamp) {
        return timestamp > 0 && timestamp < AI_ERA_CUTOFF;
    }
    
    static bool is_post_ai(int64_t timestamp) {
        return timestamp >= AI_ERA_CUTOFF;
    }
    
    static std::string era_string(int64_t timestamp) {
        if (timestamp <= 0) return "unknown";
        return is_pre_ai(timestamp) ? "pre-ai" : "post-ai";
    }
    
    /**
     * Format timestamp for display: "Jan 15, 2024"
     */
    static std::string format_date(int64_t timestamp) {
        if (timestamp <= 0) return "";
        
        time_t t = static_cast<time_t>(timestamp);
        struct tm* tm_info = gmtime(&t);
        if (!tm_info) return "";
        
        static const char* months[] = {
            "Jan", "Feb", "Mar", "Apr", "May", "Jun",
            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
        };
        
        char buf[32];
        snprintf(buf, sizeof(buf), "%s %d, %d",
                 months[tm_info->tm_mon],
                 tm_info->tm_mday,
                 tm_info->tm_year + 1900);
        return buf;
    }
    
    /**
     * Format with era indicator
     */
    static std::string format_with_era(int64_t timestamp) {
        if (timestamp <= 0) return "[Date: Unknown]";
        
        std::string date = format_date(timestamp);
        if (is_pre_ai(timestamp)) {
            return "[" + date + " · Pre-AI ✓]";
        } else {
            return "[" + date + " · Post-AI]";
        }
    }

private:
    static int64_t make_timestamp(int year, int month, int day) {
        if (year < 1970 || year > 2100) return 0;
        if (month < 1 || month > 12) return 0;
        if (day < 1 || day > 31) return 0;
        
        struct tm tm_info = {};
        tm_info.tm_year = year - 1900;
        tm_info.tm_mon = month - 1;
        tm_info.tm_mday = day;
        tm_info.tm_hour = 12;  // Noon to avoid timezone edge cases
        
        time_t t = timegm(&tm_info);
        return static_cast<int64_t>(t);
    }
    
    static int64_t parse_iso8601(const std::string& str) {
        // 2024-01-15T10:00:00Z or 2024-01-15T10:00:00+00:00 or 2024-01-15
        static const std::regex iso_pattern(R"((\d{4})-(\d{2})-(\d{2}))");
        std::smatch match;
        if (std::regex_search(str, match, iso_pattern)) {
            int year = std::stoi(match[1]);
            int month = std::stoi(match[2]);
            int day = std::stoi(match[3]);
            return make_timestamp(year, month, day);
        }
        return 0;
    }
    
    static int64_t parse_text_date(const std::string& str) {
        // "January 15, 2024" or "Jan 15, 2024"
        static const std::regex text_pattern(
            R"((Jan(?:uary)?|Feb(?:ruary)?|Mar(?:ch)?|Apr(?:il)?|May|Jun(?:e)?|Jul(?:y)?|Aug(?:ust)?|Sep(?:tember)?|Oct(?:ober)?|Nov(?:ember)?|Dec(?:ember)?)\s+(\d{1,2}),?\s+(\d{4}))",
            std::regex::icase
        );
        
        std::smatch match;
        if (std::regex_search(str, match, text_pattern)) {
            std::string month_str = match[1];
            int day = std::stoi(match[2]);
            int year = std::stoi(match[3]);
            
            // Convert month name to number
            int month = month_to_number(month_str);
            if (month > 0) {
                return make_timestamp(year, month, day);
            }
        }
        return 0;
    }
    
    static int64_t parse_url_date(const std::string& str) {
        return extract_date_from_url(str);
    }
    
    static int month_to_number(const std::string& month) {
        std::string lower;
        for (char c : month) {
            lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        
        if (lower.substr(0, 3) == "jan") return 1;
        if (lower.substr(0, 3) == "feb") return 2;
        if (lower.substr(0, 3) == "mar") return 3;
        if (lower.substr(0, 3) == "apr") return 4;
        if (lower.substr(0, 3) == "may") return 5;
        if (lower.substr(0, 3) == "jun") return 6;
        if (lower.substr(0, 3) == "jul") return 7;
        if (lower.substr(0, 3) == "aug") return 8;
        if (lower.substr(0, 3) == "sep") return 9;
        if (lower.substr(0, 3) == "oct") return 10;
        if (lower.substr(0, 3) == "nov") return 11;
        if (lower.substr(0, 3) == "dec") return 12;
        return 0;
    }
};

} // namespace search