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
        
        if (auto ts = parse_iso8601(date_str); ts > 0) return ts;
        if (auto ts = parse_text_date(date_str); ts > 0) return ts;
        if (auto ts = parse_url_date(date_str); ts > 0) return ts;
        
        return 0;
    }
    
    /**
     * Extract date from URL path patterns
     */
    static int64_t extract_date_from_url(const std::string& url) {
        static const std::regex url_pattern1(R"(/(\d{4})/(\d{1,2})(?:/(\d{1,2}))?/)");
        std::smatch match;
        if (std::regex_search(url, match, url_pattern1)) {
            int year = std::stoi(match[1]);
            int month = std::stoi(match[2]);
            int day = match[3].matched ? std::stoi(match[3]) : 1;
            return make_timestamp(year, month, day);
        }
        
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
     * Extract date from page content (body text)
     */
    static int64_t extract_date_from_content(const std::string& content) {
        if (content.empty()) return 0;
        
        std::string text = content.substr(0, std::min(content.size(), size_t(2500)));
        
        if (auto ts = find_published_date(text); ts > 0) return ts;
        if (auto ts = find_labeled_date(text); ts > 0) return ts;
        if (auto ts = find_standalone_date(text); ts > 0) return ts;
        if (auto ts = find_copyright_year(text); ts > 0) return ts;
        
        return 0;
    }
    
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
        tm_info.tm_hour = 12;
        
        time_t t = timegm(&tm_info);
        return static_cast<int64_t>(t);
    }
    
    static int64_t parse_iso8601(const std::string& str) {
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
        static const std::regex text_pattern(
            R"((Jan(?:uary)?|Feb(?:ruary)?|Mar(?:ch)?|Apr(?:il)?|May|Jun(?:e)?|Jul(?:y)?|Aug(?:ust)?|Sep(?:tember)?|Oct(?:ober)?|Nov(?:ember)?|Dec(?:ember)?)\s+(\d{1,2}),?\s+(\d{4}))",
            std::regex::icase
        );
        
        std::smatch match;
        if (std::regex_search(str, match, text_pattern)) {
            std::string month_str = match[1];
            int day = std::stoi(match[2]);
            int year = std::stoi(match[3]);
            
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
    
    // Content extraction helpers
    
    static int64_t find_published_date(const std::string& text) {
        // Try ISO format first: "Published 2021-01-15" or "Posted on 2020-03-22"
        static const std::regex iso_pattern(
            R"((?:published|posted|written|authored)(?:\s+on)?[:\s]+(\d{4})-(\d{1,2})-(\d{1,2}))",
            std::regex::icase
        );
        
        std::smatch match;
        if (std::regex_search(text, match, iso_pattern)) {
            return make_timestamp(
                std::stoi(match[1]),
                std::stoi(match[2]),
                std::stoi(match[3])
            );
        }
        
        // Try text format: "Published January 15, 2021"
        static const std::regex text_pattern(
            R"((?:published|posted|written|authored)(?:\s+on)?[:\s]+(Jan(?:uary)?|Feb(?:ruary)?|Mar(?:ch)?|Apr(?:il)?|May|Jun(?:e)?|Jul(?:y)?|Aug(?:ust)?|Sep(?:tember)?|Oct(?:ober)?|Nov(?:ember)?|Dec(?:ember)?)\s+(\d{1,2}),?\s+(\d{4}))",
            std::regex::icase
        );
        
        if (std::regex_search(text, match, text_pattern)) {
            int month = month_to_number(match[1]);
            return make_timestamp(
                std::stoi(match[3]),
                month,
                std::stoi(match[2])
            );
        }
        
        return 0;
    }
    
    static int64_t find_labeled_date(const std::string& text) {
        // Try ISO format: "Date: 2021-01-15" or "Updated: 2020-03-22"
        static const std::regex iso_pattern(
            R"((?:date|updated|modified|last\s+updated|last\s+modified)[:\s]+(\d{4})-(\d{1,2})-(\d{1,2}))",
            std::regex::icase
        );
        
        std::smatch match;
        if (std::regex_search(text, match, iso_pattern)) {
            return make_timestamp(
                std::stoi(match[1]),
                std::stoi(match[2]),
                std::stoi(match[3])
            );
        }
        
        // Try text format: "Date: January 15, 2021"
        static const std::regex text_pattern(
            R"((?:date|updated|modified|last\s+updated|last\s+modified)[:\s]+(Jan(?:uary)?|Feb(?:ruary)?|Mar(?:ch)?|Apr(?:il)?|May|Jun(?:e)?|Jul(?:y)?|Aug(?:ust)?|Sep(?:tember)?|Oct(?:ober)?|Nov(?:ember)?|Dec(?:ember)?)\s+(\d{1,2}),?\s+(\d{4}))",
            std::regex::icase
        );
        
        if (std::regex_search(text, match, text_pattern)) {
            int month = month_to_number(match[1]);
            return make_timestamp(
                std::stoi(match[3]),
                month,
                std::stoi(match[2])
            );
        }
        
        return 0;
    }
    
    static int64_t find_standalone_date(const std::string& text) {
        std::string start = text.substr(0, std::min(text.size(), size_t(500)));
        
        // Try text format first
        static const std::regex text_pattern(
            R"((Jan(?:uary)?|Feb(?:ruary)?|Mar(?:ch)?|Apr(?:il)?|May|Jun(?:e)?|Jul(?:y)?|Aug(?:ust)?|Sep(?:tember)?|Oct(?:ober)?|Nov(?:ember)?|Dec(?:ember)?)\s+(\d{1,2}),?\s+(\d{4}))",
            std::regex::icase
        );
        
        std::smatch match;
        if (std::regex_search(start, match, text_pattern)) {
            int month = month_to_number(match[1]);
            int day = std::stoi(match[2]);
            int year = std::stoi(match[3]);
            
            if (year >= 1995 && year <= 2030) {
                return make_timestamp(year, month, day);
            }
        }
        
        // Try ISO format
        static const std::regex iso_pattern(R"((\d{4})-(\d{2})-(\d{2}))");
        if (std::regex_search(start, match, iso_pattern)) {
            int year = std::stoi(match[1]);
            int month = std::stoi(match[2]);
            int day = std::stoi(match[3]);
            
            if (year >= 1995 && year <= 2030) {
                return make_timestamp(year, month, day);
            }
        }
        
        return 0;
    }
    
    static int64_t find_copyright_year(const std::string& text) {
        // "© 2021" or "Copyright 2021" or "(c) 2021"
        static const std::regex pattern(
            R"((?:©|\(c\)|copyright)\s*(\d{4}))",
            std::regex::icase
        );
        
        std::smatch match;
        if (std::regex_search(text, match, pattern)) {
            int year = std::stoi(match[1]);
            if (year >= 1995 && year <= 2030) {
                return make_timestamp(year, 7, 1);
            }
        }
        return 0;
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