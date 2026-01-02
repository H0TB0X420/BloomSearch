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
// Test 1: Basic tokenization (2.2.1)
//=============================================================================
void test_basic_tokenization(TestResults& results) {
    std::cout << "\n--- Test: Basic Tokenization (2.2.1) ---\n";
    
    Tokenizer tokenizer;
    tokenizer.set_remove_stop_words(false);
    tokenizer.set_apply_stemming(false);
    
    // Simple text
    auto tokens1 = tokenizer.tokenize_simple("Hello World");
    print_test(tokens1.size() == 2, "Splits on whitespace", results);
    print_test(tokens1[0] == "hello" && tokens1[1] == "world", "Tokens extracted", results);
    
    // Punctuation
    auto tokens2 = tokenizer.tokenize_simple("Hello, World! How are you?");
    print_test(tokens2.size() == 5, "Handles punctuation", results);
    
    // Numbers
    auto tokens3 = tokenizer.tokenize_simple("Test123 456test");
    print_test(tokens3.size() == 2, "Keeps alphanumeric", results);
    print_test(tokens3[0] == "test123", "Number in token", results);
    
    // Multiple spaces
    auto tokens4 = tokenizer.tokenize_simple("Hello    World");
    print_test(tokens4.size() == 2, "Handles multiple spaces", results);
    
    // Empty input
    auto tokens5 = tokenizer.tokenize_simple("");
    print_test(tokens5.empty(), "Empty input = empty output", results);
}

//=============================================================================
// Test 2: Case normalization (2.2.2)
//=============================================================================
void test_case_normalization(TestResults& results) {
    std::cout << "\n--- Test: Case Normalization (2.2.2) ---\n";
    
    Tokenizer tokenizer;
    tokenizer.set_remove_stop_words(false);
    tokenizer.set_apply_stemming(false);
    
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
    tokenizer.set_apply_stemming(false);
    
    // Check known stop words
    print_test(tokenizer.is_stop_word("the"), "'the' is stop word", results);
    print_test(tokenizer.is_stop_word("and"), "'and' is stop word", results);
    print_test(tokenizer.is_stop_word("is"), "'is' is stop word", results);
    print_test(!tokenizer.is_stop_word("hello"), "'hello' is not stop word", results);
    print_test(!tokenizer.is_stop_word("search"), "'search' is not stop word", results);
    
    // Tokenization with stop word removal
    auto tokens1 = tokenizer.tokenize_simple("The quick brown fox");
    print_test(tokens1.size() == 3, "Stop words removed from text", results);
    
    // Check 'the' was removed
    bool has_the = false;
    for (const auto& t : tokens1) {
        if (t == "the") has_the = true;
    }
    print_test(!has_the, "'the' not in result", results);
    
    // Custom stop word
    tokenizer.add_stop_word("custom");
    print_test(tokenizer.is_stop_word("custom"), "Custom stop word added", results);
    
    // Disable stop word removal
    tokenizer.set_remove_stop_words(false);
    auto tokens2 = tokenizer.tokenize_simple("The quick brown fox");
    print_test(tokens2.size() == 4, "Stop words kept when disabled", results);
}

//=============================================================================
// Test 4: Porter Stemming (2.2.4)
//=============================================================================
void test_stemming(TestResults& results) {
    std::cout << "\n--- Test: Porter Stemming (2.2.4) ---\n";
    
    Tokenizer tokenizer;
    tokenizer.set_remove_stop_words(false);
    
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
// Test 5: N-gram generation (2.2.5)
//=============================================================================
void test_ngrams(TestResults& results) {
    std::cout << "\n--- Test: N-gram Generation (2.2.5) ---\n";
    
    Tokenizer tokenizer;
    
    // Word bigrams
    std::vector<std::string> tokens = {"the", "quick", "brown", "fox"};
    auto bigrams = tokenizer.generate_bigrams(tokens);
    
    print_test(bigrams.size() == 3, "Correct number of bigrams", results);
    print_test(bigrams[0] == "the_quick", "First bigram", results);
    print_test(bigrams[1] == "quick_brown", "Second bigram", results);
    print_test(bigrams[2] == "brown_fox", "Third bigram", results);
    
    // Single token = no bigrams
    auto single = tokenizer.generate_bigrams({"hello"});
    print_test(single.empty(), "Single token = no bigrams", results);
    
    // Character n-grams
    auto char_ngrams = tokenizer.generate_char_ngrams("hello", 3);
    print_test(char_ngrams.size() == 3, "Character trigrams count", results);
    print_test(char_ngrams[0] == "hel", "First trigram", results);
    print_test(char_ngrams[1] == "ell", "Second trigram", results);
    print_test(char_ngrams[2] == "llo", "Third trigram", results);
    
    // Short word = no char n-grams
    auto short_ngrams = tokenizer.generate_char_ngrams("hi", 3);
    print_test(short_ngrams.empty(), "Short word = no trigrams", results);
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
    
    // Query tokenization (keeps more)
    auto query_tokens = tokenizer.tokenize_query("the quick search");
    print_test(query_tokens.size() == 3, "Query keeps stop words", results);
    
    // Simple API matches full API
    auto simple_tokens = tokenizer.tokenize_simple(text);
    print_test(simple_tokens.size() == tokens.size(), "Simple API same count", results);
}

int main() {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "    Tokenizer Test Suite - Task 2.2                         \n";
    std::cout << "============================================================\n";
    
    TestResults results;
    
    test_basic_tokenization(results);
    test_case_normalization(results);
    test_stop_words(results);
    test_stemming(results);
    test_ngrams(results);
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