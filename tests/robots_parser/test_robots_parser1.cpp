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

// Test 1: Empty content
void test_empty_content(TestResults& results) {
    std::cout << "\n--- Test: Empty Content ---\n";
    
    RobotsParser parser;
    bool success = parser.parse("");
    
    print_test(success == true, "Empty string parses successfully", results);
    print_test(parser.agent_count() == 0, "No agents in empty content", results);
}

// Test 2: Comments only
void test_comments_only(TestResults& results) {
    std::cout << "\n--- Test: Comments Only ---\n";
    
    RobotsParser parser;
    std::string content = 
        "# This is a comment\n"
        "# Another comment\n"
        "\n"
        "   # Indented comment\n";
    
    bool success = parser.parse(content);
    
    print_test(success == true, "Comments-only content parses", results);
    print_test(parser.agent_count() == 0, "No agents from comments", results);
}

// Test 3: Single user-agent with rules
void test_single_agent(TestResults& results) {
    std::cout << "\n--- Test: Single User-Agent ---\n";
    
    RobotsParser parser;
    std::string content = 
        "User-agent: *\n"
        "Disallow: /private/\n"
        "Disallow: /tmp/\n";
    
    bool success = parser.parse(content);
    
    print_test(success == true, "Single agent parses", results);
    print_test(parser.agent_count() == 1, "One agent found", results);
    print_test(parser.has_agent("*") == true, "Wildcard agent exists", results);
    
    const auto* rules = parser.get_agent_rules("*");
    print_test(rules != nullptr, "Can retrieve rules", results);
    if (rules) {
        print_test(rules->rules.size() == 2, "Two rules found", results);
        print_test(rules->rules[0].allow == false, "First rule is Disallow", results);
        print_test(rules->rules[0].path == "/private/", "First path correct", results);
    } else {
        results.failed += 3;
    }
}

// Test 4: Multiple user-agents
void test_multiple_agents(TestResults& results) {
    std::cout << "\n--- Test: Multiple User-Agents ---\n";
    
    RobotsParser parser;
    std::string content = 
        "User-agent: *\n"
        "Disallow: /private/\n"
        "\n"
        "User-agent: BloomSearchBot\n"
        "Disallow: /secret/\n"
        "Allow: /secret/public/\n";
    
    bool success = parser.parse(content);
    
    print_test(success == true, "Multiple agents parse", results);
    print_test(parser.agent_count() == 2, "Two agents found", results);
    print_test(parser.has_agent("*") == true, "Wildcard exists", results);
    print_test(parser.has_agent("BloomSearchBot") == true, "BloomSearchBot exists", results);
    
    const auto* bloom_rules = parser.get_agent_rules("BloomSearchBot");
    if (bloom_rules) {
        print_test(bloom_rules->rules.size() == 2, "BloomSearchBot has 2 rules", results);
        print_test(bloom_rules->rules[1].allow == true, "Second rule is Allow", results);
    } else {
        results.failed += 2;
    }
}

// Test 5: Case insensitivity for directives and user-agents
void test_case_insensitivity(TestResults& results) {
    std::cout << "\n--- Test: Case Insensitivity ---\n";
    
    RobotsParser parser;
    std::string content = 
        "USER-AGENT: TestBot\n"
        "DISALLOW: /admin/\n"
        "ALLOW: /admin/login/\n";
    
    bool success = parser.parse(content);
    
    print_test(success == true, "Uppercase directives parse", results);
    print_test(parser.has_agent("testbot") == true, "Agent found (lowercase lookup)", results);
    print_test(parser.has_agent("TESTBOT") == true, "Agent found (uppercase lookup)", results);
    print_test(parser.has_agent("TestBot") == true, "Agent found (mixed case lookup)", results);
}

// Test 6: Crawl-delay directive
void test_crawl_delay(TestResults& results) {
    std::cout << "\n--- Test: Crawl-Delay ---\n";
    
    RobotsParser parser;
    std::string content = 
        "User-agent: *\n"
        "Crawl-delay: 10\n"
        "Disallow: /slow/\n";
    
    bool success = parser.parse(content);
    
    print_test(success == true, "Crawl-delay content parses", results);
    
    const auto* rules = parser.get_agent_rules("*");
    if (rules) {
        print_test(rules->crawl_delay == 10, "Crawl-delay is 10", results);
    } else {
        results.failed++;
    }
}

// Test 7: Whitespace handling
void test_whitespace_handling(TestResults& results) {
    std::cout << "\n--- Test: Whitespace Handling ---\n";
    
    RobotsParser parser;
    std::string content = 
        "  User-agent:   *  \n"
        "  Disallow:   /path/  \n"
        "\n"
        "\n"
        "User-agent: Bot2\n"
        "Disallow:/nowhitespace\n";  // No space after colon
    
    bool success = parser.parse(content);
    
    print_test(success == true, "Whitespace content parses", results);
    print_test(parser.agent_count() == 2, "Both agents found", results);
    
    const auto* rules = parser.get_agent_rules("*");
    if (rules && !rules->rules.empty()) {
        print_test(rules->rules[0].path == "/path/", "Path trimmed correctly", results);
    } else {
        results.failed++;
    }
    
    const auto* bot2_rules = parser.get_agent_rules("Bot2");
    if (bot2_rules && !bot2_rules->rules.empty()) {
        print_test(bot2_rules->rules[0].path == "/nowhitespace", "No-space path works", results);
    } else {
        results.failed++;
    }
}

// Test 8: Invalid/malformed lines (should be ignored)
void test_malformed_lines(TestResults& results) {
    std::cout << "\n--- Test: Malformed Lines ---\n";
    
    RobotsParser parser;
    std::string content = 
        "User-agent: *\n"
        "This is not a valid line\n"
        "Disallow: /valid/\n"
        "Random garbage here\n"
        ": empty directive\n"
        "Disallow:\n";  // Empty path (should be handled)
    
    bool success = parser.parse(content);
    
    print_test(success == true, "Malformed content parses (ignoring bad lines)", results);
    
    const auto* rules = parser.get_agent_rules("*");
    if (rules) {
        // Should have at least the /valid/ rule
        bool found_valid = false;
        for (const auto& rule : rules->rules) {
            if (rule.path == "/valid/") found_valid = true;
        }
        print_test(found_valid, "Valid rule was parsed", results);
    } else {
        results.failed++;
    }
}

int main() {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "    RobotsParser Test Suite - Subtask 1.1.1                 \n";
    std::cout << "    Basic Parser Structure                                   \n";
    std::cout << "============================================================\n";
    
    TestResults results;
    
    test_empty_content(results);
    test_comments_only(results);
    test_single_agent(results);
    test_multiple_agents(results);
    test_case_insensitivity(results);
    test_crawl_delay(results);
    test_whitespace_handling(results);
    test_malformed_lines(results);
    
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "                    TEST SUMMARY                            \n";
    std::cout << "============================================================\n";
    std::cout << "Passed: " << results.passed << "\n";
    std::cout << "Failed: " << results.failed << "\n";
    std::cout << "\n";
    
    if (results.failed == 0) {
        std::cout << "[SUCCESS] All tests passed - Subtask 1.1.1 complete!\n\n";
        return 0;
    } else {
        std::cout << "[INCOMPLETE] Some tests failed\n\n";
        return 1;
    }
}