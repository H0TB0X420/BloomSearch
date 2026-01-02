#pragma once

#include "common/text_processor.h"
#include <string>
#include <vector>
#include <memory>

namespace search {

struct Token {
    std::string text;       
    std::string original;   
    size_t position;        
    size_t char_offset;     
};

class Tokenizer {
public:
    Tokenizer();
    explicit Tokenizer(std::shared_ptr<TextProcessor> processor);
    ~Tokenizer() = default;
    
    std::vector<Token> tokenize(const std::string& text) const;
    
    std::vector<std::string> tokenize_simple(const std::string& text) const;
    
    std::vector<std::string> tokenize_query(const std::string& query) const;
    
    std::vector<std::string> generate_bigrams(const std::vector<std::string>& tokens) const;
    
    std::vector<std::string> generate_char_ngrams(const std::string& text, size_t n = 3) const;
    
    void set_min_token_length(size_t len);
    void set_max_token_length(size_t len);
    void set_apply_stemming(bool apply);
    void set_remove_stop_words(bool remove);
    
    void add_stop_word(const std::string& word);
    void add_stop_words(const std::vector<std::string>& words);
    
    TextProcessor& processor() { return *processor_; }
    const TextProcessor& processor() const { return *processor_; }
    
    std::string normalize(const std::string& token) const;
    std::string stem(const std::string& word) const;
    bool is_stop_word(const std::string& word) const;

private:
    std::shared_ptr<TextProcessor> processor_;
    
    bool remove_stop_words_ = true;
    bool apply_stemming_ = true;
    size_t min_token_length_ = 1;
    size_t max_token_length_ = 100;
};

} // namespace search