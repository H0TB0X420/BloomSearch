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

// Test 1: Exact match takes priority over wildcard
void test_exact_match_priority(TestResults& results) {
    std::cout << "\n--- Test: Exact Match Priority ---\n";
    
    RobotsParser parser;
    std::string content = 
        "User-agent: *\n"
        "Disallow: /general-blocked/\n"
        "\n"
        "User-agent: BloomSearchBot\n"
        "Disallow: /bloom-specific/\n";
    
    parser.parse(content);
    
    const AgentRules* rules = parser.get_matching_rules("BloomSearchBot");
    
    print_test(rules != nullptr, "Found matching rules", results);
    
    if (rules) {
        print_test(rules->rules.size() == 1, "Has exactly 1 rule", results);
        if (!rules->rules.empty()) {
            print_test(rules->rules[0].path == "/bloom-specific/", 
                      "Uses BloomSearchBot-specific rule, not wildcard", results);
        } else {
            results.failed++;
        }
    } else {
        results.failed += 2;
    }
}

// Test 2: Falls back to wildcard when no exact match
void test_wildcard_fallback(TestResults& results) {
    std::cout << "\n--- Test: Wildcard Fallback ---\n";
    
    RobotsParser parser;
    std::string content = 
        "User-agent: *\n"
        "Disallow: /private/\n"
        "Disallow: /tmp/\n"
        "\n"
        "User-agent: Googlebot\n"
        "Disallow: /google-only/\n";
    
    parser.parse(content);
    
    // BloomSearchBot not listed, should fall back to *
    const AgentRules* rules = parser.get_matching_rules("BloomSearchBot");
    
    print_test(rules != nullptr, "Found wildcard rules", results);
    
    if (rules) {
        print_test(rules->rules.size() == 2, "Has 2 rules from wildcard", results);
        if (!rules->rules.empty()) {
            print_test(rules->rules[0].path == "/private/", 
                      "First wildcard rule correct", results);
        } else {
            results.failed++;
        }
    } else {
        results.failed += 2;
    }
}

// Test 3: No matching rules returns nullptr
void test_no_matching_rules(TestResults& results) {
    std::cout << "\n--- Test: No Matching Rules ---\n";
    
    RobotsParser parser;
    std::string content = 
        "User-agent: Googlebot\n"
        "Disallow: /google/\n"
        "\n"
        "User-agent: Bingbot\n"
        "Disallow: /bing/\n";
    
    parser.parse(content);
    
    // No BloomSearchBot, no wildcard
    const AgentRules* rules = parser.get_matching_rules("BloomSearchBot");
    
    print_test(rules == nullptr, "Returns nullptr when no match", results);
}

// Test 4: Case insensitive bot name matching
void test_case_insensitive_matching(TestResults& results) {
    std::cout << "\n--- Test: Case Insensitive Matching ---\n";
    
    RobotsParser parser;
    std::string content = 
        "User-agent: bloomsearchbot\n"
        "Disallow: /lowercase/\n";
    
    parser.parse(content);
    
    // Should match despite different casing
    const AgentRules* rules1 = parser.get_matching_rules("BloomSearchBot");
    const AgentRules* rules2 = parser.get_matching_rules("BLOOMSEARCHBOT");
    const AgentRules* rules3 = parser.get_matching_rules("bloomsearchbot");
    
    print_test(rules1 != nullptr, "Matches 'BloomSearchBot'", results);
    print_test(rules2 != nullptr, "Matches 'BLOOMSEARCHBOT'", results);
    print_test(rules3 != nullptr, "Matches 'bloomsearchbot'", results);
    
    if (rules1) {
        print_test(rules1->rules[0].path == "/lowercase/", "Correct rule returned", results);
    } else {
        results.failed++;
    }
}

// Test 5: Empty robots.txt returns nullptr
void test_empty_robots(TestResults& results) {
    std::cout << "\n--- Test: Empty Robots.txt ---\n";
    
    RobotsParser parser;
    parser.parse("");
    
    const AgentRules* rules = parser.get_matching_rules("BloomSearchBot");
    
    print_test(rules == nullptr, "Empty content returns nullptr", results);
}

// Test 6: Wildcard only
void test_wildcard_only(TestResults& results) {
    std::cout << "\n--- Test: Wildcard Only ---\n";
    
    RobotsParser parser;
    std::string content = 
        "User-agent: *\n"
        "Disallow: /admin/\n"
        "Crawl-delay: 5\n";
    
    parser.parse(content);
    
    const AgentRules* rules = parser.get_matching_rules("BloomSearchBot");
    
    print_test(rules != nullptr, "Wildcard rules found", results);
    if (rules) {
        print_test(rules->crawl_delay == 5, "Crawl-delay from wildcard", results);
    } else {
        results.failed++;
    }
}

// Test 7: Bot-specific crawl-delay overrides wildcard
void test_specific_crawl_delay(TestResults& results) {
    std::cout << "\n--- Test: Specific Crawl-Delay Override ---\n";
    
    RobotsParser parser;
    std::string content = 
        "User-agent: *\n"
        "Disallow: /private/\n"
        "Crawl-delay: 10\n"
        "\n"
        "User-agent: BloomSearchBot\n"
        "Disallow: /bloom/\n"
        "Crawl-delay: 2\n";
    
    parser.parse(content);
    
    const AgentRules* rules = parser.get_matching_rules("BloomSearchBot");
    
    print_test(rules != nullptr, "Found specific rules", results);
    if (rules) {
        print_test(rules->crawl_delay == 2, "Uses bot-specific crawl-delay (2), not wildcard (10)", results);
    } else {
        results.failed++;
    }
}

// Test 8: Default bot name
void test_default_bot_name(TestResults& results) {
    std::cout << "\n--- Test: Default Bot Name ---\n";
    
    RobotsParser parser;
    std::string content = 
        "User-agent: BloomSearchBot\n"
        "Disallow: /secret/\n";
    
    parser.parse(content);
    
    // Call without argument - should default to BloomSearchBot
    const AgentRules* rules = parser.get_matching_rules();
    
    print_test(rules != nullptr, "Default bot name works", results);
    if (rules) {
        print_test(rules->rules[0].path == "/secret/", "Correct rules returned", results);
    } else {
        results.failed++;
    }
}

// Test 9: Multiple user-agents sharing rules (group syntax)
void test_grouped_user_agents(TestResults& results) {
    std::cout << "\n--- Test: Grouped User-Agents ---\n";
    
    // Some robots.txt files list multiple user-agents before rules
    // Each should get the same rules
    RobotsParser parser;
    std::string content = 
        "User-agent: BloomSearchBot\n"
        "User-agent: OtherBot\n"
        "Disallow: /shared-block/\n";
    
    parser.parse(content);
    
    const AgentRules* bloom_rules = parser.get_matching_rules("BloomSearchBot");
    const AgentRules* other_rules = parser.get_matching_rules("OtherBot");
    
    print_test(bloom_rules != nullptr, "BloomSearchBot has rules", results);
    print_test(other_rules != nullptr, "OtherBot has rules", results);
    
    // Note: Depending on implementation, these might be same or different objects
    // but both should have the /shared-block/ rule
    if (bloom_rules && !bloom_rules->rules.empty()) {
        print_test(bloom_rules->rules[0].path == "/shared-block/", 
                  "BloomSearchBot has shared rule", results);
    } else {
        results.failed++;
    }
    
    if (other_rules && !other_rules->rules.empty()) {
        print_test(other_rules->rules[0].path == "/shared-block/", 
                  "OtherBot has shared rule", results);
    } else {
        results.failed++;
    }
}

int main() {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "    RobotsParser Test Suite - Subtask 1.1.2                 \n";
    std::cout << "    User-Agent Matching                                      \n";
    std::cout << "============================================================\n";
    
    TestResults results;
    
    test_exact_match_priority(results);
    test_wildcard_fallback(results);
    test_no_matching_rules(results);
    test_case_insensitive_matching(results);
    test_empty_robots(results);
    test_wildcard_only(results);
    test_specific_crawl_delay(results);
    test_default_bot_name(results);
    test_grouped_user_agents(results);
    
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "                    TEST SUMMARY                            \n";
    std::cout << "============================================================\n";
    std::cout << "Passed: " << results.passed << "\n";
    std::cout << "Failed: " << results.failed << "\n";
    std::cout << "\n";
    
    if (results.failed == 0) {
        std::cout << "[SUCCESS] All tests passed - Subtask 1.1.2 complete!\n\n";
        return 0;
    } else {
        std::cout << "[INCOMPLETE] Some tests failed\n\n";
        return 1;
    }
}