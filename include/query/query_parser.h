#pragma once

#include "common/search_types.h"
#include "common/text_processor.h"
#include <string>
#include <vector>
#include <memory>

namespace search {

//=============================================================================
// Query Parser
// Parses user search queries into structured form
// Uses shared TextProcessor for consistent tokenization/stemming
//=============================================================================
class QueryParser {
public:
    QueryParser();
    explicit QueryParser(std::shared_ptr<TextProcessor> processor);
    ~QueryParser() = default;
    
    // Parse a query string into structured form
    ParsedQuery parse(const std::string& query);
    
    // Get last error message
    const std::string& last_error() const { return last_error_; }
    
    // Configuration
    void set_enable_stemming(bool enable);
    
    // Access to underlying processor (for sharing with Tokenizer)
    TextProcessor& processor() { return *processor_; }
    const TextProcessor& processor() const { return *processor_; }
    std::shared_ptr<TextProcessor> shared_processor() { return processor_; }

private:
    std::shared_ptr<TextProcessor> processor_;
    std::string last_error_;
    bool enable_stemming_ = true;
    
    // Filter parsing
    bool parse_filter(const std::string& token, ParsedQuery& query);
    bool parse_era_filter(const std::string& value, ParsedQuery& query);
    bool parse_ai_filter(const std::string& value, ParsedQuery& query);
    bool parse_site_filter(const std::string& value, ParsedQuery& query);
    
    // Quote handling
    std::vector<std::string> extract_phrases(std::string& query);
};

} // namespace search