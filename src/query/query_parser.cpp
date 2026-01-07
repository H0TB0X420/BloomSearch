#include "query/query_parser.h"
#include <sstream>
#include <cctype>

namespace search {

QueryParser::QueryParser() 
    : processor_(std::make_shared<TextProcessor>()) {
}

QueryParser::QueryParser(std::shared_ptr<TextProcessor> processor)
    : processor_(processor ? processor : std::make_shared<TextProcessor>()) {
}

ParsedQuery QueryParser::parse(const std::string& query) {
    ParsedQuery result;
    result.original_query = query;
    
    if (query.empty()) {
        return result;
    }
    
    std::string working = query;
    
    // Step 1: Extract quoted phrases first
    result.phrases = extract_phrases(working);
    
    // Step 2: Split remaining text into tokens
    std::istringstream iss(working);
    std::string token;
    
    while (iss >> token) {
        if (token.find(':') != std::string::npos) {
            if (parse_filter(token, result)) {
                continue;
            }
        }
        
        if (token.size() > 1 && token[0] == '-') {
            std::string excluded = token.substr(1);
            excluded = processor_->process_word(excluded);
            if (!excluded.empty()) {
                result.excluded_terms.push_back(excluded);
            }
            continue;
        }
        
        std::string normalized = processor_->normalize(token);        
        if (processor_->is_stop_word(normalized)) {
            continue;
        }        
        std::string processed = enable_stemming_ ? processor_->stem(normalized) : normalized;
        
        if (!processed.empty()) {
            result.terms.push_back(processed);
        }
    }
    
    return result;
}

std::vector<std::string> QueryParser::extract_phrases(std::string& query) {
    std::vector<std::string> phrases;
    
    size_t start = 0;
    while ((start = query.find('"', start)) != std::string::npos) {
        size_t end = query.find('"', start + 1);
        if (end == std::string::npos) {
            break;
        }
        
        std::string phrase = query.substr(start + 1, end - start - 1);
        
        size_t first = phrase.find_first_not_of(" \t");
        size_t last = phrase.find_last_not_of(" \t");
        if (first != std::string::npos) {
            phrase = phrase.substr(first, last - first + 1);
        }
        
        if (!phrase.empty()) {
            phrase = processor_->to_lower(phrase);
            phrases.push_back(phrase);
        }
        
        query.replace(start, end - start + 1, " ");
    }
    
    return phrases;
}

bool QueryParser::parse_filter(const std::string& token, ParsedQuery& query) {
    size_t colon_pos = token.find(':');
    if (colon_pos == std::string::npos || colon_pos == 0) {
        return false;
    }
    
    std::string key = token.substr(0, colon_pos);
    std::string value = token.substr(colon_pos + 1);
    
    key = processor_->to_lower(key);
    
    if (key == "era") {
        return parse_era_filter(value, query);
    } else if (key == "ai") {
        return parse_ai_filter(value, query);
    } else if (key == "site") {
        return parse_site_filter(value, query);
    }
    
    return false;  // Unknown filter
}

bool QueryParser::parse_era_filter(const std::string& value, ParsedQuery& query) {
    auto era = parse_era(value); 
    if (era) {
        query.era_filter = era;
        return true;
    }
    return false;
}

bool QueryParser::parse_ai_filter(const std::string& value, ParsedQuery& query) {
    if (value.empty()) return false;
    
    try {
        if (value[0] == '<') {
            // ai:<0.3 - max AI score
            float score = std::stof(value.substr(1));
            if (score >= 0.0f && score <= 1.0f) {
                query.max_ai_score = score;
                return true;
            }
        } else if (value[0] == '>') {
            // ai:>0.7 - min AI score (find AI content)
            float score = std::stof(value.substr(1));
            if (score >= 0.0f && score <= 1.0f) {
                query.min_ai_score = score;
                return true;
            }
        } else {
            // ai:0.3 - treat as max
            float score = std::stof(value);
            if (score >= 0.0f && score <= 1.0f) {
                query.max_ai_score = score;
                return true;
            }
        }
    } catch (...) {
        // Invalid number
    }
    
    return false;
}

bool QueryParser::parse_site_filter(const std::string& value, ParsedQuery& query) {
    if (value.empty()) return false;    
    query.site_filter = processor_->to_lower(value);
    return true;
}

} // namespace search