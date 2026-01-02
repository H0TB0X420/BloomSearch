#pragma once

#include "common/search_types.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace search {

//=============================================================================
// Index interface (abstract - implemented by RocksDB client)
//=============================================================================
class IndexReader {
public:
    virtual ~IndexReader() = default;
    
    // Get posting list for a term
    virtual std::vector<Posting> get_postings(const std::string& term) const = 0;
    
    // Get document info by ID
    virtual std::optional<DocumentInfo> get_document(uint64_t doc_id) const = 0;
    
    // Get document frequency (number of docs containing term)
    virtual uint32_t get_doc_frequency(const std::string& term) const = 0;
    
    // Get total number of documents in index
    virtual uint64_t get_total_docs() const = 0;
    
    // Get average document length
    virtual float get_avg_doc_length() const = 0;
};

//=============================================================================
// Ranker
// Scores and ranks search results using BM25 with AI score integration
//=============================================================================
class Ranker {
public:
    Ranker();
    explicit Ranker(std::shared_ptr<IndexReader> index);
    ~Ranker() = default;
    
    //=========================================================================
    // Main ranking method - returns complete SearchResponse
    //=========================================================================
    SearchResponse rank(const ParsedQuery& query, size_t max_results = 100);
    
    //=========================================================================
    // Configuration
    //=========================================================================
    
    // BM25 parameters
    void set_k1(float k1) { k1_ = k1; }
    void set_b(float b) { b_ = b; }
    float k1() const { return k1_; }
    float b() const { return b_; }
    
    // AI penalty weight (0.0 = no penalty, 1.0 = full penalty)
    void set_ai_penalty_weight(float weight) { ai_penalty_weight_ = weight; }
    float ai_penalty_weight() const { return ai_penalty_weight_; }
    
    // Set index reader
    void set_index(std::shared_ptr<IndexReader> index) { index_ = index; }
    
    //=========================================================================
    // Scoring methods (public for testing)
    //=========================================================================
    
    // Calculate BM25 score for a single term in a document
    float bm25_term_score(
        uint32_t term_freq,
        uint32_t doc_length,
        uint32_t doc_freq,
        uint64_t total_docs,
        float avg_doc_length
    ) const;
    
    // Calculate IDF (Inverse Document Frequency)
    float idf(uint32_t doc_freq, uint64_t total_docs) const;
    
    // Apply AI penalty to relevance score
    float apply_ai_penalty(float relevance_score, float ai_score) const;

private:
    std::shared_ptr<IndexReader> index_;
    
    // BM25 parameters
    float k1_ = 1.2f;   // Term frequency saturation
    float b_ = 0.75f;   // Length normalization
    
    // AI penalty
    float ai_penalty_weight_ = 0.2f;  // 20% max penalty for AI content
    
    //=========================================================================
    // Internal methods
    //=========================================================================
    
    // Collect candidate documents from posting lists
    std::unordered_map<uint64_t, std::unordered_map<std::string, Posting>> 
    collect_candidates(const std::vector<std::string>& terms);
    
    // Score a single document
    float score_document(
        uint64_t doc_id,
        const std::unordered_map<std::string, Posting>& term_postings,
        const std::vector<std::string>& query_terms,
        uint32_t doc_length,
        std::unordered_map<std::string, float>& term_scores_out
    );
    
    // Check if document matches phrase query
    bool matches_phrase(
        const std::vector<uint32_t>& positions1,
        const std::vector<uint32_t>& positions2
    ) const;
    
    // Check if document matches all phrases in query
    bool check_phrases(
        const std::vector<std::string>& phrases,
        const std::unordered_map<std::string, Posting>& term_postings
    ) const;
    
    // Apply filters to a document
    bool passes_filters(const DocumentInfo& doc, const ParsedQuery& query) const;
    
    // Check if document contains excluded terms
    bool has_excluded_terms(
        const std::vector<std::string>& excluded,
        const std::unordered_map<std::string, Posting>& term_postings
    ) const;
};

} // namespace search