#include "indexer/ai_detector.h"
#include <cmath>
#include <algorithm>
#include <sstream>
#include <numeric>

namespace search {

AIDetector::AIDetector() {
    // AI-typical phrases (stemmed/lowercase)
    // These appear disproportionately in AI-generated text
    ai_phrases_ = {
        // Overused words
        "delve", "delves", "delving",
        "tapestry", "multifaceted", "nuanced",
        "comprehensive", "crucial", "vital",
        "realm", "landscape", "paradigm",
        "leverage", "leveraging", "utilize", "utilizing",
        "foster", "fostering", "facilitate",
        "enhance", "enhancing", "optimal",
        "robust", "seamless", "streamline",
        "pivotal", "intricate", "myriad",
        "plethora", "pertaining",
        
        // Filler phrases
        "it's important to note",
        "it is important to note",
        "it's worth noting",
        "it is worth noting",
        "in today's world",
        "in this day and age",
        "at the end of the day",
        "when it comes to",
        "in terms of",
        "on the other hand",
        "that being said",
        "having said that",
        "it goes without saying",
        
        // AI conclusions
        "in conclusion",
        "to summarize",
        "to sum up",
        "in summary",
        "all in all",
        "overall",
        
        // AI intros
        "let's dive in",
        "let's explore",
        "let's delve",
        "this article will",
        "this guide will",
        "in this article",
        "in this guide",
        "in this post",
        "welcome to this",
        
        // Hedging
        "it's essential",
        "it is essential",
        "play a crucial role",
        "plays a crucial role",
        "of paramount importance"
    };
    
    // Common AI paragraph starters
    ai_starters_ = {
        "furthermore",
        "additionally", 
        "moreover",
        "however",
        "consequently",
        "nevertheless",
        "subsequently",
        "therefore",
        "thus",
        "hence",
        "in addition",
        "as a result",
        "for instance",
        "for example",
        "in fact",
        "indeed",
        "notably",
        "importantly",
        "specifically",
        "particularly",
        "firstly",
        "secondly",
        "thirdly",
        "finally",
        "lastly",
        "to begin",
        "to start",
        "moving on",
        "next",
        "now"
    };
}

float AIDetector::calculate_score(const std::string& text) {
    if (text.length() < 200) {
        return 0.0f;  // Too short to analyze reliably
    }
    
    float vocab = vocabulary_score(text);
    float uniform = uniformity_score(text);
    float repeat = repetition_score(text);
    
    float combined = vocab_weight_ * vocab +
                    uniformity_weight_ * uniform +
                    repetition_weight_ * repeat;
    
    // Clamp to [0, 1]
    return std::max(0.0f, std::min(1.0f, combined));
}

float AIDetector::vocabulary_score(const std::string& text) {
    std::string lower = to_lower(text);
    
    int matches = 0;
    for (const auto& phrase : ai_phrases_) {
        size_t pos = 0;
        while ((pos = lower.find(phrase, pos)) != std::string::npos) {
            matches++;
            pos += phrase.length();
        }
    }
    
    // Normalize by text length (per 1000 chars)
    float density = (matches * 1000.0f) / static_cast<float>(text.length());
    
    // Score: 0-2 matches/1000 chars = low, 5+ = high
    // Map to 0-1 range with sigmoid-like curve
    float score = density / (density + 3.0f);
    
    return score;
}

float AIDetector::uniformity_score(const std::string& text) {
    auto sentences = split_sentences(text);
    
    if (sentences.size() < 5) {
        return 0.0f;  // Not enough data
    }
    
    // Calculate sentence lengths
    std::vector<float> lengths;
    lengths.reserve(sentences.size());
    for (const auto& s : sentences) {
        // Count words roughly
        int words = 1;
        for (char c : s) {
            if (c == ' ') words++;
        }
        lengths.push_back(static_cast<float>(words));
    }
    
    // Calculate mean
    float sum = std::accumulate(lengths.begin(), lengths.end(), 0.0f);
    float mean = sum / lengths.size();
    
    if (mean < 1.0f) return 0.0f;
    
    // Calculate standard deviation
    float sq_sum = 0.0f;
    for (float len : lengths) {
        sq_sum += (len - mean) * (len - mean);
    }
    float std_dev = std::sqrt(sq_sum / lengths.size());
    
    // Coefficient of variation (CV)
    float cv = std_dev / mean;
    
    // Human writing: CV typically 0.4-0.8
    // AI writing: CV typically 0.15-0.35
    // 
    // Map low CV to high score:
    // CV < 0.2 -> score ~0.9
    // CV = 0.4 -> score ~0.5
    // CV > 0.6 -> score ~0.2
    
    float score = 1.0f - (cv / 0.8f);
    return std::max(0.0f, std::min(1.0f, score));
}

float AIDetector::repetition_score(const std::string& text) {
    auto paragraphs = split_paragraphs(text);
    
    if (paragraphs.size() < 3) {
        return 0.0f;  // Not enough paragraphs
    }
    
    int ai_starter_count = 0;
    std::unordered_set<std::string> seen_starters;
    
    for (const auto& para : paragraphs) {
        std::string first = to_lower(get_first_words(para, 3));
        
        // Check if starts with AI-typical word
        for (const auto& starter : ai_starters_) {
            if (first.find(starter) == 0 || 
                first.find(" " + starter) != std::string::npos) {
                ai_starter_count++;
                seen_starters.insert(starter);
                break;
            }
        }
    }
    
    // Score based on:
    // 1. Ratio of paragraphs with AI starters
    // 2. Repetition of same starters
    
    float starter_ratio = static_cast<float>(ai_starter_count) / paragraphs.size();
    
    // Penalize more if starters are repetitive
    float uniqueness = seen_starters.empty() ? 1.0f : 
        static_cast<float>(seen_starters.size()) / ai_starter_count;
    
    // High starter_ratio + low uniqueness = AI-like
    float score = starter_ratio * (2.0f - uniqueness);
    
    return std::max(0.0f, std::min(1.0f, score));
}

void AIDetector::set_weights(float vocab, float uniformity, float repetition) {
    vocab_weight_ = vocab;
    uniformity_weight_ = uniformity;
    repetition_weight_ = repetition;
}

std::vector<std::string> AIDetector::split_sentences(const std::string& text) {
    std::vector<std::string> sentences;
    std::string current;
    
    for (size_t i = 0; i < text.length(); ++i) {
        char c = text[i];
        current += c;
        
        // End of sentence
        if (c == '.' || c == '!' || c == '?') {
            // Check it's not abbreviation (Mr. Dr. etc.)
            if (current.length() > 10) {
                // Trim whitespace
                size_t start = current.find_first_not_of(" \t\n");
                size_t end = current.find_last_not_of(" \t\n");
                if (start != std::string::npos) {
                    sentences.push_back(current.substr(start, end - start + 1));
                }
                current.clear();
            }
        }
    }
    
    // Don't forget last sentence without punctuation
    if (current.length() > 10) {
        size_t start = current.find_first_not_of(" \t\n");
        size_t end = current.find_last_not_of(" \t\n");
        if (start != std::string::npos) {
            sentences.push_back(current.substr(start, end - start + 1));
        }
    }
    
    return sentences;
}

std::vector<std::string> AIDetector::split_paragraphs(const std::string& text) {
    std::vector<std::string> paragraphs;
    std::istringstream stream(text);
    std::string line;
    std::string current;
    
    while (std::getline(stream, line)) {
        // Trim line
        size_t start = line.find_first_not_of(" \t");
        size_t end = line.find_last_not_of(" \t");
        
        if (start == std::string::npos) {
            // Empty line = paragraph break
            if (!current.empty()) {
                paragraphs.push_back(current);
                current.clear();
            }
        } else {
            if (!current.empty()) current += " ";
            current += line.substr(start, end - start + 1);
        }
    }
    
    if (!current.empty()) {
        paragraphs.push_back(current);
    }
    
    return paragraphs;
}

std::string AIDetector::to_lower(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    for (char c : s) {
        result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return result;
}

std::string AIDetector::get_first_words(const std::string& text, int n) {
    std::string result;
    int words = 0;
    
    for (char c : text) {
        if (c == ' ' || c == '\t' || c == '\n') {
            if (!result.empty() && result.back() != ' ') {
                result += ' ';
                words++;
                if (words >= n) break;
            }
        } else {
            result += c;
        }
    }
    
    return result;
}

} // namespace search