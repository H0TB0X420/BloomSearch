#pragma once

#include <string>
#include <vector>
#include <unordered_set>

namespace search {

class TextProcessor {
public:
    TextProcessor();
    ~TextProcessor() = default;
    
    std::string normalize(const std::string& text) const;
    
    std::string to_lower(const std::string& text) const;
    
    std::string stem(const std::string& word) const;
    
    bool is_stop_word(const std::string& word) const;
    
    void add_stop_word(const std::string& word);
    
    void add_stop_words(const std::vector<std::string>& words);
    
    const std::unordered_set<std::string>& stop_words() const { return stop_words_; }
    
    std::vector<std::string> tokenize(const std::string& text) const;
        
    std::string process_word(const std::string& word) const;
    
    void set_min_token_length(size_t len) { min_token_length_ = len; }
    void set_max_token_length(size_t len) { max_token_length_ = len; }
    void set_stemming_enabled(bool enabled) { stemming_enabled_ = enabled; }
    void set_stop_word_removal_enabled(bool enabled) { stop_word_removal_enabled_ = enabled; }
    
    size_t min_token_length() const { return min_token_length_; }
    size_t max_token_length() const { return max_token_length_; }
    bool stemming_enabled() const { return stemming_enabled_; }
    bool stop_word_removal_enabled() const { return stop_word_removal_enabled_; }

private:
    size_t min_token_length_ = 1;
    size_t max_token_length_ = 100;
    bool stemming_enabled_ = true;
    bool stop_word_removal_enabled_ = true;
    
    std::unordered_set<std::string> stop_words_;
    void init_default_stop_words();
    
    std::string porter_stem(const std::string& word) const;
    
    bool is_consonant(const std::string& word, size_t index) const;
    size_t measure(const std::string& word) const;
    bool has_vowel(const std::string& word) const;
    bool ends_double_consonant(const std::string& word) const;
    bool ends_cvc(const std::string& word) const;
    
    std::string step1a(const std::string& word) const;
    std::string step1b(const std::string& word) const;
    std::string step1c(const std::string& word) const;
    std::string step2(const std::string& word) const;
    std::string step3(const std::string& word) const;
    std::string step4(const std::string& word) const;
    std::string step5(const std::string& word) const;
};

} // namespace search