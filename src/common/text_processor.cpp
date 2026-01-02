#include "common/text_processor.h"
#include <algorithm>
#include <cctype>

namespace search {

TextProcessor::TextProcessor() {
    init_default_stop_words();
}

void TextProcessor::init_default_stop_words() {
    std::vector<std::string> defaults = {
        // Articles
        "a", "an", "the",
        // Pronouns
        "i", "me", "my", "myself", "we", "our", "ours", "ourselves",
        "you", "your", "yours", "yourself", "yourselves",
        "he", "him", "his", "himself", "she", "her", "hers", "herself",
        "it", "its", "itself", "they", "them", "their", "theirs", "themselves",
        "what", "which", "who", "whom", "this", "that", "these", "those",
        // Verbs (common)
        "am", "is", "are", "was", "were", "be", "been", "being",
        "have", "has", "had", "having", "do", "does", "did", "doing",
        "would", "should", "could", "ought", "might", "must", "shall", "will", "can",
        // Prepositions
        "at", "by", "for", "from", "in", "into", "of", "on", "to", "with",
        "about", "against", "between", "through", "during", "before", "after",
        "above", "below", "up", "down", "out", "off", "over", "under",
        // Conjunctions
        "and", "but", "if", "or", "because", "as", "until", "while",
        "although", "though", "unless", "since", "when", "where", "why", "how",
        // Other common words
        "all", "each", "every", "both", "few", "more", "most", "other",
        "some", "such", "no", "nor", "not", "only", "own", "same", "so",
        "than", "too", "very", "just", "also", "now", "here", "there", "then",
        // Additional
        "need", "any", "many", "much", "even", "back", "well", "way",
        "want", "get", "got", "go", "going", "make", "made", "like",
        // Web-specific
        "http", "https", "www", "com", "org", "net", "html", "htm"
    };
    
    for (const auto& word : defaults) {
        stop_words_.insert(word);
    }
}

void TextProcessor::add_stop_word(const std::string& word) {
    stop_words_.insert(normalize(word));
}

void TextProcessor::add_stop_words(const std::vector<std::string>& words) {
    for (const auto& word : words) {
        add_stop_word(word);
    }
}

void TextProcessor::remove_stop_word(const std::string& word) {
    stop_words_.erase(normalize(word));
}

void TextProcessor::clear_stop_words() {
    stop_words_.clear();
}

bool TextProcessor::is_stop_word(const std::string& word) const {
    return stop_words_.count(word) > 0;
}

std::string TextProcessor::normalize(const std::string& text) const {
    std::string result;
    result.reserve(text.size());
    
    for (char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            result += std::tolower(static_cast<unsigned char>(c));
        }
    }
    
    return result;
}

std::string TextProcessor::to_lower(const std::string& text) const {
    std::string result = text;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

std::vector<std::string> TextProcessor::tokenize(const std::string& text) const {
    std::vector<std::string> tokens;
    
    size_t i = 0;
    while (i < text.size()) {
        // Skip non-alphanumeric
        while (i < text.size() && !std::isalnum(static_cast<unsigned char>(text[i]))) {
            ++i;
        }
        
        if (i >= text.size()) break;
        
        // Find end of token
        size_t start = i;
        while (i < text.size() && std::isalnum(static_cast<unsigned char>(text[i]))) {
            ++i;
        }
        
        std::string token;
        for (size_t j = start; j < i; ++j) {
            token += std::tolower(static_cast<unsigned char>(text[j]));
        }
        
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }
    
    return tokens;
}

std::vector<std::string> TextProcessor::process(const std::string& text) const {
    std::vector<std::string> result;
    auto tokens = tokenize(text);
    
    for (const auto& token : tokens) {
        // Length filter
        if (token.length() < min_token_length_ || 
            token.length() > max_token_length_) {
            continue;
        }
        
        // Stop word filter
        if (stop_word_removal_enabled_ && is_stop_word(token)) {
            continue;
        }
        
        // Stemming
        std::string processed = stemming_enabled_ ? stem(token) : token;
        
        if (!processed.empty()) {
            result.push_back(processed);
        }
    }
    
    return result;
}

std::string TextProcessor::process_word(const std::string& word) const {
    std::string normalized = normalize(word);
    if (normalized.empty()) {
        return "";
    }
    return stemming_enabled_ ? stem(normalized) : normalized;
}

std::string TextProcessor::stem(const std::string& word) const {
    if (word.length() < 3) {
        return word;  // Don't stem very short words
    }
    return porter_stem(word);
}

//=============================================================================
// Porter Stemmer Implementation
// Based on the original algorithm by Martin Porter (1980)
// Reference: https://tartarus.org/martin/PorterStemmer/
//=============================================================================

bool TextProcessor::is_consonant(const std::string& word, size_t i) const {
    if (i >= word.length()) return false;
    
    char c = word[i];
    switch (c) {
        case 'a': case 'e': case 'i': case 'o': case 'u':
            return false;
        case 'y':
            return (i == 0) ? true : !is_consonant(word, i - 1);
        default:
            return true;
    }
}

size_t TextProcessor::measure(const std::string& word) const {
    // Count VC (vowel-consonant) sequences
    size_t m = 0;
    size_t i = 0;
    size_t len = word.length();
    
    // Skip initial consonants
    while (i < len && is_consonant(word, i)) ++i;
    
    while (i < len) {
        // Skip vowels
        while (i < len && !is_consonant(word, i)) ++i;
        if (i >= len) break;
        
        // Skip consonants
        while (i < len && is_consonant(word, i)) ++i;
        ++m;
    }
    
    return m;
}

bool TextProcessor::has_vowel(const std::string& word) const {
    for (size_t i = 0; i < word.length(); ++i) {
        if (!is_consonant(word, i)) return true;
    }
    return false;
}

bool TextProcessor::ends_double_consonant(const std::string& word) const {
    size_t len = word.length();
    if (len < 2) return false;
    return word[len - 1] == word[len - 2] && is_consonant(word, len - 1);
}

bool TextProcessor::ends_cvc(const std::string& word) const {
    size_t len = word.length();
    if (len < 3) return false;
    
    if (!is_consonant(word, len - 1)) return false;
    if (is_consonant(word, len - 2)) return false;
    if (!is_consonant(word, len - 3)) return false;
    
    // Check if last consonant is w, x, or y
    char c = word[len - 1];
    if (c == 'w' || c == 'x' || c == 'y') return false;
    
    return true;
}

std::string TextProcessor::step1a(const std::string& word) const {
    std::string result = word;
    size_t len = result.length();
    
    if (len > 4 && result.substr(len - 4) == "sses") {
        result = result.substr(0, len - 2);  // SSES -> SS
    } else if (len > 3 && result.substr(len - 3) == "ies") {
        result = result.substr(0, len - 2);  // IES -> I
    } else if (len > 2 && result.substr(len - 2) == "ss") {
        // SS -> SS (do nothing)
    } else if (len > 1 && result[len - 1] == 's') {
        result = result.substr(0, len - 1);  // S -> (remove)
    }
    
    return result;
}

std::string TextProcessor::step1b(const std::string& word) const {
    std::string result = word;
    size_t len = result.length();
    bool flag = false;
    
    if (len > 3 && result.substr(len - 3) == "eed") {
        std::string stem = result.substr(0, len - 3);
        if (measure(stem) > 0) {
            result = result.substr(0, len - 1);  // EED -> EE
        }
    } else if (len > 2 && result.substr(len - 2) == "ed") {
        std::string stem = result.substr(0, len - 2);
        if (has_vowel(stem)) {
            result = stem;  // ED -> (remove if stem has vowel)
            flag = true;
        }
    } else if (len > 3 && result.substr(len - 3) == "ing") {
        std::string stem = result.substr(0, len - 3);
        if (has_vowel(stem)) {
            result = stem;  // ING -> (remove if stem has vowel)
            flag = true;
        }
    }
    
    // If ED or ING was removed, apply additional rules
    if (flag) {
        len = result.length();
        if (len > 2 && result.substr(len - 2) == "at") {
            result += "e";  // AT -> ATE
        } else if (len > 2 && result.substr(len - 2) == "bl") {
            result += "e";  // BL -> BLE
        } else if (len > 2 && result.substr(len - 2) == "iz") {
            result += "e";  // IZ -> IZE
        } else if (ends_double_consonant(result)) {
            char c = result[len - 1];
            if (c != 'l' && c != 's' && c != 'z') {
                result = result.substr(0, len - 1);  // Remove double consonant
            }
        } else if (measure(result) == 1 && ends_cvc(result)) {
            result += "e";  // Add E if m=1 and CVC
        }
    }
    
    return result;
}

std::string TextProcessor::step1c(const std::string& word) const {
    std::string result = word;
    size_t len = result.length();
    
    if (len > 1 && result[len - 1] == 'y') {
        std::string stem = result.substr(0, len - 1);
        if (has_vowel(stem)) {
            result = stem + "i";  // Y -> I if stem has vowel
        }
    }
    
    return result;
}

std::string TextProcessor::step2(const std::string& word) const {
    std::string result = word;
    size_t len = result.length();
    
    // (m > 0) suffix replacements
    static const std::vector<std::pair<std::string, std::string>> suffixes = {
        {"ational", "ate"}, {"tional", "tion"}, {"enci", "ence"},
        {"anci", "ance"}, {"izer", "ize"}, {"abli", "able"},
        {"alli", "al"}, {"entli", "ent"}, {"eli", "e"},
        {"ousli", "ous"}, {"ization", "ize"}, {"ation", "ate"},
        {"ator", "ate"}, {"alism", "al"}, {"iveness", "ive"},
        {"fulness", "ful"}, {"ousness", "ous"}, {"aliti", "al"},
        {"iviti", "ive"}, {"biliti", "ble"}
    };
    
    for (const auto& [suffix, replacement] : suffixes) {
        if (len > suffix.length() && 
            result.substr(len - suffix.length()) == suffix) {
            std::string stem = result.substr(0, len - suffix.length());
            if (measure(stem) > 0) {
                result = stem + replacement;
            }
            break;
        }
    }
    
    return result;
}

std::string TextProcessor::step3(const std::string& word) const {
    std::string result = word;
    size_t len = result.length();
    
    // (m > 0) suffix replacements
    static const std::vector<std::pair<std::string, std::string>> suffixes = {
        {"icate", "ic"}, {"ative", ""}, {"alize", "al"},
        {"iciti", "ic"}, {"ical", "ic"}, {"ful", ""},
        {"ness", ""}
    };
    
    for (const auto& [suffix, replacement] : suffixes) {
        if (len > suffix.length() && 
            result.substr(len - suffix.length()) == suffix) {
            std::string stem = result.substr(0, len - suffix.length());
            if (measure(stem) > 0) {
                result = stem + replacement;
            }
            break;
        }
    }
    
    return result;
}

std::string TextProcessor::step4(const std::string& word) const {
    std::string result = word;
    size_t len = result.length();
    
    // (m > 1) suffix removal
    static const std::vector<std::string> suffixes = {
        "al", "ance", "ence", "er", "ic", "able", "ible", "ant",
        "ement", "ment", "ent", "ion", "ou", "ism", "ate", "iti",
        "ous", "ive", "ize"
    };
    
    for (const auto& suffix : suffixes) {
        if (len > suffix.length() && 
            result.substr(len - suffix.length()) == suffix) {
            std::string stem = result.substr(0, len - suffix.length());
            
            // Special case for "ion" - must be preceded by 's' or 't'
            if (suffix == "ion" && !stem.empty()) {
                char c = stem[stem.length() - 1];
                if (c != 's' && c != 't') {
                    continue;
                }
            }
            
            if (measure(stem) > 1) {
                result = stem;
            }
            break;
        }
    }
    
    return result;
}

std::string TextProcessor::step5(const std::string& word) const {
    std::string result = word;
    size_t len = result.length();
    
    // Step 5a: Remove final 'e'
    if (len > 1 && result[len - 1] == 'e') {
        std::string stem = result.substr(0, len - 1);
        size_t m = measure(stem);
        if (m > 1 || (m == 1 && !ends_cvc(stem))) {
            result = stem;
        }
    }
    
    // Step 5b: Remove double 'l'
    len = result.length();
    if (len > 1 && result[len - 1] == 'l' && 
        ends_double_consonant(result) && measure(result) > 1) {
        result = result.substr(0, len - 1);
    }
    
    return result;
}

std::string TextProcessor::porter_stem(const std::string& word) const {
    std::string result = word;
    
    result = step1a(result);
    result = step1b(result);
    result = step1c(result);
    result = step2(result);
    result = step3(result);
    result = step4(result);
    result = step5(result);
    
    return result;
}

} // namespace search