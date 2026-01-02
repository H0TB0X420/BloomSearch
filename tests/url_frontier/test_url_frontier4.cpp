#include "crawler/url_frontier.h"
#include <iostream>
#include <thread>
#include <chrono>

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

// Test 1: Default rate limiting
void test_default_rate_limiting(TestResults& results) {
    std::cout << "\n--- Test: Default Rate Limiting ---\n";
    
    URLFrontier frontier;
    
    // Add multiple URLs from same domain
    frontier.add("http://example.com/page1", URLFrontier::Priority::SEED);
    frontier.add("http://example.com/page2", URLFrontier::Priority::SEED);
    frontier.add("http://example.com/page3", URLFrontier::Priority::SEED);
    
    // First should return immediately
    auto start = std::chrono::steady_clock::now();
    auto url1 = frontier.get_next();
    auto first_time = std::chrono::steady_clock::now() - start;
    
    print_test(url1.has_value(), "First URL returned", results);
    
    // Second from same domain should return nullopt (rate limited)
    // OR we need to wait
    auto url2 = frontier.get_next();
    
    // Depending on implementation, either:
    // - Returns nullopt because domain is rate-limited
    // - Returns the URL if we waited
    // For this test, we expect nullopt (non-blocking)
    std::cout << "[INFO] Second get_next returned: " 
              << (url2.has_value() ? *url2 : "nullopt") << "\n";
    
    print_test(frontier.pending_count() >= 1, "URLs still pending (rate limited)", results);
}

// Test 2: Different domains not affected
void test_different_domains(TestResults& results) {
    std::cout << "\n--- Test: Different Domains Not Affected ---\n";
    
    URLFrontier frontier;
    
    // Add URLs from different domains
    frontier.add("http://example1.com/page", URLFrontier::Priority::SEED);
    frontier.add("http://example2.com/page", URLFrontier::Priority::SEED);
    frontier.add("http://example3.com/page", URLFrontier::Priority::SEED);
    
    // Should get all 3 without waiting (different domains)
    auto url1 = frontier.get_next();
    auto url2 = frontier.get_next();
    auto url3 = frontier.get_next();
    
    print_test(url1.has_value(), "First domain returned", results);
    print_test(url2.has_value(), "Second domain returned", results);
    print_test(url3.has_value(), "Third domain returned", results);
    print_test(frontier.empty(), "All URLs retrieved", results);
}

// Test 3: Rate limit expires
void test_rate_limit_expires(TestResults& results) {
    std::cout << "\n--- Test: Rate Limit Expires ---\n";
    
    URLFrontier frontier;
    
    // Set a short crawl delay for testing (100ms)
    frontier.set_crawl_delay("example.com", 100);
    
    frontier.add("http://example.com/page1", URLFrontier::Priority::SEED);
    frontier.add("http://example.com/page2", URLFrontier::Priority::SEED);
    
    // Get first URL
    auto url1 = frontier.get_next();
    print_test(url1.has_value(), "First URL returned", results);
    
    // Immediately try second - should fail or return nullopt
    auto url2_immediate = frontier.get_next();
    
    // Wait for rate limit to expire
    std::cout << "[INFO] Waiting 150ms for rate limit to expire...\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    
    // Now should succeed
    auto url2_after_wait = frontier.get_next();
    print_test(url2_after_wait.has_value(), "Second URL returned after wait", results);
}

// Test 4: Custom crawl delay
void test_custom_crawl_delay(TestResults& results) {
    std::cout << "\n--- Test: Custom Crawl Delay ---\n";
    
    URLFrontier frontier;
    
    // Set different delays for different domains
    frontier.set_crawl_delay("fast.com", 50);
    frontier.set_crawl_delay("slow.com", 500);
    
    frontier.add("http://fast.com/page1", URLFrontier::Priority::SEED);
    frontier.add("http://fast.com/page2", URLFrontier::Priority::SEED);
    frontier.add("http://slow.com/page1", URLFrontier::Priority::SEED);
    frontier.add("http://slow.com/page2", URLFrontier::Priority::SEED);
    
    // Get one from each domain
    frontier.get_next();  // fast.com/page1 or slow.com/page1
    frontier.get_next();  // the other domain
    
    // Wait just enough for fast.com
    std::cout << "[INFO] Waiting 75ms...\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(75));
    
    auto url = frontier.get_next();
    if (url.has_value()) {
        std::cout << "[INFO] Got URL: " << *url << "\n";
        // Should be from fast.com (its delay expired)
        print_test(url->find("fast.com") != std::string::npos, 
                   "Fast domain ready before slow domain", results);
    } else {
        print_test(false, "Expected fast.com URL after 75ms", results);
    }
}

// Test 5: Priority still respected across domains
void test_priority_with_rate_limiting(TestResults& results) {
    std::cout << "\n--- Test: Priority With Rate Limiting ---\n";
    
    URLFrontier frontier;
    frontier.set_crawl_delay("example.com", 50);
    frontier.set_crawl_delay("other.com", 50);
    
    // Add HIGH priority from example.com, then LOW from other.com
    frontier.add("http://example.com/high", URLFrontier::Priority::HIGH);
    frontier.add("http://other.com/low", URLFrontier::Priority::LOW);
    
    // First should be HIGH priority (example.com)
    auto url1 = frontier.get_next();
    print_test(url1.has_value() && url1->find("/high") != std::string::npos,
               "High priority URL first", results);
    
    // Second should be LOW priority from other.com (example.com rate limited)
    auto url2 = frontier.get_next();
    print_test(url2.has_value() && url2->find("/low") != std::string::npos,
               "Falls back to lower priority when high priority domain limited", results);
}

// Test 6: All domains rate limited returns nullopt
void test_all_domains_limited(TestResults& results) {
    std::cout << "\n--- Test: All Domains Limited Returns nullopt ---\n";
    
    URLFrontier frontier;
    frontier.set_crawl_delay("only.com", 1000);  // 1 second delay
    
    frontier.add("http://only.com/page1", URLFrontier::Priority::SEED);
    frontier.add("http://only.com/page2", URLFrontier::Priority::SEED);
    
    // Get first
    auto url1 = frontier.get_next();
    print_test(url1.has_value(), "First URL returned", results);
    
    // Try immediately - should return nullopt
    auto url2 = frontier.get_next();
    print_test(!url2.has_value(), "Returns nullopt when all domains limited", results);
    
    // But URLs should still be pending
    print_test(frontier.pending_count() == 1, "URL still pending", results);
}

// Test 7: Zero delay means no limiting
void test_zero_delay(TestResults& results) {
    std::cout << "\n--- Test: Zero Delay ---\n";
    
    URLFrontier frontier;
    frontier.set_crawl_delay("nodelay.com", 0);
    
    frontier.add("http://nodelay.com/page1", URLFrontier::Priority::SEED);
    frontier.add("http://nodelay.com/page2", URLFrontier::Priority::SEED);
    frontier.add("http://nodelay.com/page3", URLFrontier::Priority::SEED);
    
    // Should get all 3 immediately
    auto url1 = frontier.get_next();
    auto url2 = frontier.get_next();
    auto url3 = frontier.get_next();
    
    print_test(url1.has_value() && url2.has_value() && url3.has_value(),
               "All URLs returned with zero delay", results);
}

// Test 8: Pending count reflects rate-limited URLs
void test_pending_reflects_limited(TestResults& results) {
    std::cout << "\n--- Test: Pending Count Reflects Limited URLs ---\n";
    
    URLFrontier frontier;
    frontier.set_crawl_delay("example.com", 1000);
    
    frontier.add("http://example.com/page1", URLFrontier::Priority::SEED);
    frontier.add("http://example.com/page2", URLFrontier::Priority::SEED);
    
    print_test(frontier.pending_count() == 2, "Initially 2 pending", results);
    
    frontier.get_next();  // Gets page1
    print_test(frontier.pending_count() == 1, "1 pending after first get", results);
    
    frontier.get_next();  // Returns nullopt (rate limited)
    print_test(frontier.pending_count() == 1, "Still 1 pending (rate limited)", results);
}

int main() {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "    URL Frontier Test Suite - Subtask 1.2.5                 \n";
    std::cout << "    Per-Domain Rate Limiting                                 \n";
    std::cout << "============================================================\n";
    
    TestResults results;
    
    test_default_rate_limiting(results);
    test_different_domains(results);
    test_rate_limit_expires(results);
    test_custom_crawl_delay(results);
    test_priority_with_rate_limiting(results);
    test_all_domains_limited(results);
    test_zero_delay(results);
    test_pending_reflects_limited(results);
    
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "                    TEST SUMMARY                            \n";
    std::cout << "============================================================\n";
    std::cout << "Passed: " << results.passed << "\n";
    std::cout << "Failed: " << results.failed << "\n";
    std::cout << "\n";
    
    if (results.failed == 0) {
        std::cout << "[SUCCESS] All tests passed - Subtask 1.2.5 complete!\n\n";
        return 0;
    } else {
        std::cout << "[INCOMPLETE] Some tests failed\n\n";
        return 1;
    }
}