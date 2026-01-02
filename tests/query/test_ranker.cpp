#include "query/ranker.h"
#include "query/query_parser.h"
#include <iostream>
#include <cmath>

using namespace search;

//=============================================================================
// MockIndexReader - Test implementation only (not in production source)
//=============================================================================
class MockIndexReader : public IndexReader {
public:
    std::vector<Posting> get_postings(const std::string& term) const override {
        auto it = postings_.find(term);
        if (it != postings_.end()) {
            return it->second;
        }
        return {};
    }
    
    std::optional<DocumentInfo> get_document(uint64_t doc_id) const override {
        auto it = documents_.find(doc_id);
        if (it != documents_.end()) {
            return it->second;
        }
        return std::nullopt;
    }
    
    uint32_t get_doc_frequency(const std::string& term) const override {
        auto it = postings_.find(term);
        if (it != postings_.end()) {
            return static_cast<uint32_t>(it->second.size());
        }
        return 0;
    }
    
    uint64_t get_total_docs() const override {
        return total_docs_;
    }
    
    float get_avg_doc_length() const override {
        return avg_doc_length_;
    }
    
    // Test data population
    void add_posting(const std::string& term, const Posting& posting) {
        postings_[term].push_back(posting);
    }
    
    void add_document(const DocumentInfo& doc) {
        documents_[doc.doc_id] = doc;
        if (doc.doc_id >= total_docs_) {
            total_docs_ = doc.doc_id + 1;
        }
    }
    
    void set_total_docs(uint64_t count) { total_docs_ = count; }
    void set_avg_doc_length(float len) { avg_doc_length_ = len; }

private:
    std::unordered_map<std::string, std::vector<Posting>> postings_;
    std::unordered_map<uint64_t, DocumentInfo> documents_;
    uint64_t total_docs_ = 0;
    float avg_doc_length_ = 100.0f;
};

//=============================================================================
// Test utilities
//=============================================================================
struct TestResults {
    int passed = 0;
    int failed = 0;
};

void print_test(bool passed, const std::string& name, TestResults& results) {
    if (passed) {
        std::cout << "  [PASS] " << name << "\n";
        results.passed++;
    } else {
        std::cout << "  [FAIL] " << name << "\n";
        results.failed++;
    }
}

bool approx_equal(float a, float b, float epsilon = 0.001f) {
    return std::fabs(a - b) < epsilon;
}

//=============================================================================
// Helper: Create a test index
//=============================================================================
std::shared_ptr<MockIndexReader> create_test_index() {
    auto index = std::make_shared<MockIndexReader>();
    
    // Document 1: "bitcoin price prediction" - Pre-AI, low AI score
    index->add_document(DocumentInfo{
        .doc_id = 1,
        .url = "https://example.com/bitcoin-history",
        .title = "Bitcoin Price History",
        .snippet = "Historical analysis of bitcoin price trends...",
        .doc_length = 150,
        .ai_score = 0.1f,
        .era = Era::PRE_AI,
        .domain = "example.com"
    });
    
    // Document 2: "bitcoin trading" - AI era, high AI score
    index->add_document(DocumentInfo{
        .doc_id = 2,
        .url = "https://aisite.com/bitcoin-trading",
        .title = "Bitcoin Trading Guide",
        .snippet = "A comprehensive guide to trading bitcoin...",
        .doc_length = 200,
        .ai_score = 0.85f,
        .era = Era::AI_ERA,
        .domain = "aisite.com"
    });
    
    // Document 3: "ethereum price" - Transition, medium AI score
    index->add_document(DocumentInfo{
        .doc_id = 3,
        .url = "https://crypto.example.com/ethereum",
        .title = "Ethereum Price Analysis",
        .snippet = "Ethereum price movements and predictions...",
        .doc_length = 180,
        .ai_score = 0.4f,
        .era = Era::TRANSITION,
        .domain = "crypto.example.com"
    });
    
    // Document 4: "bitcoin scam warning" - Pre-AI (for exclusion test)
    index->add_document(DocumentInfo{
        .doc_id = 4,
        .url = "https://security.com/scam-alert",
        .title = "Bitcoin Scam Warning",
        .snippet = "Warning about bitcoin scams...",
        .doc_length = 100,
        .ai_score = 0.05f,
        .era = Era::PRE_AI,
        .domain = "security.com"
    });
    
    // Add postings
    // Doc 1: "bitcoin price prediction"
    index->add_posting("bitcoin", Posting{1, Field::BODY,  3, {0, 15, 45}});
    index->add_posting("price", Posting{1, Field::BODY,  2, {1, 46}});
    index->add_posting("predict", Posting{1, Field::BODY,  1, {2}});  // stemmed
    index->add_posting("histori", Posting{1, Field::BODY,  1, {5}});  // stemmed
    
    // Doc 2: "bitcoin trading"
    index->add_posting("bitcoin", Posting{2, Field::BODY, 5, {0, 10, 20, 30, 40}});
    index->add_posting("trade", Posting{2, Field::BODY, 3, {1, 11, 21}});  // stemmed
    index->add_posting("guid", Posting{2, Field::BODY, 1, {3}});  // stemmed
    
    // Doc 3: "ethereum price"
    index->add_posting("ethereum", Posting{3, Field::BODY, 4, {0, 12, 24, 36}});
    index->add_posting("price", Posting{3, Field::BODY, 3, {1, 13, 25}});
    index->add_posting("analysi", Posting{3, Field::BODY, 2, {2, 26}});  // stemmed
    
    // Doc 4: "bitcoin scam"
    index->add_posting("bitcoin", Posting{4, Field::BODY, 2, {0, 8}});
    index->add_posting("scam", Posting{4, Field::BODY, 3, {1, 5, 9}});
    index->add_posting("warn", Posting{4, Field::BODY, 1, {2}});  // stemmed
    
    index->set_total_docs(4);
    index->set_avg_doc_length(157.5f);
    
    return index;
}

//=============================================================================
// Test 1: BM25 Scoring Basics
//=============================================================================
void test_bm25_scoring(TestResults& results) {
    std::cout << "\n--- Test: BM25 Scoring ---\n";
    
    Ranker ranker;
    
    // IDF calculation
    float idf_val = ranker.idf(2, 100);
    print_test(idf_val > 0, "IDF is positive for rare term", results);
    
    float idf_common = ranker.idf(50, 100);
    print_test(idf_val > idf_common, "Rare term has higher IDF", results);
    
    // BM25 term score
    float score = ranker.bm25_term_score(3, 150, 10, 1000, 200.0f);
    print_test(score > 0, "BM25 score is positive", results);
    
    // Higher TF should give higher score
    float score_high_tf = ranker.bm25_term_score(10, 150, 10, 1000, 200.0f);
    print_test(score_high_tf > score, "Higher TF gives higher score", results);
    
    // Longer doc should have slightly lower score
    float score_long_doc = ranker.bm25_term_score(3, 400, 10, 1000, 200.0f);
    print_test(score > score_long_doc, "Length normalization penalizes long docs", results);
}

//=============================================================================
// Test 2: AI Penalty
//=============================================================================
void test_ai_penalty(TestResults& results) {
    std::cout << "\n--- Test: AI Penalty ---\n";
    
    Ranker ranker;
    ranker.set_ai_penalty_weight(0.2f);
    
    float base_score = 10.0f;
    
    // No penalty for human content
    float human_score = ranker.apply_ai_penalty(base_score, 0.0f);
    print_test(approx_equal(human_score, base_score), "No penalty for ai_score=0", results);
    
    // Full penalty for AI content
    float ai_score = ranker.apply_ai_penalty(base_score, 1.0f);
    float expected = base_score * (1.0f - 0.2f);
    print_test(approx_equal(ai_score, expected), "20% penalty for ai_score=1.0", results);
    
    // Partial penalty
    float partial = ranker.apply_ai_penalty(base_score, 0.5f);
    float expected_partial = base_score * (1.0f - 0.1f);
    print_test(approx_equal(partial, expected_partial), "10% penalty for ai_score=0.5", results);
}

//=============================================================================
// Test 3: SearchResponse Structure
//=============================================================================
void test_search_response(TestResults& results) {
    std::cout << "\n--- Test: SearchResponse Structure ---\n";
    
    auto index = create_test_index();
    Ranker ranker(index);
    
    QueryParser parser;
    auto query = parser.parse("bitcoin price");
    
    auto response = ranker.rank(query, 10);
    
    // Verify query is included in response
    print_test(response.query.original_query == "bitcoin price", 
               "Response contains original query", results);
    print_test(response.query.terms.size() == 2, 
               "Response query has parsed terms", results);
    
    // Verify statistics
    print_test(response.docs_searched == 4, "Docs searched count correct", results);
    print_test(response.total_matches > 0, "Total matches recorded", results);
    print_test(response.search_time_ms >= 0, "Search time recorded", results);
    
    // Verify results
    print_test(!response.empty(), "Response has results", results);
    print_test(response.size() > 0, "Response size() works", results);
    
    std::cout << "  Search stats:\n";
    std::cout << "    Query: " << response.query.original_query << "\n";
    std::cout << "    Docs searched: " << response.docs_searched << "\n";
    std::cout << "    Matches: " << response.total_matches << "\n";
    std::cout << "    Results: " << response.size() << "\n";
    std::cout << "    Time: " << response.search_time_ms << " ms\n";
}

//=============================================================================
// Test 4: Era Filter
//=============================================================================
void test_era_filter(TestResults& results) {
    std::cout << "\n--- Test: Era Filter ---\n";
    
    auto index = create_test_index();
    Ranker ranker(index);
    
    QueryParser parser;
    auto query = parser.parse("bitcoin era:pre-ai");
    
    auto response = ranker.rank(query, 10);
    
    print_test(!response.empty(), "Results returned with era filter", results);
    print_test(response.query.era_filter.has_value(), "Era filter in response query", results);
    
    // All results should be PRE_AI
    bool all_pre_ai = true;
    for (const auto& r : response.results) {
        if (r.doc.era != Era::PRE_AI) {
            all_pre_ai = false;
            break;
        }
    }
    print_test(all_pre_ai, "All results are Pre-AI era", results);
}

//=============================================================================
// Test 5: AI Score Filter
//=============================================================================
void test_ai_score_filter(TestResults& results) {
    std::cout << "\n--- Test: AI Score Filter ---\n";
    
    auto index = create_test_index();
    Ranker ranker(index);
    
    QueryParser parser;
    
    // Max AI score filter
    auto query = parser.parse("bitcoin ai:<0.3");
    auto response = ranker.rank(query, 10);
    
    print_test(response.query.max_ai_score.has_value(), "Max AI filter in response", results);
    
    bool all_low_ai = true;
    for (const auto& r : response.results) {
        if (r.doc.ai_score > 0.3f) {
            all_low_ai = false;
            break;
        }
    }
    print_test(all_low_ai, "All results have ai_score < 0.3", results);
}

//=============================================================================
// Test 6: Site Filter
//=============================================================================
void test_site_filter(TestResults& results) {
    std::cout << "\n--- Test: Site Filter ---\n";
    
    auto index = create_test_index();
    Ranker ranker(index);
    
    QueryParser parser;
    auto query = parser.parse("price site:example.com");
    
    auto response = ranker.rank(query, 10);
    
    print_test(response.query.site_filter.has_value(), "Site filter in response", results);
    
    bool all_example = true;
    for (const auto& r : response.results) {
        const std::string& domain = r.doc.domain;
        if (domain != "example.com" && 
            domain.find(".example.com") == std::string::npos) {
            all_example = false;
            break;
        }
    }
    print_test(all_example, "Site filter matches domain and subdomains", results);
}

//=============================================================================
// Test 7: Excluded Terms
//=============================================================================
void test_excluded_terms(TestResults& results) {
    std::cout << "\n--- Test: Excluded Terms ---\n";
    
    auto index = create_test_index();
    Ranker ranker(index);
    
    QueryParser parser;
    
    // Without exclusion
    auto query1 = parser.parse("bitcoin");
    auto response1 = ranker.rank(query1, 10);
    
    bool has_scam_doc = false;
    for (const auto& r : response1.results) {
        if (r.doc.doc_id == 4) {
            has_scam_doc = true;
            break;
        }
    }
    print_test(has_scam_doc, "Scam doc included without exclusion", results);
    
    // With exclusion
    auto query2 = parser.parse("bitcoin -scam");
    auto response2 = ranker.rank(query2, 10);
    
    print_test(!response2.query.excluded_terms.empty(), "Excluded terms in response", results);
    
    bool scam_excluded = true;
    for (const auto& r : response2.results) {
        if (r.doc.doc_id == 4) {
            scam_excluded = false;
            break;
        }
    }
    print_test(scam_excluded, "Scam doc excluded with -scam", results);
}

//=============================================================================
// Test 8: Empty Query
//=============================================================================
void test_empty_query(TestResults& results) {
    std::cout << "\n--- Test: Empty Query ---\n";
    
    auto index = create_test_index();
    Ranker ranker(index);
    
    QueryParser parser;
    auto query = parser.parse("");
    
    auto response = ranker.rank(query, 10);
    print_test(response.empty(), "Empty query returns no results", results);
    print_test(response.query.empty(), "Response query is empty", results);
}

//=============================================================================
// Test 9: Result Limit
//=============================================================================
void test_result_limit(TestResults& results) {
    std::cout << "\n--- Test: Result Limit ---\n";
    
    auto index = create_test_index();
    Ranker ranker(index);
    
    QueryParser parser;
    auto query = parser.parse("bitcoin");
    
    auto response1 = ranker.rank(query, 1);
    print_test(response1.size() <= 1, "Limit of 1 respected", results);
    print_test(response1.total_matches >= response1.size(), 
               "total_matches >= results.size()", results);
    
    auto response2 = ranker.rank(query, 100);
    print_test(response2.size() >= response1.size(), "Higher limit returns more/equal", results);
}

//=============================================================================
// Test 10: Score Breakdown
//=============================================================================
void test_score_breakdown(TestResults& results) {
    std::cout << "\n--- Test: Score Breakdown ---\n";
    
    auto index = create_test_index();
    Ranker ranker(index);
    
    QueryParser parser;
    auto query = parser.parse("bitcoin price");
    
    auto response = ranker.rank(query, 10);
    
    if (!response.empty()) {
        const auto& r = response.results[0];
        
        print_test(r.relevance_score > 0, "Relevance score is positive", results);
        print_test(r.score <= r.relevance_score, "Final score <= relevance (AI penalty)", results);
        print_test(!r.term_scores.empty(), "Per-term scores available", results);
        
        std::cout << "  Score breakdown for top result:\n";
        std::cout << "    Title: " << r.doc.title << "\n";
        std::cout << "    Relevance: " << r.relevance_score << "\n";
        std::cout << "    AI Penalty: " << r.ai_penalty << "\n";
        std::cout << "    Final: " << r.score << "\n";
    } else {
        print_test(false, "Need results for breakdown test", results);
    }
}

//=============================================================================
// Test 11: Complex Query with All Filters
//=============================================================================
void test_complex_query(TestResults& results) {
    std::cout << "\n--- Test: Complex Query ---\n";
    
    auto index = create_test_index();
    Ranker ranker(index);
    
    QueryParser parser;
    auto query = parser.parse("bitcoin era:pre-ai ai:<0.5 -scam");
    
    auto response = ranker.rank(query, 10);
    
    // Verify all filters are preserved in response
    print_test(response.query.era_filter.has_value(), "Era filter preserved", results);
    print_test(response.query.max_ai_score.has_value(), "AI filter preserved", results);
    print_test(!response.query.excluded_terms.empty(), "Exclusions preserved", results);
    
    // Verify results match all filters
    bool all_pass = true;
    for (const auto& r : response.results) {
        if (r.doc.era != Era::PRE_AI) all_pass = false;
        if (r.doc.ai_score > 0.5f) all_pass = false;
    }
    print_test(all_pass, "All results pass all filters", results);
    
    std::cout << "\n  Complex query response:\n";
    std::cout << response.query.to_string() << "\n";
}

//=============================================================================
// Main
//=============================================================================
int main() {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "    Ranker Test Suite - Task 3.5                            \n";
    std::cout << "============================================================\n";
    
    TestResults results;
    
    test_bm25_scoring(results);
    test_ai_penalty(results);
    test_search_response(results);
    test_era_filter(results);
    test_ai_score_filter(results);
    test_site_filter(results);
    test_excluded_terms(results);
    test_empty_query(results);
    test_result_limit(results);
    test_score_breakdown(results);
    test_complex_query(results);
    
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "                    TEST SUMMARY                            \n";
    std::cout << "============================================================\n";
    std::cout << "Passed: " << results.passed << "\n";
    std::cout << "Failed: " << results.failed << "\n";
    std::cout << "\n";
    
    if (results.failed == 0) {
        std::cout << "[SUCCESS] All Ranker tests passed!\n\n";
        return 0;
    } else {
        std::cout << "[FAILED] Some tests failed\n\n";
        return 1;
    }
}