#include "query/query_parser.h"
#include <iostream>
#include <cassert>

using namespace search;

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

//=============================================================================
// Test 1: Basic term parsing
//=============================================================================
void test_basic_terms(TestResults& results) {
    std::cout << "\n--- Test: Basic Term Parsing ---\n";
    
    QueryParser parser;
    
    auto q1 = parser.parse("bitcoin price");
    print_test(q1.terms.size() == 2, "Two terms parsed", results);
    print_test(q1.terms[0] == "bitcoin", "First term is 'bitcoin'", results);
    print_test(q1.terms[1] == "price", "Second term is 'price'", results);
    print_test(q1.phrases.empty(), "No phrases", results);
    
    auto q2 = parser.parse("Bitcoin PRICE Prediction");
    print_test(q2.terms[0] == "bitcoin", "Lowercase: 'bitcoin'", results);
    print_test(q2.terms[1] == "price", "Lowercase: 'price'", results);
}

//=============================================================================
// Test 2: Stop word removal
//=============================================================================
void test_stop_words(TestResults& results) {
    std::cout << "\n--- Test: Stop Word Removal ---\n";
    
    QueryParser parser;
    
    auto q1 = parser.parse("the price of bitcoin");
    
    bool has_the = false, has_of = false, has_bitcoin = false, has_price = false;
    for (const auto& term : q1.terms) {
        if (term == "the") has_the = true;
        if (term == "of") has_of = true;
        if (term == "bitcoin") has_bitcoin = true;
        if (term == "price") has_price = true;
    }
    
    print_test(!has_the, "'the' removed", results);
    print_test(!has_of, "'of' removed", results);
    print_test(has_bitcoin, "'bitcoin' kept", results);
    print_test(has_price, "'price' kept", results);
}

//=============================================================================
// Test 3: Phrase queries
//=============================================================================
void test_phrase_queries(TestResults& results) {
    std::cout << "\n--- Test: Phrase Queries ---\n";
    
    QueryParser parser;
    
    auto q1 = parser.parse("\"bitcoin price prediction\"");
    print_test(q1.phrases.size() == 1, "One phrase extracted", results);
    print_test(q1.phrases[0] == "bitcoin price prediction", "Phrase content correct", results);
    print_test(q1.terms.empty(), "No loose terms", results);
    
    auto q2 = parser.parse("cryptocurrency \"bitcoin price\" analysis");
    print_test(q2.phrases.size() == 1, "One phrase in mixed query", results);
    print_test(q2.phrases[0] == "bitcoin price", "Phrase extracted correctly", results);
    print_test(q2.terms.size() == 2, "Two loose terms", results);
}

//=============================================================================
// Test 4: Era filter (using shared Era enum)
//=============================================================================
void test_era_filter(TestResults& results) {
    std::cout << "\n--- Test: Era Filter ---\n";
    
    QueryParser parser;
    
    auto q1 = parser.parse("bitcoin era:pre-ai");
    print_test(q1.era_filter.has_value(), "Era filter present", results);
    print_test(*q1.era_filter == Era::PRE_AI, "Era is PRE_AI", results);
    
    auto q2 = parser.parse("era:transition news");
    print_test(q2.era_filter.has_value(), "Transition era parsed", results);
    print_test(*q2.era_filter == Era::TRANSITION, "Era is TRANSITION", results);
    
    auto q3 = parser.parse("content era:ai-era");
    print_test(q3.era_filter.has_value(), "AI era parsed", results);
    print_test(*q3.era_filter == Era::AI_ERA, "Era is AI_ERA", results);
    
    // Test shared era_to_string
    print_test(era_to_string(Era::PRE_AI) == "Pre-AI", "era_to_string works", results);
}

//=============================================================================
// Test 5: AI score filter
//=============================================================================
void test_ai_filter(TestResults& results) {
    std::cout << "\n--- Test: AI Score Filter ---\n";
    
    QueryParser parser;
    
    auto q1 = parser.parse("bitcoin ai:<0.3");
    print_test(q1.max_ai_score.has_value(), "Max AI score present", results);
    print_test(*q1.max_ai_score == 0.3f, "Max AI score is 0.3", results);
    
    auto q2 = parser.parse("ai:>0.7 content");
    print_test(q2.min_ai_score.has_value(), "Min AI score present", results);
    print_test(*q2.min_ai_score == 0.7f, "Min AI score is 0.7", results);
    
    auto q3 = parser.parse("bitcoin ai:0.5");
    print_test(q3.max_ai_score.has_value(), "Plain number parsed as max", results);
    print_test(*q3.max_ai_score == 0.5f, "Max is 0.5", results);
}

//=============================================================================
// Test 6: Site filter
//=============================================================================
void test_site_filter(TestResults& results) {
    std::cout << "\n--- Test: Site Filter ---\n";
    
    QueryParser parser;
    
    auto q1 = parser.parse("bitcoin site:coindesk.com");
    print_test(q1.site_filter.has_value(), "Site filter present", results);
    print_test(*q1.site_filter == "coindesk.com", "Site is coindesk.com", results);
    
    auto q2 = parser.parse("site:Wikipedia.ORG history");
    print_test(q2.site_filter.has_value(), "Site filter parsed", results);
    print_test(*q2.site_filter == "wikipedia.org", "Site lowercased", results);
}

//=============================================================================
// Test 7: Excluded terms
//=============================================================================
void test_excluded_terms(TestResults& results) {
    std::cout << "\n--- Test: Excluded Terms ---\n";
    
    QueryParser parser;
    
    auto q1 = parser.parse("bitcoin -scam");
    print_test(q1.terms.size() == 1, "One search term", results);
    print_test(q1.excluded_terms.size() == 1, "One excluded term", results);
    print_test(q1.excluded_terms[0] == "scam", "Excluded term is 'scam'", results);
    
    auto q2 = parser.parse("crypto -spam -ads -clickbait");
    print_test(q2.excluded_terms.size() == 3, "Three excluded terms", results);
}

//=============================================================================
// Test 8: Complex queries
//=============================================================================
void test_complex_queries(TestResults& results) {
    std::cout << "\n--- Test: Complex Queries ---\n";
    
    QueryParser parser;
    
    auto q1 = parser.parse("\"bitcoin price\" prediction era:pre-ai ai:<0.3 site:archive.org -spam");
    
    print_test(q1.phrases.size() == 1, "One phrase", results);
    print_test(q1.phrases[0] == "bitcoin price", "Phrase is 'bitcoin price'", results);
    print_test(q1.era_filter.has_value(), "Era filter present", results);
    print_test(*q1.era_filter == Era::PRE_AI, "Era is PRE_AI", results);
    print_test(q1.max_ai_score.has_value(), "Max AI score present", results);
    print_test(*q1.max_ai_score == 0.3f, "Max AI is 0.3", results);
    print_test(q1.site_filter.has_value(), "Site filter present", results);
    print_test(*q1.site_filter == "archive.org", "Site is archive.org", results);
    print_test(q1.excluded_terms.size() == 1, "One exclusion", results);
    
    std::cout << "\n  Parsed query:\n" << q1.to_string() << "\n";
}

//=============================================================================
// Test 9: Edge cases
//=============================================================================
void test_edge_cases(TestResults& results) {
    std::cout << "\n--- Test: Edge Cases ---\n";
    
    QueryParser parser;
    
    auto q1 = parser.parse("");
    print_test(q1.empty(), "Empty query is empty", results);
    
    auto q2 = parser.parse("   \t\n  ");
    print_test(q2.empty(), "Whitespace-only query is empty", results);
    
    auto q3 = parser.parse("the a an");
    print_test(q3.empty(), "Stop-words-only query is empty", results);
    
    auto q4 = parser.parse("\"unclosed phrase");
    print_test(q4.phrases.empty(), "Unclosed quote ignored", results);
    
    auto q5 = parser.parse("bitcoin site:");
    print_test(!q5.site_filter.has_value(), "Empty filter value rejected", results);
}

//=============================================================================
// Test 10: Shared TextProcessor
//=============================================================================
void test_shared_processor(TestResults& results) {
    std::cout << "\n--- Test: Shared TextProcessor ---\n";
    
    auto processor = std::make_shared<TextProcessor>();
    QueryParser parser(processor);
    
    processor->add_stop_word("customword");
    
    auto q1 = parser.parse("test customword query");
    bool has_custom = false;
    for (const auto& t : q1.terms) {
        if (t == "customword") has_custom = true;
    }
    print_test(!has_custom, "Shared processor stop word respected", results);
    
    parser.processor().add_stop_word("anotherword");
    print_test(processor->is_stop_word("anotherword"), 
               "Changes through parser visible in shared processor", results);
}

//=============================================================================
// Test 11: ParsedQuery to_string (shared type)
//=============================================================================
void test_parsed_query_to_string(TestResults& results) {
    std::cout << "\n--- Test: ParsedQuery::to_string ---\n";
    
    QueryParser parser;
    auto q = parser.parse("bitcoin era:pre-ai ai:<0.3");
    
    std::string str = q.to_string();
    print_test(str.find("bitcoin") != std::string::npos, "to_string contains terms", results);
    print_test(str.find("Pre-AI") != std::string::npos, "to_string contains era", results);
    print_test(str.find("0.3") != std::string::npos, "to_string contains ai score", results);
}

//=============================================================================
// Main
//=============================================================================
int main() {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "    Query Parser Test Suite                                 \n";
    std::cout << "============================================================\n";
    
    TestResults results;
    
    test_basic_terms(results);
    test_stop_words(results);
    test_phrase_queries(results);
    test_era_filter(results);
    test_ai_filter(results);
    test_site_filter(results);
    test_excluded_terms(results);
    test_complex_queries(results);
    test_edge_cases(results);
    test_shared_processor(results);
    test_parsed_query_to_string(results);
    
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "                    TEST SUMMARY                            \n";
    std::cout << "============================================================\n";
    std::cout << "Passed: " << results.passed << "\n";
    std::cout << "Failed: " << results.failed << "\n";
    std::cout << "\n";
    
    if (results.failed == 0) {
        std::cout << "[SUCCESS] All query parser tests passed!\n\n";
        return 0;
    } else {
        std::cout << "[FAILED] Some tests failed\n\n";
        return 1;
    }
}