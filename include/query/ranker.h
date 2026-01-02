#pragma once

#include "common/search_types.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace search {

class IndexReader {
public:
    virtual ~IndexReader() = default;
    
    virtual std::vector<Posting> get_postings(const std::string& term) const = 0;
    
    virtual std::optional<DocumentInfo> get_document(uint64_t doc_id) const = 0;
    
    virtual uint32_t get_doc_frequency(const std::string& term) const = 0;
    
    virtual uint64_t get_total_docs() const = 0;
    
    virtual float get_avg_doc_length() const = 0;
};

class Ranker {
public:
    Ranker();
    explicit Ranker(std::shared_ptr<IndexReader> index);
    ~Ranker() = default;
    
    SearchResponse rank(const ParsedQuery& query, size_t max_results = 100);
    
    void set_k1(float k1) { k1_ = k1; }
    void set_b(float b) { b_ = b; }
    float k1() const { return k1_; }
    float b() const { return b_; }
    
    void set_ai_penalty_weight(float weight) { ai_penalty_weight_ = weight; }
    float ai_penalty_weight() const { return ai_penalty_weight_; }
    
    void set_index(std::shared_ptr<IndexReader> index) { index_ = index; }
    
    float bm25_term_score(
        uint32_t term_freq,
        uint32_t doc_length,
        uint32_t doc_freq,
        uint64_t total_docs,
        float avg_doc_length
    ) const;
    
    float idf(uint32_t doc_freq, uint64_t total_docs) const;
    
    float apply_ai_penalty(float relevance_score, float ai_score) const;

private:
    std::shared_ptr<IndexReader> index_;
    
    float k1_ = 1.2f;   // Term frequency saturation
    float b_ = 0.75f;   // Length normalization
    
    // AI penalty
    float ai_penalty_weight_ = 0.2f;  // 20% max penalty for AI content
    
    std::unordered_map<uint64_t, std::unordered_map<std::string, Posting>> 
    collect_candidates(const std::vector<std::string>& terms);
    
    float score_document(
        uint64_t doc_id,
        const std::unordered_map<std::string, Posting>& term_postings,
        const std::vector<std::string>& query_terms,
        uint32_t doc_length,
        std::unordered_map<std::string, float>& term_scores_out
    );
    
    bool matches_phrase(
        const std::vector<uint32_t>& positions1,
        const std::vector<uint32_t>& positions2
    ) const;
    
    bool check_phrases(
        const std::vector<std::string>& phrases,
        const std::unordered_map<std::string, Posting>& term_postings
    ) const;
    
    bool passes_filters(const DocumentInfo& doc, const ParsedQuery& query) const;
    
    bool has_excluded_terms(
        const std::vector<std::string>& excluded,
        const std::unordered_map<std::string, Posting>& term_postings
    ) const;
};

} // namespace search