#include "indexer/tokenizer.h"
#include <iostream>
#include <cassert>

using namespace search;

struct TestResults {
    int passed = 0;
    int failed = 0;
};

void print_test(bool passed, const std::string& name, TestResults& results) {
    if (passed) {
        std::cout << "[PASS] " << name << "\n";
        results.passed++;
    } else {
        std::cout << "[FAIL] " << name << "\n";
        results.failed++;
    }
}

//=============================================================================
// Test 2: Case normalization (2.2.2)
//=============================================================================
void test_case_normalization(TestResults& results) {
    std::cout << "\n--- Test: Case Normalization (2.2.2) ---\n";
    
    Tokenizer tokenizer;
    
    print_test(tokenizer.normalize("HELLO") == "hello", "Uppercase to lowercase", results);
    print_test(tokenizer.normalize("HeLLo") == "hello", "Mixed case", results);
    print_test(tokenizer.normalize("hello") == "hello", "Already lowercase", results);
    print_test(tokenizer.normalize("HELLO123") == "hello123", "With numbers", results);
    print_test(tokenizer.normalize("") == "", "Empty string", results);
}

//=============================================================================
// Test 3: Stop word removal (2.2.3)
//=============================================================================
void test_stop_words(TestResults& results) {
    std::cout << "\n--- Test: Stop Word Removal (2.2.3) ---\n";
    
    Tokenizer tokenizer;
    
    // Check known stop words
    print_test(tokenizer.is_stop_word("the"), "'the' is stop word", results);
    print_test(tokenizer.is_stop_word("and"), "'and' is stop word", results);
    print_test(tokenizer.is_stop_word("is"), "'is' is stop word", results);
    print_test(!tokenizer.is_stop_word("hello"), "'hello' is not stop word", results);
    print_test(!tokenizer.is_stop_word("search"), "'search' is not stop word", results);
    
    // Custom stop word
    tokenizer.add_stop_word("custom");
    print_test(tokenizer.is_stop_word("custom"), "Custom stop word added", results);
    
}

//=============================================================================
// Test 4: Porter Stemming (2.2.4)
//=============================================================================
void test_stemming(TestResults& results) {
    std::cout << "\n--- Test: Porter Stemming (2.2.4) ---\n";
    
    Tokenizer tokenizer;
    
    // Common stemming examples
    print_test(tokenizer.stem("running") == "run", "running -> run", results);
    print_test(tokenizer.stem("runs") == "run", "runs -> run", results);
    print_test(tokenizer.stem("walked") == "walk", "walked -> walk", results);
    print_test(tokenizer.stem("walking") == "walk", "walking -> walk", results);
    
    // Plurals
    print_test(tokenizer.stem("cats") == "cat", "cats -> cat", results);
    print_test(tokenizer.stem("ponies") == "poni", "ponies -> poni", results);
    
    // -ness, -ful, -ment
    print_test(tokenizer.stem("happiness") == "happi", "happiness -> happi", results);
    print_test(tokenizer.stem("hopeful") == "hope", "hopeful -> hope", results);
    
    // Short words unchanged
    print_test(tokenizer.stem("go") == "go", "Short words unchanged", results);
    print_test(tokenizer.stem("a") == "a", "Single letter unchanged", results);
    
    // Same stem for related words
    std::string stem1 = tokenizer.stem("connection");
    std::string stem2 = tokenizer.stem("connected");
    std::string stem3 = tokenizer.stem("connecting");
    std::cout << "[INFO] connection/connected/connecting -> " 
              << stem1 << "/" << stem2 << "/" << stem3 << "\n";
    print_test(stem1 == stem2 && stem2 == stem3, "Related words same stem", results);
}

//=============================================================================
// Test 6: Full pipeline (2.2.6)
//=============================================================================
void test_full_pipeline(TestResults& results) {
    std::cout << "\n--- Test: Full Pipeline (2.2.6) ---\n";
    
    Tokenizer tokenizer;
    
    // Full tokenization
    std::string text = "The quick brown foxes were running and jumping over the lazy dogs.";
    auto tokens = tokenizer.tokenize(text);
    
    std::cout << "[INFO] Input: \"" << text << "\"\n";
    std::cout << "[INFO] Tokens: ";
    for (const auto& t : tokens) {
        std::cout << t.text << " ";
    }
    std::cout << "\n";
    
    print_test(!tokens.empty(), "Produced tokens", results);
    
    // Check stop words removed
    bool has_stop = false;
    for (const auto& t : tokens) {
        if (t.text == "the" || t.text == "and" || t.text == "were" || t.text == "over") {
            has_stop = true;
        }
    }
    print_test(!has_stop, "Stop words removed", results);
    
    // Check stemming applied
    bool has_stemmed = false;
    for (const auto& t : tokens) {
        if (t.text == "run" || t.text == "jump" || t.text == "fox") {
            has_stemmed = true;
        }
    }
    print_test(has_stemmed, "Stemming applied", results);
    
    // Position tracking
    print_test(tokens[0].position == 0, "First token position = 0", results);
    
}

int main() {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "    Tokenizer Test Suite - Task 2.2                         \n";
    std::cout << "============================================================\n";
    
    TestResults results;
    
    test_case_normalization(results);
    test_stop_words(results);
    test_stemming(results);
    test_full_pipeline(results);
    
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "                    TEST SUMMARY                            \n";
    std::cout << "============================================================\n";
    std::cout << "Passed: " << results.passed << "\n";
    std::cout << "Failed: " << results.failed << "\n";
    std::cout << "\n";
    
    if (results.failed == 0) {
        std::cout << "[SUCCESS] All tokenizer tests passed!\n\n";
        return 0;
    } else {
        std::cout << "[FAILED] Some tests failed\n\n";
        return 1;
    }
}