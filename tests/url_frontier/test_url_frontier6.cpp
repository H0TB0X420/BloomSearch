#include "crawler/url_frontier.h"
#include <iostream>
#include <fstream>
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

//=============================================================================
// Integration Test 1: Full crawl simulation
//=============================================================================
void test_crawl_simulation(TestResults& results) {
    std::cout << "\n--- Integration: Crawl Simulation ---\n";
    
    URLFrontier frontier;
    
    // Set fast delays for testing
    frontier.set_crawl_delay("site1.com", 0);
    frontier.set_crawl_delay("site2.com", 0);
    frontier.set_crawl_delay("site3.com", 0);
    
    // Load seeds
    std::ofstream seeds("/tmp/crawl_seeds.txt");
    seeds << "http://site1.com/\n";
    seeds << "http://site2.com/\n";
    seeds << "http://site3.com/\n";
    seeds.close();
    
    bool loaded = frontier.load_seeds("/tmp/crawl_seeds.txt");
    print_test(loaded, "Seeds loaded", results);
    print_test(frontier.pending_count() == 3, "3 seed URLs pending", results);
    
    // Simulate crawling and discovering new links
    int crawled = 0;
    while (auto url = frontier.get_next()) {
        crawled++;
        
        // Simulate discovering links on the page
        if (url->find("site1.com") != std::string::npos && crawled == 1) {
            frontier.add("http://site1.com/page1", URLFrontier::Priority::NORMAL);
            frontier.add("http://site1.com/page2", URLFrontier::Priority::NORMAL);
            frontier.add("http://external.com/link", URLFrontier::Priority::LOW);
        }
        
        if (crawled > 10) break;  // Safety limit
    }
    
    print_test(crawled >= 3, "Crawled at least 3 URLs", results);
    std::cout << "[INFO] Total crawled: " << crawled << "\n";
}

//=============================================================================
// Integration Test 2: Priority + Rate Limiting together
//=============================================================================
void test_priority_with_rate_limits(TestResults& results) {
    std::cout << "\n--- Integration: Priority + Rate Limiting ---\n";
    
    URLFrontier frontier;
    
    // Fast domain and slow domain
    frontier.set_crawl_delay("fast.com", 0);
    frontier.set_crawl_delay("slow.com", 500);
    
    // Add high-priority URLs from slow domain
    frontier.add("http://slow.com/important1", URLFrontier::Priority::SEED);
    frontier.add("http://slow.com/important2", URLFrontier::Priority::SEED);
    
    // Add low-priority from fast domain
    frontier.add("http://fast.com/page1", URLFrontier::Priority::LOW);
    frontier.add("http://fast.com/page2", URLFrontier::Priority::LOW);
    
    // First should be slow.com (highest priority)
    auto url1 = frontier.get_next();
    print_test(url1.has_value() && url1->find("slow.com") != std::string::npos,
               "First is high-priority slow.com", results);
    
    // Second: slow.com is rate-limited, so falls back to fast.com
    auto url2 = frontier.get_next();
    print_test(url2.has_value() && url2->find("fast.com") != std::string::npos,
               "Falls back to fast.com when slow.com limited", results);
    
    // Third: fast.com still available
    auto url3 = frontier.get_next();
    print_test(url3.has_value() && url3->find("fast.com") != std::string::npos,
               "Continues with fast.com", results);
    
    // Fourth: only slow.com left, but rate limited
    auto url4 = frontier.get_next();
    print_test(!url4.has_value(), "Returns nullopt when only limited domain left", results);
    
    // Still have 1 pending
    print_test(frontier.pending_count() == 1, "1 URL still pending (rate limited)", results);
}

//=============================================================================
// Integration Test 3: Deduplication across sources
//=============================================================================
void test_dedup_across_sources(TestResults& results) {
    std::cout << "\n--- Integration: Deduplication Across Sources ---\n";
    
    URLFrontier frontier;
    
    // Add via direct add
    frontier.add("http://example.com/page1");
    
    // Try to add same URL via batch
    std::vector<std::string> batch = {
        "http://example.com/page1",  // Duplicate
        "http://example.com/page2",
        "HTTP://EXAMPLE.COM/page1",  // Duplicate (normalized)
    };
    size_t added = frontier.add_batch(batch);
    print_test(added == 1, "Batch only added 1 new URL", results);
    
    // Try via seeds file
    std::ofstream seeds("/tmp/dedup_seeds.txt");
    seeds << "http://example.com/page1\n";  // Duplicate
    seeds << "http://example.com/page3\n";
    seeds.close();
    
    frontier.load_seeds("/tmp/dedup_seeds.txt");
    
    print_test(frontier.seen_count() == 3, "Only 3 unique URLs seen", results);
}

//=============================================================================
// Integration Test 4: URL normalization consistency
//=============================================================================
void test_normalization_consistency(TestResults& results) {
    std::cout << "\n--- Integration: Normalization Consistency ---\n";
    
    URLFrontier frontier;
    frontier.set_crawl_delay("example.com", 0);
    
    // All these should be the same URL
    frontier.add("http://example.com/path");
    frontier.add("HTTP://EXAMPLE.COM/path");
    frontier.add("http://example.com:80/path");
    frontier.add("http://example.com/path#section");
    frontier.add("http://example.com/path?");
    
    print_test(frontier.pending_count() == 1, "All variants deduplicated to 1", results);
    
    // The returned URL should be normalized
    auto url = frontier.get_next();
    print_test(url.has_value(), "Got URL back", results);
    print_test(*url == "http://example.com/path", "URL is normalized form", results);
}

//=============================================================================
// Integration Test 5: Empty frontier behavior
//=============================================================================
void test_empty_frontier(TestResults& results) {
    std::cout << "\n--- Integration: Empty Frontier Behavior ---\n";
    
    URLFrontier frontier;
    
    print_test(frontier.empty(), "New frontier is empty", results);
    print_test(frontier.pending_count() == 0, "Pending count is 0", results);
    print_test(frontier.seen_count() == 0, "Seen count is 0", results);
    print_test(!frontier.get_next().has_value(), "get_next returns nullopt", results);
    
    // Load empty file
    std::ofstream empty("/tmp/empty_seeds.txt");
    empty.close();
    
    bool loaded = frontier.load_seeds("/tmp/empty_seeds.txt");
    print_test(loaded, "Loading empty file succeeds", results);
    print_test(frontier.empty(), "Still empty after loading empty file", results);
}

//=============================================================================
// Integration Test 6: Rate limit recovery
//=============================================================================
void test_rate_limit_recovery(TestResults& results) {
    std::cout << "\n--- Integration: Rate Limit Recovery ---\n";
    
    URLFrontier frontier;
    frontier.set_crawl_delay("example.com", 100);  // 100ms delay
    
    frontier.add("http://example.com/page1");
    frontier.add("http://example.com/page2");
    
    // Get first
    auto url1 = frontier.get_next();
    print_test(url1.has_value(), "First URL retrieved", results);
    
    // Immediately try second - should fail
    auto url2 = frontier.get_next();
    print_test(!url2.has_value(), "Second blocked by rate limit", results);
    
    // Wait for rate limit
    std::cout << "[INFO] Waiting 150ms for rate limit...\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    
    // Now should work
    auto url3 = frontier.get_next();
    print_test(url3.has_value(), "Second URL retrieved after waiting", results);
    
    print_test(frontier.empty(), "Frontier now empty", results);
}

//=============================================================================
// Integration Test 7: Many domains performance
//=============================================================================
void test_many_domains(TestResults& results) {
    std::cout << "\n--- Integration: Many Domains Performance ---\n";
    
    URLFrontier frontier;
    
    // Add 100 URLs from 100 different domains
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 100; ++i) {
        frontier.add("http://domain" + std::to_string(i) + ".com/page");
    }
    auto add_time = std::chrono::steady_clock::now() - start;
    
    print_test(frontier.pending_count() == 100, "Added 100 URLs", results);
    
    // Retrieve all
    start = std::chrono::steady_clock::now();
    int retrieved = 0;
    while (frontier.get_next()) {
        retrieved++;
    }
    auto get_time = std::chrono::steady_clock::now() - start;
    
    print_test(retrieved == 100, "Retrieved all 100 URLs", results);
    
    auto add_ms = std::chrono::duration_cast<std::chrono::milliseconds>(add_time).count();
    auto get_ms = std::chrono::duration_cast<std::chrono::milliseconds>(get_time).count();
    
    std::cout << "[INFO] Add 100 URLs: " << add_ms << "ms\n";
    std::cout << "[INFO] Get 100 URLs: " << get_ms << "ms\n";
    
    print_test(add_ms < 1000, "Adding 100 URLs < 1 second", results);
    print_test(get_ms < 1000, "Getting 100 URLs < 1 second", results);
}

//=============================================================================
// Integration Test 8: Mixed priority batch
//=============================================================================
void test_mixed_priority_batch(TestResults& results) {
    std::cout << "\n--- Integration: Mixed Priority Operations ---\n";
    
    URLFrontier frontier;
    
    // Add different priorities
    frontier.add("http://seed1.com/", URLFrontier::Priority::SEED);
    frontier.add("http://seed2.com/", URLFrontier::Priority::SEED);
    
    frontier.add_batch({
        "http://normal1.com/",
        "http://normal2.com/"
    }, URLFrontier::Priority::NORMAL);
    
    frontier.add("http://low1.com/", URLFrontier::Priority::LOW);
    
    print_test(frontier.pending_count() == 5, "5 URLs added with mixed priorities", results);
    
    // Verify order
    auto url1 = frontier.get_next();
    auto url2 = frontier.get_next();
    
    bool seeds_first = url1->find("seed") != std::string::npos && 
                       url2->find("seed") != std::string::npos;
    print_test(seeds_first, "SEED priority URLs come first", results);
}

int main() {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "    URL Frontier - Final Integration Tests                  \n";
    std::cout << "    Subtask 1.2.7                                           \n";
    std::cout << "============================================================\n";
    
    TestResults results;
    
    test_crawl_simulation(results);
    test_priority_with_rate_limits(results);
    test_dedup_across_sources(results);
    test_normalization_consistency(results);
    test_empty_frontier(results);
    test_rate_limit_recovery(results);
    test_many_domains(results);
    test_mixed_priority_batch(results);
    
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "                    TEST SUMMARY                            \n";
    std::cout << "============================================================\n";
    std::cout << "Passed: " << results.passed << "\n";
    std::cout << "Failed: " << results.failed << "\n";
    std::cout << "\n";
    
    if (results.failed == 0) {
        std::cout << "============================================================\n";
        std::cout << "  [SUCCESS] URL Frontier Complete!                          \n";
        std::cout << "  Task 1.2 DONE - All 7 subtasks passed                     \n";
        std::cout << "============================================================\n\n";
        return 0;
    } else {
        std::cout << "[INCOMPLETE] Some integration tests failed\n\n";
        return 1;
    }
}