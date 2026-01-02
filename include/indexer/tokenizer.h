#pragma once

#include "common/text_processor.h"
#include <string>
#include <vector>
#include <memory>

namespace search {

//=============================================================================
// Token with position information (for phrase queries and highlighting)
//=============================================================================
struct Token {
    std::string text;       // Processed token (normalized, stemmed)
    std::string original;   // Original text before processing
    size_t position;        // Token position in document (0-indexed)
    size_t char_offset;     // Character offset in original text
};

//=============================================================================
// Tokenizer
// Converts text into tokens for indexing
// Delegates core text processing to TextProcessor
//=============================================================================
class Tokenizer {
public:
    Tokenizer();
    explicit Tokenizer(std::shared_ptr<TextProcessor> processor);
    ~Tokenizer() = default;
    
    //=========================================================================
    // Main tokenization methods
    //=========================================================================
    
    // Full tokenization with position tracking
    std::vector<Token> tokenize(const std::string& text) const;
    
    // Simple tokenization (just strings, no positions)
    std::vector<std::string> tokenize_simple(const std::string& text) const;
    
    // Query tokenization (may have different behavior)
    std::vector<std::string> tokenize_query(const std::string& query) const;
    
    //=========================================================================
    // N-gram generation
    //=========================================================================
    
    // Generate word bigrams (for phrase matching)
    std::vector<std::string> generate_bigrams(const std::vector<std::string>& tokens) const;
    
    // Generate character n-grams (for fuzzy matching)
    std::vector<std::string> generate_char_ngrams(const std::string& text, size_t n = 3) const;
    
    //=========================================================================
    // Configuration (delegates to TextProcessor)
    //=========================================================================
    
    void set_min_token_length(size_t len);
    void set_max_token_length(size_t len);
    void set_apply_stemming(bool apply);
    void set_remove_stop_words(bool remove);
    
    // Stop word management
    void add_stop_word(const std::string& word);
    void add_stop_words(const std::vector<std::string>& words);
    
    // Direct access to underlying processor (for advanced use)
    TextProcessor& processor() { return *processor_; }
    const TextProcessor& processor() const { return *processor_; }
    
    //=========================================================================
    // Convenience methods (delegate to TextProcessor)
    //=========================================================================
    
    std::string normalize(const std::string& token) const;
    std::string stem(const std::string& word) const;
    bool is_stop_word(const std::string& word) const;

private:
    std::shared_ptr<TextProcessor> processor_;
    
    // Configuration (cached from processor for convenience)
    bool remove_stop_words_ = true;
    bool apply_stemming_ = true;
    size_t min_token_length_ = 1;
    size_t max_token_length_ = 100;
};

} // namespace search