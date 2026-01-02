#include "crawler/robots_parser.h"
#include "common/logger.h"
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

// Test 1: Basic Disallow
void test_basic_disallow(TestResults& results) {
    std::cout << "\n--- Test: Basic Disallow ---\n";
    
    RobotsParser parser;
    std::string content = 
        "User-agent: *\n"
        "Disallow: /private/\n"
        "Disallow: /tmp/\n";
    
    parser.parse(content);
    
    print_test(parser.is_allowed("/") == true, "Root is allowed", results);
    print_test(parser.is_allowed("/public/page.html") == true, "Public page allowed", results);
    print_test(parser.is_allowed("/private/") == false, "Exact /private/ blocked", results);
    print_test(parser.is_allowed("/private/secret.html") == false, "/private/secret.html blocked", results);
    print_test(parser.is_allowed("/private/deep/path/file.txt") == false, "Deep private path blocked", results);
    print_test(parser.is_allowed("/tmp/cache.txt") == false, "/tmp/ blocked", results);
    print_test(parser.is_allowed("/privateer/") == true, "/privateer/ allowed (not prefix match)", results);
}

// Test 2: Allow overrides Disallow
void test_allow_override(TestResults& results) {
    std::cout << "\n--- Test: Allow Overrides Disallow ---\n";
    
    RobotsParser parser;
    std::string content = 
        "User-agent: *\n"
        "Disallow: /private/\n"
        "Allow: /private/public/\n";
    
    parser.parse(content);
    
    print_test(parser.is_allowed("/private/secret.html") == false, "/private/secret.html blocked", results);
    print_test(parser.is_allowed("/private/public/") == true, "/private/public/ allowed", results);
    print_test(parser.is_allowed("/private/public/page.html") == true, "/private/public/page.html allowed", results);
}

// Test 3: Specificity - longer match wins
void test_specificity(TestResults& results) {
    std::cout << "\n--- Test: Specificity (Longer Match Wins) ---\n";
    
    RobotsParser parser;
    std::string content = 
        "User-agent: *\n"
        "Allow: /p\n"
        "Disallow: /private/\n"
        "Allow: /private/public/\n"
        "Disallow: /private/public/secret/\n";
    
    parser.parse(content);
    
    print_test(parser.is_allowed("/p") == true, "/p allowed (Allow: /p)", results);
    print_test(parser.is_allowed("/private/") == false, "/private/ blocked", results);
    print_test(parser.is_allowed("/private/public/") == true, "/private/public/ allowed (more specific)", results);
    print_test(parser.is_allowed("/private/public/secret/") == false, "/private/public/secret/ blocked (most specific)", results);
    print_test(parser.is_allowed("/private/public/secret/file.txt") == false, "Deep secret path blocked", results);
}

// Test 4: Empty Disallow means allow all
void test_empty_disallow(TestResults& results) {
    std::cout << "\n--- Test: Empty Disallow (Allow All) ---\n";
    
    RobotsParser parser;
    std::string content = 
        "User-agent: *\n"
        "Disallow:\n";  // Empty = allow everything
    
    parser.parse(content);
    
    print_test(parser.is_allowed("/") == true, "Root allowed", results);
    print_test(parser.is_allowed("/anything/at/all") == true, "Any path allowed", results);
}

// Test 5: Disallow all
void test_disallow_all(TestResults& results) {
    std::cout << "\n--- Test: Disallow All ---\n";
    
    RobotsParser parser;
    std::string content = 
        "User-agent: *\n"
        "Disallow: /\n";  // Block everything
    
    parser.parse(content);
    
    print_test(parser.is_allowed("/") == false, "Root blocked", results);
    print_test(parser.is_allowed("/any/path") == false, "All paths blocked", results);
}

// Test 6: No matching rules = allowed
void test_no_rules_allowed(TestResults& results) {
    std::cout << "\n--- Test: No Rules = Allowed ---\n";
    
    RobotsParser parser;
    parser.parse("");  // Empty robots.txt
    
    print_test(parser.is_allowed("/") == true, "Empty robots.txt = root allowed", results);
    print_test(parser.is_allowed("/anything") == true, "Empty robots.txt = all allowed", results);
    
    // Also test when rules exist but none match our bot
    RobotsParser parser2;
    parser2.parse("User-agent: Googlebot\nDisallow: /\n");
    
    print_test(parser2.is_allowed("/") == true, "No rules for our bot = allowed", results);
}

// Test 7: Full URL handling (extract path)
void test_full_url(TestResults& results) {
    std::cout << "\n--- Test: Full URL Handling ---\n";
    
    RobotsParser parser;
    std::string content = 
        "User-agent: *\n"
        "Disallow: /private/\n";
    
    parser.parse(content);
    
    // Should work with full URLs (extract path) or just paths
    print_test(parser.is_allowed("https://example.com/public/page.html") == true, 
               "Full URL public path allowed", results);
    print_test(parser.is_allowed("https://example.com/private/secret.html") == false, 
               "Full URL private path blocked", results);
    print_test(parser.is_allowed("http://other.com/private/file.txt") == false, 
               "Different domain, same path blocked", results);
}

// Test 8: Query strings and fragments
void test_query_and_fragments(TestResults& results) {
    std::cout << "\n--- Test: Query Strings and Fragments ---\n";
    
    RobotsParser parser;
    std::string content = 
        "User-agent: *\n"
        "Disallow: /search\n";
    
    parser.parse(content);
    
    // Query strings are part of the path for matching purposes
    print_test(parser.is_allowed("/search") == false, "/search blocked", results);
    print_test(parser.is_allowed("/search?q=test") == false, "/search?q=test blocked", results);
    print_test(parser.is_allowed("/search#results") == false, "/search#results blocked", results);
}

// Test 9: Case sensitivity (paths ARE case-sensitive)
void test_case_sensitivity(TestResults& results) {
    std::cout << "\n--- Test: Case Sensitivity ---\n";
    
    RobotsParser parser;
    std::string content = 
        "User-agent: *\n"
        "Disallow: /Private/\n";
    
    parser.parse(content);
    
    // Paths are case-sensitive per RFC
    print_test(parser.is_allowed("/Private/") == false, "/Private/ blocked (exact case)", results);
    print_test(parser.is_allowed("/private/") == true, "/private/ allowed (different case)", results);
    print_test(parser.is_allowed("/PRIVATE/") == true, "/PRIVATE/ allowed (different case)", results);
}

// Test 10: Wildcard and end-of-string patterns (bonus - may skip)
void test_wildcards(TestResults& results) {
    std::cout << "\n--- Test: Wildcards (Optional/Bonus) ---\n";
    
    RobotsParser parser;
    std::string content = 
        "User-agent: *\n"
        "Disallow: /*.pdf$\n"    // Block all PDFs
        "Disallow: /tmp/*\n";    // Block everything under /tmp/
    
    parser.parse(content);
    
    // Note: Wildcard support is optional for basic implementation
    // If not implemented, these can be skipped
    bool has_wildcard_support = false;  // Set to true if you implement wildcards
    
    if (has_wildcard_support) {
        print_test(parser.is_allowed("/document.pdf") == false, "*.pdf$ blocks PDFs", results);
        print_test(parser.is_allowed("/document.pdf.bak") == true, "*.pdf$ doesn't block .pdf.bak", results);
        print_test(parser.is_allowed("/tmp/file.txt") == false, "/tmp/* blocks files", results);
    } else {
        std::cout << "[SKIP] Wildcard support not implemented (optional)\n";
        results.passed += 0;  // Don't count as pass or fail
    }
}

// Test 11: Bot-specific rules
void test_bot_specific(TestResults& results) {
    std::cout << "\n--- Test: Bot-Specific Rules ---\n";
    
    RobotsParser parser;
    std::string content = 
        "User-agent: *\n"
        "Disallow: /general-block/\n"
        "\n"
        "User-agent: BloomSearchBot\n"
        "Disallow: /bloom-block/\n"
        "Allow: /general-block/\n";  // Override for our bot
    
    parser.parse(content);
    
    // BloomSearchBot should use its specific rules, not wildcard
    print_test(parser.is_allowed("/bloom-block/") == false, 
               "BloomSearchBot blocked from /bloom-block/", results);
    print_test(parser.is_allowed("/general-block/") == true, 
               "BloomSearchBot allowed /general-block/ (overridden)", results);
    print_test(parser.is_allowed("/other/") == true, 
               "BloomSearchBot allowed other paths", results);
}

int main() {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "    RobotsParser Test Suite - Subtask 1.1.3                 \n";
    std::cout << "    Disallow/Allow Rules (is_allowed)                        \n";
    std::cout << "============================================================\n";
    
    TestResults results;
    
    test_basic_disallow(results);
    test_allow_override(results);
    test_specificity(results);
    test_empty_disallow(results);
    test_disallow_all(results);
    test_no_rules_allowed(results);
    test_full_url(results);
    test_query_and_fragments(results);
    test_case_sensitivity(results);
    test_wildcards(results);
    test_bot_specific(results);
    
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "                    TEST SUMMARY                            \n";
    std::cout << "============================================================\n";
    std::cout << "Passed: " << results.passed << "\n";
    std::cout << "Failed: " << results.failed << "\n";
    std::cout << "\n";
    
    if (results.failed == 0) {
        std::cout << "[SUCCESS] All tests passed - Subtask 1.1.3 complete!\n\n";
        return 0;
    } else {
        std::cout << "[INCOMPLETE] Some tests failed\n\n";
        return 1;
    }
}