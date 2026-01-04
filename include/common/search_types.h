#pragma once

#include <string>
#include <vector>
#include <optional>
#include <unordered_map>
#include <cstdint>

namespace search {

enum class Era {
    PRE_AI,      // Before Nov 2022 - definitely human-written
    TRANSITION,  // Nov 2022 - Dec 2023 - uncertain period
    AI_ERA,      // 2024+ - high likelihood of AI if other signals present
    UNKNOWN      // No date information available
};

inline std::string era_to_string(Era era) {
    switch (era) {
        case Era::PRE_AI:     return "Pre-AI";
        case Era::TRANSITION: return "Transition";
        case Era::AI_ERA:     return "AI-Era";
        case Era::UNKNOWN:    return "Unknown";
        default:              return "Unknown";
    }
}

inline std::optional<Era> parse_era(const std::string& str) {
    std::string lower;
    for (char c : str) {
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    
    if (lower == "pre-ai" || lower == "preai" || lower == "pre_ai" || lower == "human") {
        return Era::PRE_AI;
    } else if (lower == "transition" || lower == "trans") {
        return Era::TRANSITION;
    } else if (lower == "ai-era" || lower == "ai_era" || lower == "aiera" || lower == "ai") {
        return Era::AI_ERA;
    }
    
    return std::nullopt;
}

struct ParsedQuery {
    // Search terms (tokenized, stemmed)
    std::vector<std::string> terms;
    
    // Exact phrase matches (content inside quotes)
    std::vector<std::string> phrases;
    
    // Excluded terms (prefixed with -)
    std::vector<std::string> excluded_terms;
    
    // Filters
    std::optional<Era> era_filter;              // era:pre-ai
    std::optional<float> max_ai_score;          // ai:<0.3
    std::optional<float> min_ai_score;          // ai:>0.7
    std::optional<std::string> site_filter;     // site:example.com
    
    // Original query for display
    std::string original_query;
    
    bool empty() const {
        return terms.empty() && phrases.empty();
    }
    
    std::string to_string() const;
};

struct DocumentInfo {
    uint64_t doc_id = 0;
    std::string url;
    std::string title;
    std::string snippet;           // Text excerpt for display
    uint32_t doc_length = 0;       // Number of tokens in document
    int64_t published_at = 0;      // Original publish date (0 = unknown)
    int64_t modified_at = 0;       // Last modified date (0 = unknown)
    float ai_score = 0.0f;         // 0.0 = human, 1.0 = AI
    Era era = Era::UNKNOWN;        // PRE_AI, POST_AI, UNKNOWN
    std::string domain;            // Extracted domain for site: filter
};

struct SearchResult {
    DocumentInfo doc;
    float score = 0.0f;            // Final combined score
    float relevance_score = 0.0f;  // TF-IDF/BM25 score
    float ai_penalty = 0.0f;       // Penalty applied for AI content
    
    std::unordered_map<std::string, float> term_scores;
    
    bool operator<(const SearchResult& other) const {
        return score > other.score;  // Higher score = better (for sorting)
    }
   
};

struct SearchResponse {
    ParsedQuery query;
    
    std::vector<SearchResult> results;
    
    uint64_t total_matches = 0;
    uint64_t docs_searched = 0;    
    float search_time_ms = 0.0f;  
    
    bool empty() const { return results.empty(); }
    size_t size() const { return results.size(); }
};

enum class Field : uint8_t {
    BODY = 0,
    TITLE = 1,
    H1 = 2,
    H2 = 3,
    URL = 4,
    META_DESC = 5,
    ANCHOR = 6
};

constexpr float FIELD_BOOSTS[] = {
    1.0f,   // BODY
    10.0f,  // TITLE
    5.0f,   // H1
    3.0f,   // H2
    5.0f,   // URL
    2.0f,   // META_DESC
    4.0f    // ANCHOR
};


struct Posting {
    uint64_t doc_id;
    Field field;
    uint32_t frequency;                 
    std::vector<uint32_t> positions;    
    
    std::string serialize() const;
    static Posting deserialize(const std::string& data);
};

struct PostingList {
    std::string term;
    uint32_t doc_frequency;             
    std::vector<Posting> postings;
    
    void add_posting(const Posting& posting);
    
    void remove_document(uint64_t doc_id);
    
    std::string serialize() const;
    static PostingList deserialize(const std::string& data);
};

inline std::string ParsedQuery::to_string() const {
    std::string result = "ParsedQuery {\n";
    result += "  original: \"" + original_query + "\"\n";
    
    result += "  terms: [";
    for (size_t i = 0; i < terms.size(); ++i) {
        if (i > 0) result += ", ";
        result += "\"" + terms[i] + "\"";
    }
    result += "]\n";
    
    if (!phrases.empty()) {
        result += "  phrases: [";
        for (size_t i = 0; i < phrases.size(); ++i) {
            if (i > 0) result += ", ";
            result += "\"" + phrases[i] + "\"";
        }
        result += "]\n";
    }
    
    if (!excluded_terms.empty()) {
        result += "  excluded: [";
        for (size_t i = 0; i < excluded_terms.size(); ++i) {
            if (i > 0) result += ", ";
            result += "\"" + excluded_terms[i] + "\"";
        }
        result += "]\n";
    }
    
    if (era_filter) {
        result += "  era_filter: " + era_to_string(*era_filter) + "\n";
    }
    if (max_ai_score) {
        result += "  max_ai_score: " + std::to_string(*max_ai_score) + "\n";
    }
    if (min_ai_score) {
        result += "  min_ai_score: " + std::to_string(*min_ai_score) + "\n";
    }
    if (site_filter) {
        result += "  site_filter: " + *site_filter + "\n";
    }
    
    result += "}";
    return result;
}

} // namespace search