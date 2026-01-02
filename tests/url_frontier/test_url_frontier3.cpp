#include "crawler/url_frontier.h"
#include <iostream>
#include <vector>

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

// Test 1: Basic add and retrieve
void test_basic_add(TestResults& results) {
    std::cout << "\n--- Test: Basic Add ---\n";
    
    URLFrontier frontier;
    
    bool added = frontier.add("http://example.com/page1");
    print_test(added == true, "First URL added successfully", results);
    print_test(frontier.pending_count() == 1, "Pending count is 1", results);
    print_test(!frontier.empty(), "Frontier not empty", results);
}

// Test 2: Priority ordering (different domains to avoid rate limiting)
void test_priority_ordering(TestResults& results) {
    std::cout << "\n--- Test: Priority Ordering ---\n";
    
    URLFrontier frontier;
    
    // Add in reverse priority order - use different domains
    frontier.add("http://low.com/low", URLFrontier::Priority::LOW);
    frontier.add("http://normal.com/normal", URLFrontier::Priority::NORMAL);
    frontier.add("http://high.com/high", URLFrontier::Priority::HIGH);
    frontier.add("http://seed.com/seed", URLFrontier::Priority::SEED);
    
    print_test(frontier.pending_count() == 4, "All 4 URLs added", results);
    
    // Should come out in priority order: SEED, HIGH, NORMAL, LOW
    auto url1 = frontier.get_next();
    print_test(url1.has_value() && url1->find("/seed") != std::string::npos, 
               "First out is SEED priority", results);
    
    auto url2 = frontier.get_next();
    print_test(url2.has_value() && url2->find("/high") != std::string::npos, 
               "Second out is HIGH priority", results);
    
    auto url3 = frontier.get_next();
    print_test(url3.has_value() && url3->find("/normal") != std::string::npos, 
               "Third out is NORMAL priority", results);
    
    auto url4 = frontier.get_next();
    print_test(url4.has_value() && url4->find("/low") != std::string::npos, 
               "Fourth out is LOW priority", results);
    
    print_test(frontier.empty(), "Frontier empty after all retrieved", results);
}

// Test 3: URL normalization on add
void test_normalization_on_add(TestResults& results) {
    std::cout << "\n--- Test: Normalization on Add ---\n";
    
    URLFrontier frontier;
    
    // These should all normalize to the same URL
    frontier.add("HTTP://EXAMPLE.COM/page");
    bool dup1 = frontier.add("http://example.com:80/page");
    bool dup2 = frontier.add("http://example.com/page#section");
    
    print_test(frontier.pending_count() == 1, "Duplicates detected via normalization", results);
    print_test(dup1 == false, "Port-variant duplicate rejected", results);
    print_test(dup2 == false, "Fragment-variant duplicate rejected", results);
}

// Test 4: Batch add
void test_batch_add(TestResults& results) {
    std::cout << "\n--- Test: Batch Add ---\n";
    
    URLFrontier frontier;
    
    std::vector<std::string> urls = {
        "http://example.com/page1",
        "http://example.com/page2",
        "http://example.com/page3",
        "http://example.com/page1",  // Duplicate
        "http://other.com/page1"
    };
    
    size_t added = frontier.add_batch(urls, URLFrontier::Priority::NORMAL);
    
    print_test(added == 4, "Batch added 4 unique URLs (1 duplicate rejected)", results);
    print_test(frontier.pending_count() == 4, "Pending count is 4", results);
}

// Test 5: Invalid URLs rejected
void test_invalid_urls(TestResults& results) {
    std::cout << "\n--- Test: Invalid URLs Rejected ---\n";
    
    URLFrontier frontier;
    
    bool added1 = frontier.add("not-a-valid-url");
    bool added2 = frontier.add("");
    bool added3 = frontier.add("ftp://example.com/file");
    bool added4 = frontier.add("javascript:alert(1)");
    
    print_test(added1 == false, "Garbage URL rejected", results);
    print_test(added2 == false, "Empty URL rejected", results);
    print_test(added3 == false, "FTP URL rejected", results);
    print_test(added4 == false, "Javascript URL rejected", results);
    print_test(frontier.empty(), "Frontier still empty", results);
}

// Test 6: Same priority from different domains
void test_same_priority(TestResults& results) {
    std::cout << "\n--- Test: Same Priority URLs ---\n";
    
    URLFrontier frontier;
    
    // Use different domains to avoid rate limiting
    frontier.add("http://first.com/first", URLFrontier::Priority::NORMAL);
    frontier.add("http://second.com/second", URLFrontier::Priority::NORMAL);
    frontier.add("http://third.com/third", URLFrontier::Priority::NORMAL);
    
    // Just verify all 3 come out
    std::vector<std::string> retrieved;
    while (auto url = frontier.get_next()) {
        retrieved.push_back(*url);
    }
    
    print_test(retrieved.size() == 3, "All 3 same-priority URLs retrieved", results);
}

// Test 7: Domain extraction stored
void test_domain_stored(TestResults& results) {
    std::cout << "\n--- Test: Domain Extracted ---\n";
    
    URLFrontier frontier;
    
    frontier.add("http://example.com/page1");
    frontier.add("http://other.com/page1");
    frontier.add("http://example.com/page2");
    
    print_test(frontier.pending_count() == 3, "3 URLs from 2 domains added", results);
}

// Test 8: get_next on empty returns nullopt
void test_empty_get_next(TestResults& results) {
    std::cout << "\n--- Test: Empty Queue Returns nullopt ---\n";
    
    URLFrontier frontier;
    
    auto url = frontier.get_next();
    print_test(!url.has_value(), "get_next on empty returns nullopt", results);
    
    frontier.add("http://example.com/page");
    frontier.get_next();  // Remove it
    
    url = frontier.get_next();
    print_test(!url.has_value(), "get_next after drain returns nullopt", results);
}

// Test 9: seen_count tracks all seen URLs
void test_seen_count(TestResults& results) {
    std::cout << "\n--- Test: Seen Count ---\n";
    
    URLFrontier frontier;
    
    frontier.add("http://example.com/page1");
    frontier.add("http://example.com/page2");
    frontier.add("http://example.com/page1");  // Duplicate
    
    print_test(frontier.seen_count() == 2, "Seen count is 2 (unique URLs)", results);
    
    // After retrieval, seen count should remain (for dedup)
    frontier.get_next();
    print_test(frontier.seen_count() == 2, "Seen count unchanged after retrieval", results);
}

// Test 10: Large batch with unique domains
void test_large_batch(TestResults& results) {
    std::cout << "\n--- Test: Large Batch ---\n";
    
    URLFrontier frontier;
    
    std::vector<std::string> urls;
    for (int i = 0; i < 1000; ++i) {
        urls.push_back("http://example" + std::to_string(i) + ".com/page");
    }
    
    size_t added = frontier.add_batch(urls);
    
    print_test(added == 1000, "Added 1000 URLs", results);
    print_test(frontier.pending_count() == 1000, "Pending count is 1000", results);
}

int main() {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "    URL Frontier Test Suite - Subtask 1.2.3                 \n";
    std::cout << "    Priority Queue Structure                                 \n";
    std::cout << "============================================================\n";
    
    TestResults results;
    
    test_basic_add(results);
    test_priority_ordering(results);
    test_normalization_on_add(results);
    test_batch_add(results);
    test_invalid_urls(results);
    test_same_priority(results);
    test_domain_stored(results);
    test_empty_get_next(results);
    test_seen_count(results);
    test_large_batch(results);
    
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "                    TEST SUMMARY                            \n";
    std::cout << "============================================================\n";
    std::cout << "Passed: " << results.passed << "\n";
    std::cout << "Failed: " << results.failed << "\n";
    std::cout << "\n";
    
    if (results.failed == 0) {
        std::cout << "[SUCCESS] All tests passed - Subtask 1.2.3 complete!\n\n";
        return 0;
    } else {
        std::cout << "[INCOMPLETE] Some tests failed\n\n";
        return 1;
    }
}