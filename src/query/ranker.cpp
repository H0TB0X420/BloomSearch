#include "query/ranker.h"
#include <algorithm>
#include <cmath>
#include <chrono>

namespace search {

//=============================================================================
// Constructor
//=============================================================================
Ranker::Ranker() : index_(nullptr) {}

Ranker::Ranker(std::shared_ptr<IndexReader> index) : index_(index) {}

//=============================================================================
// BM25 Scoring
//=============================================================================

float Ranker::idf(uint32_t doc_freq, uint64_t total_docs) const {
    if (doc_freq == 0 || total_docs == 0) {
        return 0.0f;
    }
    
    // BM25 IDF with positive guarantee (Lucene/Elasticsearch variant)
    // IDF = log(1 + (N - df + 0.5) / (df + 0.5))
    // The +1 inside log ensures IDF is always positive
    float n = static_cast<float>(total_docs);
    float df = static_cast<float>(doc_freq);
    
    return std::log(1.0f + (n - df + 0.5f) / (df + 0.5f));
}

float Ranker::bm25_term_score(
    uint32_t term_freq,
    uint32_t doc_length,
    uint32_t doc_freq,
    uint64_t total_docs,
    float avg_doc_length
) const {
    if (term_freq == 0 || doc_freq == 0 || total_docs == 0) {
        return 0.0f;
    }
    
    float tf = static_cast<float>(term_freq);
    float dl = static_cast<float>(doc_length);
    float avgdl = avg_doc_length > 0 ? avg_doc_length : 1.0f;
    
    // IDF component
    float idf_score = idf(doc_freq, total_docs);
    
    // TF component with length normalization
    // TF = (f * (k1 + 1)) / (f + k1 * (1 - b + b * dl/avgdl))
    float length_norm = 1.0f - b_ + b_ * (dl / avgdl);
    float tf_component = (tf * (k1_ + 1.0f)) / (tf + k1_ * length_norm);
    
    return idf_score * tf_component;
}

float Ranker::apply_ai_penalty(float relevance_score, float ai_score) const {
    // Linear penalty based on AI score
    // penalty = ai_score * weight
    // final = relevance * (1 - penalty)
    float penalty = ai_score * ai_penalty_weight_;
    return relevance_score * (1.0f - penalty);
}

//=============================================================================
// Candidate Collection
//=============================================================================

std::unordered_map<uint64_t, std::unordered_map<std::string, Posting>>
Ranker::collect_candidates(const std::vector<std::string>& terms) {
    std::unordered_map<uint64_t, std::unordered_map<std::string, Posting>> candidates;
    
    if (!index_) return candidates;
    
    for (const auto& term : terms) {
        auto postings = index_->get_postings(term);
        for (const auto& posting : postings) {
            candidates[posting.doc_id][term] = posting;
        }
    }
    
    return candidates;
}

//=============================================================================
// Document Scoring
//=============================================================================

float Ranker::score_document(
    uint64_t doc_id,
    const std::unordered_map<std::string, Posting>& term_postings,
    const std::vector<std::string>& query_terms,
    uint32_t doc_length,
    std::unordered_map<std::string, float>& term_scores_out
) {
    if (!index_) return 0.0f;
    
    float total_score = 0.0f;
    uint64_t total_docs = index_->get_total_docs();
    float avg_doc_length = index_->get_avg_doc_length();
    
    for (const auto& term : query_terms) {
        auto it = term_postings.find(term);
        if (it == term_postings.end()) {
            term_scores_out[term] = 0.0f;
            continue;
        }
        
        const Posting& posting = it->second;
        uint32_t doc_freq = index_->get_doc_frequency(term);
        
        float term_score = bm25_term_score(
            posting.term_frequency,
            doc_length,
            doc_freq,
            total_docs,
            avg_doc_length
        );
        
        term_scores_out[term] = term_score;
        total_score += term_score;
    }
    
    return total_score;
}

//=============================================================================
// Phrase Matching
//=============================================================================

bool Ranker::matches_phrase(
    const std::vector<uint32_t>& positions1,
    const std::vector<uint32_t>& positions2
) const {
    // Check if any position in positions2 is exactly 1 after any position in positions1
    for (uint32_t pos1 : positions1) {
        for (uint32_t pos2 : positions2) {
            if (pos2 == pos1 + 1) {
                return true;
            }
        }
    }
    return false;
}

bool Ranker::check_phrases(
    const std::vector<std::string>& phrases,
    const std::unordered_map<std::string, Posting>& term_postings
) const {
    // For each phrase, check if words appear consecutively
    for (const auto& phrase : phrases) {
        // Tokenize the phrase (simple split by space)
        std::vector<std::string> phrase_terms;
        std::string current;
        for (char c : phrase) {
            if (c == ' ') {
                if (!current.empty()) {
                    phrase_terms.push_back(current);
                    current.clear();
                }
            } else {
                current += c;
            }
        }
        if (!current.empty()) {
            phrase_terms.push_back(current);
        }
        
        if (phrase_terms.size() < 2) {
            // Single word phrase - just check if term exists
            if (phrase_terms.size() == 1) {
                if (term_postings.find(phrase_terms[0]) == term_postings.end()) {
                    return false;
                }
            }
            continue;
        }
        
        // Check consecutive positions for each pair
        bool phrase_found = true;
        for (size_t i = 0; i < phrase_terms.size() - 1; ++i) {
            auto it1 = term_postings.find(phrase_terms[i]);
            auto it2 = term_postings.find(phrase_terms[i + 1]);
            
            if (it1 == term_postings.end() || it2 == term_postings.end()) {
                phrase_found = false;
                break;
            }
            
            if (!matches_phrase(it1->second.positions, it2->second.positions)) {
                phrase_found = false;
                break;
            }
        }
        
        if (!phrase_found) {
            return false;
        }
    }
    
    return true;
}

//=============================================================================
// Filter Application
//=============================================================================

bool Ranker::passes_filters(const DocumentInfo& doc, const ParsedQuery& query) const {
    // Era filter
    if (query.era_filter.has_value()) {
        if (doc.era != *query.era_filter) {
            return false;
        }
    }
    
    // AI score filters
    if (query.max_ai_score.has_value()) {
        if (doc.ai_score > *query.max_ai_score) {
            return false;
        }
    }
    
    if (query.min_ai_score.has_value()) {
        if (doc.ai_score < *query.min_ai_score) {
            return false;
        }
    }
    
    // Site filter
    if (query.site_filter.has_value()) {
        // Check if document domain matches or is subdomain of filter
        const std::string& filter_domain = *query.site_filter;
        const std::string& doc_domain = doc.domain;
        
        if (doc_domain != filter_domain) {
            // Check if doc_domain ends with .filter_domain
            if (doc_domain.length() <= filter_domain.length()) {
                return false;
            }
            std::string suffix = "." + filter_domain;
            if (doc_domain.substr(doc_domain.length() - suffix.length()) != suffix) {
                return false;
            }
        }
    }
    
    return true;
}

bool Ranker::has_excluded_terms(
    const std::vector<std::string>& excluded,
    const std::unordered_map<std::string, Posting>& term_postings
) const {
    for (const auto& term : excluded) {
        if (term_postings.find(term) != term_postings.end()) {
            return true;
        }
    }
    return false;
}

//=============================================================================
// Main Ranking Method - Returns SearchResponse
//=============================================================================

SearchResponse Ranker::rank(const ParsedQuery& query, size_t max_results) {
    SearchResponse response;
    response.query = query;  // Include the query in the response
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    if (!index_ || query.empty()) {
        return response;
    }
    
    response.docs_searched = index_->get_total_docs();
    
    // Collect all terms to search (including phrase terms)
    std::vector<std::string> all_terms = query.terms;
    
    // Add terms from phrases (for candidate collection)
    for (const auto& phrase : query.phrases) {
        std::string current;
        for (char c : phrase) {
            if (c == ' ') {
                if (!current.empty()) {
                    all_terms.push_back(current);
                    current.clear();
                }
            } else {
                current += c;
            }
        }
        if (!current.empty()) {
            all_terms.push_back(current);
        }
    }
    
    // Also collect postings for excluded terms (to filter them out)
    std::vector<std::string> search_terms = all_terms;
    for (const auto& excluded : query.excluded_terms) {
        search_terms.push_back(excluded);
    }
    
    // Collect candidate documents
    auto candidates = collect_candidates(search_terms);
    
    // Score and filter each candidate
    std::vector<SearchResult> results;
    uint64_t total_matches = 0;
    
    for (auto& [doc_id, term_postings] : candidates) {
        // Get document info
        auto doc_opt = index_->get_document(doc_id);
        if (!doc_opt.has_value()) {
            continue;
        }
        DocumentInfo doc = *doc_opt;
        
        // Check exclusions first (fastest filter)
        if (has_excluded_terms(query.excluded_terms, term_postings)) {
            continue;
        }
        
        // Check filters
        if (!passes_filters(doc, query)) {
            continue;
        }
        
        // Check phrase matches
        if (!query.phrases.empty()) {
            if (!check_phrases(query.phrases, term_postings)) {
                continue;
            }
        }
        
        // This doc matches - count it
        ++total_matches;
        
        // Score the document
        SearchResult result;
        result.doc = doc;
        
        result.relevance_score = score_document(
            doc_id,
            term_postings,
            all_terms,
            doc.doc_length,
            result.term_scores
        );
        
        // Apply AI penalty
        result.ai_penalty = doc.ai_score * ai_penalty_weight_;
        result.score = apply_ai_penalty(result.relevance_score, doc.ai_score);
        
        results.push_back(result);
    }
    
    // Sort by score (descending)
    std::sort(results.begin(), results.end());
    
    // Limit results
    if (results.size() > max_results) {
        results.resize(max_results);
    }
    
    // Calculate search time
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    response.search_time_ms = static_cast<float>(duration.count()) / 1000.0f;
    
    // Populate response
    response.results = std::move(results);
    response.total_matches = total_matches;
    
    return response;
}

} // namespace search