#pragma once

#include <string>
#include <vector>
#include <unordered_set>

namespace search {

//=============================================================================
// TextProcessor
// Shared text processing utilities for indexing and query parsing
// Ensures consistent tokenization, stemming, and normalization
//=============================================================================
class TextProcessor {
public:
    TextProcessor();
    ~TextProcessor() = default;
    
    //=========================================================================
    // Normalization
    //=========================================================================
    
    // Lowercase and strip non-alphanumeric characters
    std::string normalize(const std::string& text) const;
    
    // Just lowercase (preserves punctuation)
    std::string to_lower(const std::string& text) const;
    
    //=========================================================================
    // Stemming (Porter Stemmer)
    //=========================================================================
    
    // Apply Porter stemming algorithm
    std::string stem(const std::string& word) const;
    
    //=========================================================================
    // Stop Words
    //=========================================================================
    
    // Check if word is a stop word
    bool is_stop_word(const std::string& word) const;
    
    // Add custom stop word
    void add_stop_word(const std::string& word);
    
    // Add multiple stop words
    void add_stop_words(const std::vector<std::string>& words);
    
    // Remove a stop word
    void remove_stop_word(const std::string& word);
    
    // Clear all stop words
    void clear_stop_words();
    
    // Get current stop words (for debugging/testing)
    const std::unordered_set<std::string>& stop_words() const { return stop_words_; }
    
    //=========================================================================
    // Basic Tokenization
    //=========================================================================
    
    // Split text into tokens (lowercase, alphanumeric only)
    std::vector<std::string> tokenize(const std::string& text) const;
    
    // Tokenize with all processing (normalize + stem + remove stop words)
    std::vector<std::string> process(const std::string& text) const;
    
    // Process a single word (normalize + stem, no stop word removal)
    std::string process_word(const std::string& word) const;
    
    //=========================================================================
    // Configuration
    //=========================================================================
    
    void set_min_token_length(size_t len) { min_token_length_ = len; }
    void set_max_token_length(size_t len) { max_token_length_ = len; }
    void set_stemming_enabled(bool enabled) { stemming_enabled_ = enabled; }
    void set_stop_word_removal_enabled(bool enabled) { stop_word_removal_enabled_ = enabled; }
    
    size_t min_token_length() const { return min_token_length_; }
    size_t max_token_length() const { return max_token_length_; }
    bool stemming_enabled() const { return stemming_enabled_; }
    bool stop_word_removal_enabled() const { return stop_word_removal_enabled_; }

private:
    // Configuration
    size_t min_token_length_ = 1;
    size_t max_token_length_ = 100;
    bool stemming_enabled_ = true;
    bool stop_word_removal_enabled_ = true;
    
    // Stop words
    std::unordered_set<std::string> stop_words_;
    void init_default_stop_words();
    
    //=========================================================================
    // Porter Stemmer Implementation
    //=========================================================================
    std::string porter_stem(const std::string& word) const;
    
    // Helper functions for Porter stemmer
    bool is_consonant(const std::string& word, size_t index) const;
    size_t measure(const std::string& word) const;
    bool has_vowel(const std::string& word) const;
    bool ends_double_consonant(const std::string& word) const;
    bool ends_cvc(const std::string& word) const;
    
    // Porter stemmer steps
    std::string step1a(const std::string& word) const;
    std::string step1b(const std::string& word) const;
    std::string step1c(const std::string& word) const;
    std::string step2(const std::string& word) const;
    std::string step3(const std::string& word) const;
    std::string step4(const std::string& word) const;
    std::string step5(const std::string& word) const;
};

} // namespace search