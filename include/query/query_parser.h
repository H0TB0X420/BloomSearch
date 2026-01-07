#pragma once

#include "common/search_types.h"
#include "common/text_processor.h"
#include <string>
#include <vector>
#include <memory>

namespace search {

class QueryParser {
public:
    QueryParser();
    explicit QueryParser(std::shared_ptr<TextProcessor> processor);
    ~QueryParser() = default;
    
    ParsedQuery parse(const std::string& query);
    
    const std::string& last_error() const { return last_error_; }
        
    TextProcessor& processor() { return *processor_; }
    const TextProcessor& processor() const { return *processor_; }
    std::shared_ptr<TextProcessor> shared_processor() { return processor_; }

private:
    std::shared_ptr<TextProcessor> processor_;
    std::string last_error_;
    bool enable_stemming_ = true;
    
    bool parse_filter(const std::string& token, ParsedQuery& query);
    bool parse_era_filter(const std::string& value, ParsedQuery& query);
    bool parse_ai_filter(const std::string& value, ParsedQuery& query);
    bool parse_site_filter(const std::string& value, ParsedQuery& query);
    
    std::vector<std::string> extract_phrases(std::string& query);
};

} // namespace search