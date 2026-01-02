#include "common/text_processor.h"
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
// Test 1: Normalization
//=============================================================================
void test_normalization(TestResults& results) {
    std::cout << "\n--- Test: Normalization ---\n";
    
    TextProcessor tp;
    
    print_test(tp.normalize("Hello") == "hello", "Lowercase", results);
    print_test(tp.normalize("WORLD") == "world", "All caps", results);
    print_test(tp.normalize("Hello, World!") == "helloworld", "Strip punctuation", results);
    print_test(tp.normalize("test123") == "test123", "Preserve numbers", results);
    print_test(tp.normalize("   spaces   ") == "spaces", "Strip whitespace", results);
    print_test(tp.normalize("") == "", "Empty string", results);
    print_test(tp.normalize("...") == "", "Only punctuation", results);
}

//=============================================================================
// Test 2: to_lower (preserves structure)
//=============================================================================
void test_to_lower(TestResults& results) {
    std::cout << "\n--- Test: to_lower ---\n";
    
    TextProcessor tp;
    
    print_test(tp.to_lower("Hello World") == "hello world", "Basic lowercase", results);
    print_test(tp.to_lower("BITCOIN PRICE!") == "bitcoin price!", "Preserves punctuation", results);
    print_test(tp.to_lower("MiXeD CaSe") == "mixed case", "Mixed case", results);
}

//=============================================================================
// Test 3: Basic Tokenization
//=============================================================================
void test_tokenization(TestResults& results) {
    std::cout << "\n--- Test: Basic Tokenization ---\n";
    
    TextProcessor tp;
    
    auto tokens1 = tp.tokenize("Hello World");
    print_test(tokens1.size() == 2, "Two tokens from 'Hello World'", results);
    print_test(tokens1[0] == "hello", "First token lowercase", results);
    print_test(tokens1[1] == "world", "Second token lowercase", results);
    
    auto tokens2 = tp.tokenize("one,two;three:four");
    print_test(tokens2.size() == 4, "Splits on punctuation", results);
    
    auto tokens3 = tp.tokenize("   multiple   spaces   ");
    print_test(tokens3.size() == 2, "Handles multiple spaces", results);
    
    auto tokens4 = tp.tokenize("");
    print_test(tokens4.empty(), "Empty string returns empty", results);
    
    auto tokens5 = tp.tokenize("test123number");
    print_test(tokens5.size() == 1 && tokens5[0] == "test123number", 
               "Keeps alphanumeric together", results);
}

//=============================================================================
// Test 4: Stop Words
//=============================================================================
void test_stop_words(TestResults& results) {
    std::cout << "\n--- Test: Stop Words ---\n";
    
    TextProcessor tp;
    
    // Default stop words
    print_test(tp.is_stop_word("the"), "'the' is stop word", results);
    print_test(tp.is_stop_word("a"), "'a' is stop word", results);
    print_test(tp.is_stop_word("and"), "'and' is stop word", results);
    print_test(tp.is_stop_word("is"), "'is' is stop word", results);
    print_test(!tp.is_stop_word("bitcoin"), "'bitcoin' is NOT stop word", results);
    print_test(!tp.is_stop_word("price"), "'price' is NOT stop word", results);
    
    // Add custom stop word
    tp.add_stop_word("bitcoin");
    print_test(tp.is_stop_word("bitcoin"), "Added 'bitcoin' as stop word", results);
    
    // Remove stop word
    tp.remove_stop_word("the");
    print_test(!tp.is_stop_word("the"), "Removed 'the' from stop words", results);
    
    // Clear all
    tp.clear_stop_words();
    print_test(!tp.is_stop_word("and"), "Cleared all stop words", results);
}

//=============================================================================
// Test 5: Porter Stemmer - Basic Cases
//=============================================================================
void test_stemmer_basic(TestResults& results) {
    std::cout << "\n--- Test: Porter Stemmer - Basic ---\n";
    
    TextProcessor tp;
    
    // Plurals (Step 1a)
    print_test(tp.stem("cats") == "cat", "cats -> cat", results);
    print_test(tp.stem("caresses") == "caress", "caresses -> caress", results);
    print_test(tp.stem("ponies") == "poni", "ponies -> poni", results);
    
    // -ed, -ing (Step 1b)
    print_test(tp.stem("agreed") == "agre", "agreed -> agre", results);
    print_test(tp.stem("walking") == "walk", "walking -> walk", results);
    print_test(tp.stem("playing") == "plai", "playing -> plai", results);
    
    // -y to -i (Step 1c)
    print_test(tp.stem("happy") == "happi", "happy -> happi", results);
    
    // Short words unchanged
    print_test(tp.stem("is") == "is", "is unchanged (too short)", results);
    print_test(tp.stem("a") == "a", "a unchanged (too short)", results);
}

//=============================================================================
// Test 6: Porter Stemmer - Complex Cases
//=============================================================================
void test_stemmer_complex(TestResults& results) {
    std::cout << "\n--- Test: Porter Stemmer - Complex ---\n";
    
    TextProcessor tp;
    
    // Step 2 suffixes
    print_test(tp.stem("relational") == "relat", "relational -> relat", results);
    print_test(tp.stem("conditional") == "condit", "conditional -> condit", results);
    print_test(tp.stem("organization") == "organ", "organization -> organ", results);
    
    // Step 3 suffixes
    print_test(tp.stem("triplicate") == "triplic", "triplicate -> triplic", results);
    print_test(tp.stem("formalize") == "formal", "formalize -> formal", results);
    
    // Step 4 suffixes
    print_test(tp.stem("revival") == "reviv", "revival -> reviv", results);
    
    // Common words
    print_test(tp.stem("running") == "run", "running -> run", results);
    print_test(tp.stem("easily") == "easili", "easily -> easili", results);
}

//=============================================================================
// Test 7: Process (full pipeline)
//=============================================================================
void test_process(TestResults& results) {
    std::cout << "\n--- Test: Full Processing Pipeline ---\n";
    
    TextProcessor tp;
    
    // Full processing: tokenize + stop words + stem
    auto tokens1 = tp.process("The quick brown foxes are running");
    
    // "The" and "are" should be removed (stop words)
    // "foxes" -> "fox", "running" -> "run"
    bool has_the = false;
    bool has_are = false;
    bool has_fox = false;
    bool has_run = false;
    
    for (const auto& t : tokens1) {
        if (t == "the") has_the = true;
        if (t == "are") has_are = true;
        if (t == "fox") has_fox = true;
        if (t == "run") has_run = true;
    }
    
    print_test(!has_the, "'the' removed", results);
    print_test(!has_are, "'are' removed", results);
    print_test(has_fox, "'foxes' -> 'fox'", results);
    print_test(has_run, "'running' -> 'run'", results);
    
    // Process single word
    print_test(tp.process_word("Running") == "run", "process_word normalizes + stems", results);
    print_test(tp.process_word("THE") == "the", "process_word normalizes (no stop removal)", results);
}

//=============================================================================
// Test 8: Configuration Options
//=============================================================================
void test_configuration(TestResults& results) {
    std::cout << "\n--- Test: Configuration ---\n";
    
    TextProcessor tp;
    
    // Disable stemming
    tp.set_stemming_enabled(false);
    print_test(tp.process_word("running") == "running", "Stemming disabled", results);
    tp.set_stemming_enabled(true);
    print_test(tp.process_word("running") == "run", "Stemming re-enabled", results);
    
    // Disable stop word removal
    tp.set_stop_word_removal_enabled(false);
    auto tokens = tp.process("the cat");
    bool has_the = false;
    for (const auto& t : tokens) {
        if (t == "the") has_the = true;
    }
    print_test(has_the, "Stop word removal disabled", results);
    
    // Min/max token length
    tp.set_stop_word_removal_enabled(true);
    tp.set_min_token_length(4);
    auto tokens2 = tp.process("a big cat runs");
    // "a" and "cat" filtered (< 4 chars after processing)
    print_test(tokens2.size() == 1, "Min length filter works", results);
}

//=============================================================================
// Test 9: Stemmer Consistency (important for search!)
//=============================================================================
void test_stemmer_consistency(TestResults& results) {
    std::cout << "\n--- Test: Stemmer Consistency ---\n";
    
    TextProcessor tp;
    
    // These should all stem to the same root
    std::string connect1 = tp.stem("connect");
    std::string connect2 = tp.stem("connected");
    std::string connect3 = tp.stem("connecting");
    std::string connect4 = tp.stem("connection");
    std::string connect5 = tp.stem("connections");
    
    std::cout << "  connect: " << connect1 << "\n";
    std::cout << "  connected: " << connect2 << "\n";
    std::cout << "  connecting: " << connect3 << "\n";
    std::cout << "  connection: " << connect4 << "\n";
    std::cout << "  connections: " << connect5 << "\n";
    
    // At minimum, these pairs should match
    print_test(connect2 == connect3, "connected == connecting", results);
    print_test(connect4 == connect5, "connection == connections", results);
    
    // Search-critical: query and document should match
    std::string query_term = tp.process_word("running");
    std::string doc_term = tp.process_word("runs");
    std::cout << "  Query 'running' -> " << query_term << "\n";
    std::cout << "  Doc 'runs' -> " << doc_term << "\n";
    print_test(query_term == doc_term, "Query 'running' matches doc 'runs'", results);
}

//=============================================================================
// Test 10: Edge Cases
//=============================================================================
void test_edge_cases(TestResults& results) {
    std::cout << "\n--- Test: Edge Cases ---\n";
    
    TextProcessor tp;
    
    // Unicode/special characters (should be stripped)
    print_test(tp.normalize("café") == "caf", "Strips non-ASCII", results);
    
    // Numbers only
    print_test(tp.normalize("12345") == "12345", "Numbers preserved", results);
    
    // Mixed
    print_test(tp.normalize("test123test") == "test123test", "Alphanumeric preserved", results);
    
    // Very long word
    std::string long_word(200, 'a');
    tp.set_max_token_length(100);
    auto tokens = tp.process(long_word);
    print_test(tokens.empty(), "Filters words exceeding max length", results);
    
    // Empty after normalization
    print_test(tp.normalize("!!!") == "", "Punctuation-only normalizes to empty", results);
}

//=============================================================================
// Main
//=============================================================================
int main() {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "    TextProcessor Test Suite                                \n";
    std::cout << "============================================================\n";
    
    TestResults results;
    
    test_normalization(results);
    test_to_lower(results);
    test_tokenization(results);
    test_stop_words(results);
    test_stemmer_basic(results);
    test_stemmer_complex(results);
    test_process(results);
    test_configuration(results);
    test_stemmer_consistency(results);
    test_edge_cases(results);
    
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "                    TEST SUMMARY                            \n";
    std::cout << "============================================================\n";
    std::cout << "Passed: " << results.passed << "\n";
    std::cout << "Failed: " << results.failed << "\n";
    std::cout << "\n";
    
    if (results.failed == 0) {
        std::cout << "[SUCCESS] All TextProcessor tests passed!\n\n";
        return 0;
    } else {
        std::cout << "[FAILED] Some tests failed\n\n";
        return 1;
    }
}