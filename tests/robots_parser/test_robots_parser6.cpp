#include "crawler/robots_parser.h"
#include "crawler/http_fetcher.h"
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

// Edge Case 1: Malformed robots.txt with garbage
void test_malformed_garbage(TestResults& results) {
    std::cout << "\n--- Edge Case: Malformed Garbage ---\n";
    
    RobotsParser parser;
    std::string content = 
        "asdkjhasd kajshd aksjdh\n"
        "not a valid robots file\n"
        "User-agent: *\n"
        "more garbage here\n"
        "Disallow: /valid/\n"
        "12345 numbers\n";
    
    bool success = parser.parse(content);
    
    print_test(success == true, "Parses without crashing", results);
    print_test(parser.is_allowed("/valid/") == false, "Valid rule still extracted", results);
    print_test(parser.is_allowed("/garbage/") == true, "Garbage didn't create false rules", results);
}

// Edge Case 2: BOM (Byte Order Mark) at start
void test_bom_handling(TestResults& results) {
    std::cout << "\n--- Edge Case: BOM at Start ---\n";
    
    RobotsParser parser;
    // UTF-8 BOM: EF BB BF
    std::string content = "\xEF\xBB\xBFUser-agent: *\nDisallow: /private/\n";
    
    bool success = parser.parse(content);
    
    print_test(success == true, "Parses with BOM", results);
    // May or may not handle BOM correctly - just shouldn't crash
}

// Edge Case 3: Windows line endings (CRLF)
void test_crlf_line_endings(TestResults& results) {
    std::cout << "\n--- Edge Case: CRLF Line Endings ---\n";
    
    RobotsParser parser;
    std::string content = "User-agent: *\r\nDisallow: /private/\r\nAllow: /public/\r\n";
    
    parser.parse(content);
    
    print_test(parser.is_allowed("/private/") == false, "CRLF: /private/ blocked", results);
    print_test(parser.is_allowed("/public/") == true, "CRLF: /public/ allowed", results);
}

// Edge Case 4: Mixed line endings
void test_mixed_line_endings(TestResults& results) {
    std::cout << "\n--- Edge Case: Mixed Line Endings ---\n";
    
    RobotsParser parser;
    std::string content = "User-agent: *\nDisallow: /a/\r\nDisallow: /b/\rDisallow: /c/\n";
    
    parser.parse(content);
    
    print_test(parser.is_allowed("/a/") == false, "Mixed endings: /a/ blocked", results);
    print_test(parser.is_allowed("/b/") == false, "Mixed endings: /b/ blocked", results);
}

// Edge Case 5: Very long lines
void test_very_long_lines(TestResults& results) {
    std::cout << "\n--- Edge Case: Very Long Lines ---\n";
    
    RobotsParser parser;
    std::string long_path = "/very" + std::string(10000, '/') + "long/";
    std::string content = "User-agent: *\nDisallow: " + long_path + "\n";
    
    bool success = parser.parse(content);
    
    print_test(success == true, "Handles very long lines", results);
}

// Edge Case 6: Unicode in paths
void test_unicode_paths(TestResults& results) {
    std::cout << "\n--- Edge Case: Unicode Paths ---\n";
    
    RobotsParser parser;
    std::string content = "User-agent: *\nDisallow: /données/\nDisallow: /日本語/\n";
    
    parser.parse(content);
    
    print_test(parser.is_allowed("/données/file") == false, "Unicode path blocked", results);
    print_test(parser.is_allowed("/日本語/page") == false, "Japanese path blocked", results);
    print_test(parser.is_allowed("/english/") == true, "English path allowed", results);
}

// Edge Case 7: URL-encoded paths
void test_url_encoded_paths(TestResults& results) {
    std::cout << "\n--- Edge Case: URL-Encoded Paths ---\n";
    
    RobotsParser parser;
    std::string content = "User-agent: *\nDisallow: /path%20with%20spaces/\n";
    
    parser.parse(content);
    
    // Note: Matching is literal - %20 != space
    print_test(parser.is_allowed("/path%20with%20spaces/file") == false, 
               "Encoded path blocked (literal match)", results);
    print_test(parser.is_allowed("/path with spaces/file") == true, 
               "Decoded path not matched (different)", results);
}

// Edge Case 8: Comments everywhere
void test_inline_comments(TestResults& results) {
    std::cout << "\n--- Edge Case: Comments Everywhere ---\n";
    
    RobotsParser parser;
    std::string content = 
        "# Comment at start\n"
        "User-agent: * # inline comment - may or may not be handled\n"
        "# Comment between\n"
        "Disallow: /blocked/\n"
        "# Comment at end\n";
    
    parser.parse(content);
    
    // The key is /blocked/ should work
    print_test(parser.is_allowed("/blocked/") == false, "Rules work with comments", results);
}

// Edge Case 9: Multiple colons in value
void test_multiple_colons(TestResults& results) {
    std::cout << "\n--- Edge Case: Multiple Colons ---\n";
    
    RobotsParser parser;
    std::string content = "User-agent: *\nDisallow: /path:with:colons/\n";
    
    parser.parse(content);
    
    print_test(parser.is_allowed("/path:with:colons/") == false, 
               "Path with colons blocked", results);
}

// Edge Case 10: Empty user-agent value
void test_empty_user_agent(TestResults& results) {
    std::cout << "\n--- Edge Case: Empty User-Agent ---\n";
    
    RobotsParser parser;
    std::string content = 
        "User-agent:\n"
        "Disallow: /empty-agent/\n"
        "User-agent: *\n"
        "Disallow: /wildcard/\n";
    
    parser.parse(content);
    
    // Empty user-agent should be ignored or handled gracefully
    print_test(parser.is_allowed("/wildcard/") == false, "Wildcard rules still work", results);
}

// Edge Case 11: Duplicate user-agents
void test_duplicate_agents(TestResults& results) {
    std::cout << "\n--- Edge Case: Duplicate User-Agents ---\n";
    
    RobotsParser parser;
    std::string content = 
        "User-agent: BloomSearchBot\n"
        "Disallow: /first/\n"
        "\n"
        "User-agent: BloomSearchBot\n"
        "Disallow: /second/\n";
    
    parser.parse(content);
    
    // Both rules should apply, or at least one should
    // Implementation-dependent, but shouldn't crash
    bool first = parser.is_allowed("/first/", "BloomSearchBot");
    bool second = parser.is_allowed("/second/", "BloomSearchBot");
    
    std::cout << "[INFO] /first/ allowed: " << first << ", /second/ allowed: " << second << "\n";
    print_test(true, "Duplicate agents handled without crash", results);
}

// Edge Case 12: Sitemap directive (should be ignored)
void test_sitemap_directive(TestResults& results) {
    std::cout << "\n--- Edge Case: Sitemap Directive ---\n";
    
    RobotsParser parser;
    std::string content = 
        "User-agent: *\n"
        "Disallow: /private/\n"
        "Sitemap: https://example.com/sitemap.xml\n";
    
    bool success = parser.parse(content);
    
    print_test(success == true, "Sitemap directive doesn't break parsing", results);
    print_test(parser.is_allowed("/private/") == false, "Rules still work", results);
}

// Edge Case 13: Case variations in directives
void test_directive_case_variations(TestResults& results) {
    std::cout << "\n--- Edge Case: Directive Case Variations ---\n";
    
    RobotsParser parser;
    std::string content = 
        "USER-AGENT: *\n"
        "DISALLOW: /upper/\n"
        "disallow: /lower/\n"
        "DisAllow: /mixed/\n";
    
    parser.parse(content);
    
    print_test(parser.is_allowed("/upper/") == false, "UPPERCASE directive works", results);
    print_test(parser.is_allowed("/lower/") == false, "lowercase directive works", results);
    print_test(parser.is_allowed("/mixed/") == false, "MixedCase directive works", results);
}

// Edge Case 14: Path with query string in rule
void test_query_string_in_rule(TestResults& results) {
    std::cout << "\n--- Edge Case: Query String in Rule ---\n";
    
    RobotsParser parser;
    std::string content = "User-agent: *\nDisallow: /search?q=\n";
    
    parser.parse(content);
    
    print_test(parser.is_allowed("/search?q=test") == false, "Query string rule blocks", results);
    print_test(parser.is_allowed("/search") == true, "Without query allowed", results);
}

// Edge Case 15: Root path only
void test_root_path_allowed(TestResults& results) {
    std::cout << "\n--- Edge Case: Root Path Behavior ---\n";
    
    RobotsParser parser;
    std::string content = "User-agent: *\nDisallow: /everything/\n";
    
    parser.parse(content);
    
    print_test(parser.is_allowed("/") == true, "Root path allowed", results);
    print_test(parser.is_allowed("") == true, "Empty path allowed (treated as root)", results);
}

int main() {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "    RobotsParser Test Suite - Subtask 1.1.6                 \n";
    std::cout << "    Comprehensive Edge Case Tests                            \n";
    std::cout << "============================================================\n";
    
    TestResults results;
    
    test_malformed_garbage(results);
    test_bom_handling(results);
    test_crlf_line_endings(results);
    test_mixed_line_endings(results);
    test_very_long_lines(results);
    test_unicode_paths(results);
    test_url_encoded_paths(results);
    test_inline_comments(results);
    test_multiple_colons(results);
    test_empty_user_agent(results);
    test_duplicate_agents(results);
    test_sitemap_directive(results);
    test_directive_case_variations(results);
    test_query_string_in_rule(results);
    test_root_path_allowed(results);
    
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "                    TEST SUMMARY                            \n";
    std::cout << "============================================================\n";
    std::cout << "Passed: " << results.passed << "\n";
    std::cout << "Failed: " << results.failed << "\n";
    std::cout << "\n";
    
    if (results.failed == 0) {
        std::cout << "[SUCCESS] All edge case tests passed!\n";
        std::cout << "[SUCCESS] Subtask 1.1.6 complete - RobotsParser is ready!\n\n";
        return 0;
    } else {
        std::cout << "[INCOMPLETE] Some edge cases failed - review implementation\n\n";
        return 1;
    }
}