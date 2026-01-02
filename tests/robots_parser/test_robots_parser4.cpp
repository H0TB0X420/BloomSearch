#include "crawler/robots_parser.h"
#include "common/logger.h"
#include <iostream>

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

// Test 1: Basic crawl-delay
void test_basic_crawl_delay(TestResults& results) {
    std::cout << "\n--- Test: Basic Crawl-Delay ---\n";
    
    RobotsParser parser;
    std::string content = 
        "User-agent: *\n"
        "Crawl-delay: 10\n"
        "Disallow: /private/\n";
    
    parser.parse(content);
    
    print_test(parser.get_crawl_delay() == 10, "Crawl-delay is 10 seconds", results);
}

// Test 2: Bot-specific crawl-delay
void test_bot_specific_delay(TestResults& results) {
    std::cout << "\n--- Test: Bot-Specific Crawl-Delay ---\n";
    
    RobotsParser parser;
    std::string content = 
        "User-agent: *\n"
        "Crawl-delay: 10\n"
        "\n"
        "User-agent: BloomSearchBot\n"
        "Crawl-delay: 2\n";
    
    parser.parse(content);
    
    print_test(parser.get_crawl_delay("BloomSearchBot") == 2, 
               "BloomSearchBot gets crawl-delay of 2", results);
    print_test(parser.get_crawl_delay("OtherBot") == 10, 
               "OtherBot falls back to wildcard delay of 10", results);
}

// Test 3: No crawl-delay specified
void test_no_crawl_delay(TestResults& results) {
    std::cout << "\n--- Test: No Crawl-Delay Specified ---\n";
    
    RobotsParser parser;
    std::string content = 
        "User-agent: *\n"
        "Disallow: /private/\n";
    
    parser.parse(content);
    
    print_test(parser.get_crawl_delay() == -1, "Returns -1 when not specified", results);
}

// Test 4: Empty robots.txt
void test_empty_robots(TestResults& results) {
    std::cout << "\n--- Test: Empty Robots.txt ---\n";
    
    RobotsParser parser;
    parser.parse("");
    
    print_test(parser.get_crawl_delay() == -1, "Empty robots.txt returns -1", results);
}

// Test 5: No matching agent
void test_no_matching_agent(TestResults& results) {
    std::cout << "\n--- Test: No Matching Agent ---\n";
    
    RobotsParser parser;
    std::string content = 
        "User-agent: Googlebot\n"
        "Crawl-delay: 5\n";
    
    parser.parse(content);
    
    print_test(parser.get_crawl_delay("BloomSearchBot") == -1, 
               "No matching agent returns -1", results);
}

// Test 6: Wildcard fallback
void test_wildcard_fallback(TestResults& results) {
    std::cout << "\n--- Test: Wildcard Fallback ---\n";
    
    RobotsParser parser;
    std::string content = 
        "User-agent: *\n"
        "Crawl-delay: 15\n"
        "\n"
        "User-agent: Googlebot\n"
        "Crawl-delay: 1\n";
    
    parser.parse(content);
    
    // BloomSearchBot not listed, should fall back to *
    print_test(parser.get_crawl_delay("BloomSearchBot") == 15, 
               "Falls back to wildcard crawl-delay", results);
}

// Test 7: Zero crawl-delay
void test_zero_delay(TestResults& results) {
    std::cout << "\n--- Test: Zero Crawl-Delay ---\n";
    
    RobotsParser parser;
    std::string content = 
        "User-agent: *\n"
        "Crawl-delay: 0\n";
    
    parser.parse(content);
    
    print_test(parser.get_crawl_delay() == 0, "Zero crawl-delay is valid", results);
}

// Test 8: Large crawl-delay
void test_large_delay(TestResults& results) {
    std::cout << "\n--- Test: Large Crawl-Delay ---\n";
    
    RobotsParser parser;
    std::string content = 
        "User-agent: *\n"
        "Crawl-delay: 3600\n";  // 1 hour
    
    parser.parse(content);
    
    print_test(parser.get_crawl_delay() == 3600, "Large crawl-delay (3600) works", results);
}

// Test 9: Bot with rules but no crawl-delay
void test_bot_no_delay(TestResults& results) {
    std::cout << "\n--- Test: Bot Has Rules But No Crawl-Delay ---\n";
    
    RobotsParser parser;
    std::string content = 
        "User-agent: *\n"
        "Crawl-delay: 10\n"
        "\n"
        "User-agent: BloomSearchBot\n"
        "Disallow: /private/\n";  // No crawl-delay for BloomSearchBot
    
    parser.parse(content);
    
    // BloomSearchBot has specific rules, so uses those (no crawl-delay = -1)
    // Does NOT fall back to wildcard's crawl-delay
    print_test(parser.get_crawl_delay("BloomSearchBot") == -1, 
               "Bot-specific rules without crawl-delay returns -1", results);
}

// Test 10: Default parameter
void test_default_parameter(TestResults& results) {
    std::cout << "\n--- Test: Default Bot Name Parameter ---\n";
    
    RobotsParser parser;
    std::string content = 
        "User-agent: BloomSearchBot\n"
        "Crawl-delay: 5\n";
    
    parser.parse(content);
    
    // Should default to BloomSearchBot
    print_test(parser.get_crawl_delay() == 5, "Default parameter uses BloomSearchBot", results);
}

int main() {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "    RobotsParser Test Suite - Subtask 1.1.4                 \n";
    std::cout << "    Crawl-Delay Extraction                                   \n";
    std::cout << "============================================================\n";
    
    TestResults results;
    
    test_basic_crawl_delay(results);
    test_bot_specific_delay(results);
    test_no_crawl_delay(results);
    test_empty_robots(results);
    test_no_matching_agent(results);
    test_wildcard_fallback(results);
    test_zero_delay(results);
    test_large_delay(results);
    test_bot_no_delay(results);
    test_default_parameter(results);
    
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "                    TEST SUMMARY                            \n";
    std::cout << "============================================================\n";
    std::cout << "Passed: " << results.passed << "\n";
    std::cout << "Failed: " << results.failed << "\n";
    std::cout << "\n";
    
    if (results.failed == 0) {
        std::cout << "[SUCCESS] All tests passed - Subtask 1.1.4 complete!\n\n";
        return 0;
    } else {
        std::cout << "[INCOMPLETE] Some tests failed\n\n";
        return 1;
    }
}