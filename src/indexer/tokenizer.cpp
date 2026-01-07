#include "indexer/tokenizer.h"
#include <algorithm>
#include <cctype>

namespace search {

Tokenizer::Tokenizer() 
    : processor_(std::make_shared<TextProcessor>()) {
}

Tokenizer::Tokenizer(std::shared_ptr<TextProcessor> processor)
    : processor_(processor ? processor : std::make_shared<TextProcessor>()) {
}

void Tokenizer::set_min_token_length(size_t len) {
    min_token_length_ = len;
    processor_->set_min_token_length(len);
}

void Tokenizer::set_max_token_length(size_t len) {
    max_token_length_ = len;
    processor_->set_max_token_length(len);
}

void Tokenizer::add_stop_word(const std::string& word) {
    processor_->add_stop_word(word);
}

void Tokenizer::add_stop_words(const std::vector<std::string>& words) {
    processor_->add_stop_words(words);
}

std::string Tokenizer::normalize(const std::string& token) const {
    return processor_->normalize(token);
}

std::string Tokenizer::stem(const std::string& word) const {
    return processor_->stem(word);
}

bool Tokenizer::is_stop_word(const std::string& word) const {
    return processor_->is_stop_word(word);
}

std::vector<Token> Tokenizer::tokenize(const std::string& text) const {
    std::vector<Token> tokens;
    
    size_t i = 0;
    size_t token_position = 0;
    
    while (i < text.size()) {
        while (i < text.size() && !std::isalnum(static_cast<unsigned char>(text[i]))) {
            ++i;
        }
        
        if (i >= text.size()) break;
        
        size_t start = i;
        while (i < text.size() && std::isalnum(static_cast<unsigned char>(text[i]))) {
            ++i;
        }
        
        std::string original = text.substr(start, i - start);
        std::string normalized = processor_->normalize(original);
        
        if (normalized.length() < min_token_length_ || 
            normalized.length() > max_token_length_) {
            continue;
        }
        
        if (remove_stop_words_ && processor_->is_stop_word(normalized)) {
            continue;
        }
        
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

} // namespace search