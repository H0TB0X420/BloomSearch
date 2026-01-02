#include "indexer/tokenizer.h"
#include <algorithm>
#include <cctype>

namespace search {

//=============================================================================
// Constructors
//=============================================================================
Tokenizer::Tokenizer() 
    : processor_(std::make_shared<TextProcessor>()) {
}

Tokenizer::Tokenizer(std::shared_ptr<TextProcessor> processor)
    : processor_(processor ? processor : std::make_shared<TextProcessor>()) {
}

//=============================================================================
// Configuration
//=============================================================================
void Tokenizer::set_min_token_length(size_t len) {
    min_token_length_ = len;
    processor_->set_min_token_length(len);
}

void Tokenizer::set_max_token_length(size_t len) {
    max_token_length_ = len;
    processor_->set_max_token_length(len);
}

void Tokenizer::set_apply_stemming(bool apply) {
    apply_stemming_ = apply;
    processor_->set_stemming_enabled(apply);
}

void Tokenizer::set_remove_stop_words(bool remove) {
    remove_stop_words_ = remove;
    processor_->set_stop_word_removal_enabled(remove);
}

void Tokenizer::add_stop_word(const std::string& word) {
    processor_->add_stop_word(word);
}

void Tokenizer::add_stop_words(const std::vector<std::string>& words) {
    processor_->add_stop_words(words);
}

//=============================================================================
// Convenience methods (delegate to TextProcessor)
//=============================================================================
std::string Tokenizer::normalize(const std::string& token) const {
    return processor_->normalize(token);
}

std::string Tokenizer::stem(const std::string& word) const {
    return processor_->stem(word);
}

bool Tokenizer::is_stop_word(const std::string& word) const {
    return processor_->is_stop_word(word);
}

//=============================================================================
// Main tokenization with position tracking
//=============================================================================
std::vector<Token> Tokenizer::tokenize(const std::string& text) const {
    std::vector<Token> tokens;
    
    size_t i = 0;
    size_t token_position = 0;
    
    while (i < text.size()) {
        // Skip non-alphanumeric characters
        while (i < text.size() && !std::isalnum(static_cast<unsigned char>(text[i]))) {
            ++i;
        }
        
        if (i >= text.size()) break;
        
        // Find end of token
        size_t start = i;
        while (i < text.size() && std::isalnum(static_cast<unsigned char>(text[i]))) {
            ++i;
        }
        
        std::string original = text.substr(start, i - start);
        std::string normalized = processor_->normalize(original);
        
        // Apply length filters
        if (normalized.length() < min_token_length_ || 
            normalized.length() > max_token_length_) {
            continue;
        }
        
        // Apply stop word filter
        if (remove_stop_words_ && processor_->is_stop_word(normalized)) {
            continue;
        }
        
        // Apply stemming
        std::string final_token = apply_stemming_ ? processor_->stem(normalized) : normalized;
        
        if (!final_token.empty()) {
            tokens.push_back(Token{
                .text = final_token,
                .original = original,
                .position = token_position++,
                .char_offset = start
            });
        }
    }
    
    return tokens;
}

//=============================================================================
// Simple tokenization (strings only)
//=============================================================================
std::vector<std::string> Tokenizer::tokenize_simple(const std::string& text) const {
    auto tokens = tokenize(text);
    std::vector<std::string> result;
    result.reserve(tokens.size());
    
    for (const auto& token : tokens) {
        result.push_back(token.text);
    }
    
    return result;
}

//=============================================================================
// Query tokenization
// Similar to simple but may preserve more for phrase matching
//=============================================================================
std::vector<std::string> Tokenizer::tokenize_query(const std::string& query) const {
    std::vector<std::string> tokens;
    
    size_t i = 0;
    while (i < query.size()) {
        // Skip whitespace
        while (i < query.size() && std::isspace(static_cast<unsigned char>(query[i]))) {
            ++i;
        }
        
        if (i >= query.size()) break;
        
        // Find end of token
        size_t start = i;
        while (i < query.size() && !std::isspace(static_cast<unsigned char>(query[i]))) {
            ++i;
        }
        
        std::string token = query.substr(start, i - start);
        std::string normalized = processor_->normalize(token);
        
        if (!normalized.empty()) {
            // Apply stemming but keep stop words for phrase matching
            std::string final_token = apply_stemming_ ? processor_->stem(normalized) : normalized;
            if (!final_token.empty()) {
                tokens.push_back(final_token);
            }
        }
    }
    
    return tokens;
}

//=============================================================================
// N-gram generation
//=============================================================================
std::vector<std::string> Tokenizer::generate_bigrams(const std::vector<std::string>& tokens) const {
    std::vector<std::string> bigrams;
    
    if (tokens.size() < 2) {
        return bigrams;
    }
    
    bigrams.reserve(tokens.size() - 1);
    
    for (size_t i = 0; i < tokens.size() - 1; ++i) {
        bigrams.push_back(tokens[i] + "_" + tokens[i + 1]);
    }
    
    return bigrams;
}

std::vector<std::string> Tokenizer::generate_char_ngrams(const std::string& text, size_t n) const {
    std::vector<std::string> ngrams;
    
    std::string normalized = processor_->normalize(text);
    if (normalized.size() < n) {
        return ngrams;
    }
    
    ngrams.reserve(normalized.size() - n + 1);
    
    for (size_t i = 0; i <= normalized.size() - n; ++i) {
        ngrams.push_back(normalized.substr(i, n));
    }
    
    return ngrams;
}

} // namespace search