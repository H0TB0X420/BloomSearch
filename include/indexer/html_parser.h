#pragma once

#include <string>
#include <vector>
#include <optional>
#include <unordered_map>

// Forward declare Gumbo types
struct GumboInternalNode;
struct GumboInternalOutput;
typedef GumboInternalNode GumboNode;
typedef GumboInternalOutput GumboOutput;

namespace search {

//=============================================================================
// Extracted link with anchor text
//=============================================================================
struct ExtractedLink {
    std::string url;
    std::string anchor_text;
};

//=============================================================================
// Parsed document containing all extracted data
//=============================================================================
struct ParsedDocument {
    // Basic metadata
    std::string title;
    std::string description;
    std::string keywords;
    std::string author;
    
    // Date extraction (for era classification)
    std::optional<std::string> published_date;
    std::optional<std::string> modified_date;
    
    // Content
    std::string text_content;      // All visible text
    std::string main_content;      // Main body text (excluding nav, footer, etc.)
    
    // Links
    std::vector<ExtractedLink> links;
    
    // Stats
    size_t word_count = 0;
    size_t link_count = 0;
};

//=============================================================================
// HTML Parser - extracts structured data from HTML
//=============================================================================
class HTMLParser {
public:
    HTMLParser() = default;
    ~HTMLParser() = default;
    
    // Main parsing method
    ParsedDocument parse(const std::string& html, const std::string& base_url = "");
    
    // Individual extraction methods (for testing)
    std::string extract_title(const std::string& html);
    std::string extract_text(const std::string& html);
    std::vector<ExtractedLink> extract_links(const std::string& html, const std::string& base_url = "");
    std::unordered_map<std::string, std::string> extract_meta_tags(const std::string& html);
    
    // Get last error (if any)
    const std::string& last_error() const { return last_error_; }

private:
    std::string last_error_;
    
    void extract_text_recursive(const GumboNode* node, 
                                std::string& output,
                                bool& in_main_content);
    
    void extract_links_recursive(const GumboNode* node,
                                 std::vector<ExtractedLink>& links,
                                 const std::string& base_url);
    
    std::string get_attribute(const GumboNode* node, const char* name);
    std::string normalize_url(const std::string& url, const std::string& base_url);
    std::string trim_whitespace(const std::string& str);
    
    // Tags to skip when extracting text
    bool is_invisible_tag(int tag) const;
    bool is_boilerplate_tag(int tag) const;
};

} // namespace search