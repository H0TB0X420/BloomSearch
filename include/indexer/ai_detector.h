#pragma once

#include <string>
#include <vector>
#include <unordered_set>
#include <cstdint>

namespace search {

/**
 * AIDetector - Heuristic-based AI content detection
 * 
 * Combines three signals:
 * 1. Vocabulary analysis - AI-typical phrases
 * 2. Sentence uniformity - AI has low variance in sentence length
 * 3. Paragraph starters - AI repeats transitional phrases
 * 
 * Output: 0.0 (likely human) to 1.0 (likely AI)
 */
class AIDetector {
public:
    AIDetector();
    
    /**
     * Calculate combined AI likelihood score
     * @param text Plain text content (not HTML)
     * @return Score from 0.0 (human) to 1.0 (AI)
     */
    float calculate_score(const std::string& text);
    
    // Individual signal scores (for debugging/tuning)
    float vocabulary_score(const std::string& text);
    float uniformity_score(const std::string& text);
    float repetition_score(const std::string& text);
    
private:
    float vocab_weight_ = 0.40f;
    float uniformity_weight_ = 0.35f;
    float repetition_weight_ = 0.25f;
    
    // AI-typical vocabulary
    std::unordered_set<std::string> ai_phrases_;
    std::unordered_set<std::string> ai_starters_;
    
    // Helpers
    std::vector<std::string> split_sentences(const std::string& text);
    std::vector<std::string> split_paragraphs(const std::string& text);
    std::string to_lower(const std::string& s);
    std::string get_first_words(const std::string& text, int n);
};

} // namespace search