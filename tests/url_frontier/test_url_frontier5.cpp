#include "crawler/url_frontier.h"
#include <iostream>
#include <fstream>

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

// Helper to create test file
void create_test_file(const std::string& path, const std::string& content) {
    std::ofstream file(path);
    file << content;
}

// Test 1: Basic seed loading
void test_basic_loading(TestResults& results) {
    std::cout << "\n--- Test: Basic Seed Loading ---\n";
    
    create_test_file("/tmp/seeds_basic.txt",
        "http://example1.com/\n"
        "http://example2.com/\n"
        "http://example3.com/\n"
    );
    
    URLFrontier frontier;
    bool success = frontier.load_seeds("/tmp/seeds_basic.txt");
    
    print_test(success, "load_seeds returned true", results);
    print_test(frontier.pending_count() == 3, "Loaded 3 URLs", results);
}

// Test 2: Skip comments and empty lines
void test_comments_and_empty(TestResults& results) {
    std::cout << "\n--- Test: Comments and Empty Lines ---\n";
    
    create_test_file("/tmp/seeds_comments.txt",
        "# This is a comment\n"
        "http://example1.com/\n"
        "\n"
        "   \n"
        "# Another comment\n"
        "http://example2.com/\n"
        "  # Indented comment\n"
    );
    
    URLFrontier frontier;
    frontier.load_seeds("/tmp/seeds_comments.txt");
    
    print_test(frontier.pending_count() == 2, "Loaded 2 URLs (skipped comments/empty)", results);
}

// Test 3: Whitespace trimming
void test_whitespace_trimming(TestResults& results) {
    std::cout << "\n--- Test: Whitespace Trimming ---\n";
    
    create_test_file("/tmp/seeds_whitespace.txt",
        "  http://example1.com/  \n"
        "\thttp://example2.com/\t\n"
        "http://example3.com/\r\n"
    );
    
    URLFrontier frontier;
    frontier.load_seeds("/tmp/seeds_whitespace.txt");
    
    print_test(frontier.pending_count() == 3, "Loaded 3 URLs with trimmed whitespace", results);
}

// Test 4: File not found
void test_file_not_found(TestResults& results) {
    std::cout << "\n--- Test: File Not Found ---\n";
    
    URLFrontier frontier;
    bool success = frontier.load_seeds("/nonexistent/path/seeds.txt");
    
    print_test(success == false, "Returns false for missing file", results);
    print_test(frontier.empty(), "Frontier still empty", results);
}

// Test 5: Invalid URLs skipped
void test_invalid_urls_skipped(TestResults& results) {
    std::cout << "\n--- Test: Invalid URLs Skipped ---\n";
    
    create_test_file("/tmp/seeds_invalid.txt",
        "http://valid.com/\n"
        "not-a-url\n"
        "ftp://invalid-scheme.com/\n"
        "http://also-valid.com/\n"
    );
    
    URLFrontier frontier;
    frontier.load_seeds("/tmp/seeds_invalid.txt");
    
    print_test(frontier.pending_count() == 2, "Loaded 2 valid URLs (skipped 2 invalid)", results);
}

// Test 6: Seeds get SEED priority
void test_seed_priority(TestResults& results) {
    std::cout << "\n--- Test: Seeds Get SEED Priority ---\n";
    
    create_test_file("/tmp/seeds_priority.txt",
        "http://seed.com/page\n"
    );
    
    URLFrontier frontier;
    
    // Add a LOW priority URL first
    frontier.add("http://other.com/low", URLFrontier::Priority::LOW);
    
    // Load seed
    frontier.load_seeds("/tmp/seeds_priority.txt");
    
    // Seed should come out first despite being added second
    auto url = frontier.get_next();
    print_test(url.has_value() && url->find("seed.com") != std::string::npos,
               "SEED priority URL comes first", results);
}

// Test 7: Duplicate seeds
void test_duplicate_seeds(TestResults& results) {
    std::cout << "\n--- Test: Duplicate Seeds ---\n";
    
    create_test_file("/tmp/seeds_dups.txt",
        "http://example.com/page\n"
        "http://example.com/page\n"
        "HTTP://EXAMPLE.COM/page\n"
    );
    
    URLFrontier frontier;
    frontier.load_seeds("/tmp/seeds_dups.txt");
    
    print_test(frontier.pending_count() == 1, "Duplicates deduplicated", results);
}

int main() {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "    URL Frontier Test Suite - Subtask 1.2.6                 \n";
    std::cout << "    Seed URL Loading                                         \n";
    std::cout << "============================================================\n";
    
    TestResults results;
    
    test_basic_loading(results);
    test_comments_and_empty(results);
    test_whitespace_trimming(results);
    test_file_not_found(results);
    test_invalid_urls_skipped(results);
    test_seed_priority(results);
    test_duplicate_seeds(results);
    
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "                    TEST SUMMARY                            \n";
    std::cout << "============================================================\n";
    std::cout << "Passed: " << results.passed << "\n";
    std::cout << "Failed: " << results.failed << "\n";
    std::cout << "\n";
    
    if (results.failed == 0) {
        std::cout << "[SUCCESS] All tests passed - Subtask 1.2.6 complete!\n\n";
        return 0;
    } else {
        std::cout << "[INCOMPLETE] Some tests failed\n\n";
        return 1;
    }
}