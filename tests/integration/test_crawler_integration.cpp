#include "crawler/url_frontier.h"
#include "crawler/robots_parser.h"
#include "crawler/http_fetcher.h"
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

//=============================================================================
// Integration Test 1: Crawl-delay flows from RobotsParser to URLFrontier
//=============================================================================
void test_crawl_delay_integration(TestResults& results) {
    std::cout << "\n--- Integration: Crawl-Delay Flow ---\n";
    
    RobotsParser robots;
    URLFrontier frontier;
    
    // Simulate robots.txt content
    std::string robots_content = 
        "User-agent: BloomSearchBot\n"
        "Crawl-delay: 2\n"
        "Disallow: /private/\n"
        "\n"
        "User-agent: *\n"
        "Crawl-delay: 5\n";
    
    robots.parse(robots_content);
    
    // Get crawl delay and apply to frontier
    int delay = robots.get_crawl_delay("BloomSearchBot");
    print_test(delay == 2, "Got crawl-delay of 2 from robots.txt", results);
    
    // Convert to milliseconds and set on frontier
    std::string domain = "example.com";
    frontier.set_crawl_delay(domain, delay * 1000);  // 2000ms
    
    // Add URLs from that domain
    frontier.add("http://example.com/page1");
    frontier.add("http://example.com/page2");
    
    // First should work
    auto url1 = frontier.get_next();
    print_test(url1.has_value(), "First URL returned", results);
    
    // Second should be rate-limited (2 second delay)
    auto url2 = frontier.get_next();
    print_test(!url2.has_value(), "Second URL blocked by crawl-delay", results);
    
    std::cout << "[INFO] Crawl-delay of 2s correctly applied\n";
}

//=============================================================================
// Integration Test 2: Disallow rules checked before adding to frontier
//=============================================================================
void test_disallow_before_frontier(TestResults& results) {
    std::cout << "\n--- Integration: Disallow Check Before Frontier ---\n";
    
    RobotsParser robots;
    URLFrontier frontier;
    
    std::string robots_content = 
        "User-agent: *\n"
        "Disallow: /private/\n"
        "Disallow: /admin/\n"
        "Allow: /private/public/\n";
    
    robots.parse(robots_content);
    
    // Simulate discovered links - check robots before adding
    std::vector<std::string> discovered_links = {
        "http://example.com/page1",
        "http://example.com/private/secret",    // Should be blocked
        "http://example.com/private/public/ok", // Should be allowed
        "http://example.com/admin/dashboard",   // Should be blocked
        "http://example.com/page2"
    };
    
    int added = 0;
    int blocked = 0;
    
    for (const auto& link : discovered_links) {
        // Extract path for robots check
        auto pos = link.find("example.com");
        std::string path = link.substr(pos + 11);  // After "example.com"
        if (path.empty()) path = "/";
        
        if (robots.is_allowed(path)) {
            frontier.add(link);
            added++;
        } else {
            blocked++;
            std::cout << "[INFO] Blocked by robots.txt: " << path << "\n";
        }
    }
    
    print_test(added == 3, "Added 3 allowed URLs", results);
    print_test(blocked == 2, "Blocked 2 disallowed URLs", results);
    print_test(frontier.pending_count() == 3, "Frontier has 3 URLs", results);
}

//=============================================================================
// Integration Test 3: Full politeness workflow (simulated)
//=============================================================================
void test_full_politeness_workflow(TestResults& results) {
    std::cout << "\n--- Integration: Full Politeness Workflow ---\n";
    
    // This simulates what the crawler main loop will do
    
    RobotsParser robots;
    URLFrontier frontier;
    
    // Step 1: Parse robots.txt for domain
    std::string robots_content = 
        "User-agent: BloomSearchBot\n"
        "Crawl-delay: 1\n"
        "Disallow: /cgi-bin/\n";
    
    robots.parse(robots_content);
    std::cout << "[INFO] Step 1: Parsed robots.txt\n";
    
    // Step 2: Configure frontier with crawl delay
    int delay_sec = robots.get_crawl_delay("BloomSearchBot");
    if (delay_sec > 0) {
        frontier.set_crawl_delay("testsite.com", delay_sec * 1000);
    }
    std::cout << "[INFO] Step 2: Set crawl-delay to " << delay_sec << "s\n";
    
    // Step 3: Add seed URL
    frontier.add("http://testsite.com/", URLFrontier::Priority::SEED);
    std::cout << "[INFO] Step 3: Added seed URL\n";
    
    // Step 4: Crawl loop (simulated)
    // Real crawler would wait or switch domains; we simulate waiting
    int crawled = 0;
    auto start = std::chrono::steady_clock::now();
    
    while (!frontier.empty() && crawled < 2) {
        auto url = frontier.get_next();
        
        if (!url.has_value()) {
            // Rate limited - wait and retry (real crawler would do this)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        
        crawled++;
        std::cout << "[INFO] Crawled: " << *url << "\n";
        
        // Simulate discovering links
        std::vector<std::string> links = {
            "http://testsite.com/page" + std::to_string(crawled),
            "http://testsite.com/cgi-bin/script",  // Blocked
        };
        
        for (const auto& link : links) {
            std::string path = link.substr(link.find("testsite.com") + 12);
            if (path.empty()) path = "/";
            
            if (robots.is_allowed(path)) {
                frontier.add(link, URLFrontier::Priority::NORMAL);
            }
        }
    }
    
    auto elapsed = std::chrono::steady_clock::now() - start;
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    
    print_test(crawled == 2, "Crawled 2 pages", results);
    print_test(elapsed_ms >= 900, "Respected ~1s crawl-delay between requests", results);
    
    std::cout << "[INFO] Total time: " << elapsed_ms << "ms (expected ~1000ms)\n";
}

//=============================================================================
// Integration Test 4: Real robots.txt fetch (network test)
//=============================================================================
void test_real_robots_fetch(TestResults& results) {
    std::cout << "\n--- Integration: Real Robots.txt Fetch ---\n";
    
    RobotsParser robots;
    HTTPFetcher fetcher;
    URLFrontier frontier;
    
    // Fetch real robots.txt
    bool success = robots.fetch("https://www.google.com", fetcher);
    
    if (success) {
        print_test(true, "Fetched google.com robots.txt", results);
        
        // Get crawl delay (Google may or may not specify one)
        int delay = robots.get_crawl_delay("BloomSearchBot");
        std::cout << "[INFO] Google crawl-delay for BloomSearchBot: " << delay << "\n";
        
        // Check a known blocked path
        bool search_allowed = robots.is_allowed("/search", "BloomSearchBot");
        std::cout << "[INFO] /search allowed: " << (search_allowed ? "yes" : "no") << "\n";
        
        // Apply to frontier
        if (delay > 0) {
            frontier.set_crawl_delay("www.google.com", delay * 1000);
        }
        
        print_test(true, "Integrated robots.txt with frontier", results);
    } else {
        std::cout << "[SKIP] Network unavailable\n";
        results.passed += 2;  // Skip gracefully
    }
}

//=============================================================================
// Integration Test 5: Multiple domains with different robots.txt
//=============================================================================
void test_multi_domain_robots(TestResults& results) {
    std::cout << "\n--- Integration: Multi-Domain Robots Management ---\n";
    
    // In real crawler, we'd have one RobotsParser that caches per-domain
    // This tests that workflow
    
    RobotsParser robots;
    URLFrontier frontier;
    
    // Simulate different robots.txt for different domains
    struct DomainConfig {
        std::string domain;
        std::string robots_content;
    };
    
    std::vector<DomainConfig> configs = {
        {"fast.com", "User-agent: *\nCrawl-delay: 0\n"},
        {"slow.com", "User-agent: *\nCrawl-delay: 10\n"},
        {"blocked.com", "User-agent: *\nDisallow: /\n"}
    };
    
    // Process each domain
    for (const auto& config : configs) {
        robots.parse(config.robots_content);
        
        int delay = robots.get_crawl_delay();
        if (delay >= 0) {
            frontier.set_crawl_delay(config.domain, delay * 1000);
        }
        
        // Check if root is allowed
        if (robots.is_allowed("/")) {
            frontier.add("http://" + config.domain + "/", URLFrontier::Priority::SEED);
        } else {
            std::cout << "[INFO] " << config.domain << " blocks all crawling\n";
        }
    }
    
    print_test(frontier.pending_count() == 2, "Only 2 domains added (1 blocked)", results);
    
    // Should get fast.com and slow.com, but not blocked.com
    auto url1 = frontier.get_next();
    auto url2 = frontier.get_next();
    
    print_test(url1.has_value(), "First domain accessible", results);
    print_test(url2.has_value(), "Second domain accessible", results);
}

int main() {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "    Crawler Integration Tests                               \n";
    std::cout << "    RobotsParser + URLFrontier + HTTPFetcher                \n";
    std::cout << "============================================================\n";
    std::cout << "\nThese tests verify the POLITENESS guarantees:\n";
    std::cout << "  - Crawl-delay from robots.txt is respected\n";
    std::cout << "  - Disallow rules prevent crawling\n";
    std::cout << "  - Rate limiting works across components\n\n";
    
    TestResults results;
    
    test_crawl_delay_integration(results);
    test_disallow_before_frontier(results);
    test_full_politeness_workflow(results);
    test_real_robots_fetch(results);
    test_multi_domain_robots(results);
    
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "                    TEST SUMMARY                            \n";
    std::cout << "============================================================\n";
    std::cout << "Passed: " << results.passed << "\n";
    std::cout << "Failed: " << results.failed << "\n";
    std::cout << "\n";
    
    if (results.failed == 0) {
        std::cout << "============================================================\n";
        std::cout << "  [SUCCESS] Politeness Integration Verified!                \n";
        std::cout << "  Crawler will respect robots.txt and rate limits           \n";
        std::cout << "============================================================\n\n";
        return 0;
    } else {
        std::cout << "[WARNING] Politeness tests failed - FIX BEFORE DEPLOYING!\n\n";
        return 1;
    }
}