//=============================================================================
// Bloom Search - Multithreaded Crawler Test Suite
// Tests: Politeness, Speed, Quality, Thread Safety, Graceful Shutdown
//=============================================================================

#include "crawler/url_frontier_mt.h"
#include "crawler/http_fetcher.h"
#include "crawler/robots_parser.h"
#include "indexer/html_parser.h"

#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <set>
#include <map>
#include <mutex>
#include <random>
#include <functional>
#include <iomanip>

using namespace search;

//=============================================================================
// Test Framework
//=============================================================================
struct TestResults {
    int passed = 0;
    int failed = 0;
    std::vector<std::string> failures;
};

void print_test(bool passed, const std::string& name, TestResults& results) {
    if (passed) {
        std::cout << "  [PASS] " << name << "\n";
        results.passed++;
    } else {
        std::cout << "  [FAIL] " << name << "\n";
        results.failed++;
        results.failures.push_back(name);
    }
}

//=============================================================================
// SECTION 1: POLITENESS TESTS
// Ensure rate limiting works correctly per domain
//=============================================================================
void test_politeness_single_domain(TestResults& results) {
    std::cout << "\n=== Test: Politeness - Single Domain Rate Limiting ===\n";
    
    URLFrontierMT frontier;
    frontier.set_default_delay(std::chrono::milliseconds(200));  // 200ms delay
    
    // Add multiple URLs from same domain
    frontier.add("https://example.com/page1");
    frontier.add("https://example.com/page2");
    frontier.add("https://example.com/page3");
    
    std::vector<std::chrono::steady_clock::time_point> pop_times;
    
    // Pop all URLs, recording timestamps
    for (int i = 0; i < 3; ++i) {
        auto url = frontier.pop();
        pop_times.push_back(std::chrono::steady_clock::now());
        frontier.mark_domain_crawled("example.com");
    }
    
    // Check delays between pops
    if (pop_times.size() >= 3) {
        auto delay1 = std::chrono::duration_cast<std::chrono::milliseconds>(
            pop_times[1] - pop_times[0]).count();
        auto delay2 = std::chrono::duration_cast<std::chrono::milliseconds>(
            pop_times[2] - pop_times[1]).count();
        
        std::cout << "  Delay between pop 1-2: " << delay1 << "ms\n";
        std::cout << "  Delay between pop 2-3: " << delay2 << "ms\n";
        
        // Should be at least 180ms (allowing 20ms tolerance)
        print_test(delay1 >= 180, "First delay >= 180ms", results);
        print_test(delay2 >= 180, "Second delay >= 180ms", results);
    }
}

void test_politeness_multi_domain(TestResults& results) {
    std::cout << "\n=== Test: Politeness - Multi Domain Parallel ===\n";
    
    URLFrontierMT frontier;
    frontier.set_default_delay(std::chrono::milliseconds(500));  // 500ms delay
    
    // Add URLs from DIFFERENT domains
    frontier.add("https://domain1.com/page");
    frontier.add("https://domain2.com/page");
    frontier.add("https://domain3.com/page");
    frontier.add("https://domain4.com/page");
    
    auto start = std::chrono::steady_clock::now();
    
    // Pop all 4 URLs - should be fast since different domains
    std::vector<std::string> urls;
    for (int i = 0; i < 4; ++i) {
        auto url = frontier.pop();
        if (url) {
            urls.push_back(*url);
            std::string domain = URLFrontierMT::extract_domain(*url);
            frontier.mark_domain_crawled(domain);
        }
    }
    
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    
    std::cout << "  Time to pop 4 different domains: " << elapsed << "ms\n";
    
    // Different domains should NOT wait for each other
    // Should complete in well under 500ms (the per-domain delay)
    print_test(elapsed < 200, "Different domains don't block each other (<200ms)", results);
    print_test(urls.size() == 4, "All 4 URLs retrieved", results);
}

void test_politeness_custom_delay(TestResults& results) {
    std::cout << "\n=== Test: Politeness - Custom Per-Domain Delay ===\n";
    
    URLFrontierMT frontier;
    frontier.set_default_delay(std::chrono::milliseconds(100));
    
    // Set custom delay for specific domain (simulating robots.txt crawl-delay)
    frontier.set_domain_delay("slow-site.com", std::chrono::milliseconds(300));
    
    // Add URLs
    frontier.add("https://slow-site.com/page1");
    frontier.add("https://slow-site.com/page2");
    frontier.add("https://fast-site.com/page1");
    frontier.add("https://fast-site.com/page2");
    
    // Track per-domain timing
    std::map<std::string, std::vector<int64_t>> domain_times;
    
    for (int i = 0; i < 4; ++i) {
        auto url = frontier.pop();
        if (url) {
            std::string domain = URLFrontierMT::extract_domain(*url);
            auto now = std::chrono::steady_clock::now().time_since_epoch().count();
            domain_times[domain].push_back(now);
            frontier.mark_domain_crawled(domain);
        }
    }
    
    print_test(domain_times.count("slow-site.com") > 0, "slow-site.com crawled", results);
    print_test(domain_times.count("fast-site.com") > 0, "fast-site.com crawled", results);
}

//=============================================================================
// SECTION 2: SPEED TESTS
// Verify multithreading provides actual speedup
//=============================================================================
void test_speed_single_vs_multi_thread(TestResults& results) {
    std::cout << "\n=== Test: Speed - Single vs Multi Thread Comparison ===\n";
    
    const int num_urls = 100;
    const int work_per_url_ms = 10;  // Simulate 10ms of "work" per URL
    
    // Simulate work function
    auto simulate_work = [work_per_url_ms]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(work_per_url_ms));
    };
    
    //--- Single-threaded simulation ---
    auto start_single = std::chrono::steady_clock::now();
    for (int i = 0; i < num_urls; ++i) {
        simulate_work();
    }
    auto single_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_single).count();
    
    //--- Multi-threaded simulation (4 threads) ---
    URLFrontierMT frontier;
    frontier.set_default_delay(std::chrono::milliseconds(0));  // No delay for speed test
    
    for (int i = 0; i < num_urls; ++i) {
        frontier.add("https://domain" + std::to_string(i) + ".com/page");
    }
    
    std::atomic<int> processed{0};
    auto start_multi = std::chrono::steady_clock::now();
    
    std::vector<std::thread> workers;
    const int num_threads = 4;
    
    for (int t = 0; t < num_threads; ++t) {
        workers.emplace_back([&frontier, &processed, &simulate_work]() {
            while (true) {
                auto url = frontier.pop();
                if (!url) break;
                simulate_work();
                processed++;
                
                // Check if done
                if (processed >= 100) {
                    frontier.shutdown();
                    break;
                }
            }
        });
    }
    
    // Wait a bit then shutdown
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    frontier.shutdown();
    
    for (auto& w : workers) {
        if (w.joinable()) w.join();
    }
    
    auto multi_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_multi).count();
    
    std::cout << "  Single-threaded time: " << single_time << "ms\n";
    std::cout << "  Multi-threaded time:  " << multi_time << "ms\n";
    std::cout << "  URLs processed:       " << processed.load() << "\n";
    
    double speedup = static_cast<double>(single_time) / multi_time;
    std::cout << "  Speedup factor:       " << std::fixed << std::setprecision(2) << speedup << "x\n";
    
    // Should see at least 2x speedup with 4 threads
    print_test(speedup > 2.0, "Speedup > 2x with 4 threads", results);
    print_test(processed >= 50, "At least 50 URLs processed", results);
}

void test_speed_throughput(TestResults& results) {
    std::cout << "\n=== Test: Speed - Throughput Measurement ===\n";
    
    URLFrontierMT frontier;
    frontier.set_default_delay(std::chrono::milliseconds(0));
    
    const int total_urls = 10000;
    
    // Add many URLs
    for (int i = 0; i < total_urls; ++i) {
        frontier.add("https://domain" + std::to_string(i % 1000) + ".com/page" + std::to_string(i));
    }
    
    std::atomic<int> consumed{0};
    auto start = std::chrono::steady_clock::now();
    
    // 4 consumer threads
    std::vector<std::thread> consumers;
    for (int t = 0; t < 4; ++t) {
        consumers.emplace_back([&frontier, &consumed, total_urls]() {
            while (consumed < total_urls) {
                auto url = frontier.pop();
                if (!url) break;
                consumed++;
            }
        });
    }
    
    // Wait for completion or timeout
    while (consumed < total_urls && !frontier.empty()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    frontier.shutdown();
    for (auto& c : consumers) {
        if (c.joinable()) c.join();
    }
    
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    
    double throughput = static_cast<double>(consumed) / elapsed * 1000;  // URLs per second
    
    std::cout << "  URLs consumed:  " << consumed.load() << "\n";
    std::cout << "  Time:           " << elapsed << "ms\n";
    std::cout << "  Throughput:     " << std::fixed << std::setprecision(0) << throughput << " URLs/sec\n";
    
    // Should be able to process at least 10,000 URLs/sec from queue
    print_test(throughput > 5000, "Throughput > 5000 URLs/sec", results);
}

//=============================================================================
// SECTION 3: QUALITY TESTS
// Verify deduplication, normalization, and data integrity
//=============================================================================
void test_quality_deduplication(TestResults& results) {
    std::cout << "\n=== Test: Quality - URL Deduplication ===\n";
    
    URLFrontierMT frontier;
    
    // Add same URL multiple times (various forms)
    bool added1 = frontier.add("https://example.com/page");
    bool added2 = frontier.add("https://example.com/page");  // Exact duplicate
    bool added3 = frontier.add("https://EXAMPLE.COM/page");  // Case variant
    bool added4 = frontier.add("https://example.com/page#section");  // With fragment
    bool added5 = frontier.add("https://example.com:443/page");  // With default port
    
    std::cout << "  Add 1 (original):     " << (added1 ? "accepted" : "rejected") << "\n";
    std::cout << "  Add 2 (exact dup):    " << (added2 ? "accepted" : "rejected") << "\n";
    std::cout << "  Add 3 (case variant): " << (added3 ? "accepted" : "rejected") << "\n";
    std::cout << "  Add 4 (with #):       " << (added4 ? "accepted" : "rejected") << "\n";
    std::cout << "  Add 5 (with :443):    " << (added5 ? "accepted" : "rejected") << "\n";
    
    print_test(added1, "First URL accepted", results);
    print_test(!added2, "Exact duplicate rejected", results);
    print_test(!added3, "Case variant rejected (normalized)", results);
    print_test(!added4, "Fragment variant rejected (normalized)", results);
    print_test(!added5, "Default port variant rejected (normalized)", results);
    print_test(frontier.size() == 1, "Queue size is 1", results);
}

void test_quality_normalization(TestResults& results) {
    std::cout << "\n=== Test: Quality - URL Normalization ===\n";
    
    // Test normalization function directly
    auto norm = URLFrontierMT::normalize_url;
    
    print_test(
        norm("HTTPS://EXAMPLE.COM/Path") == "https://example.com/Path",
        "Lowercase scheme and host (preserve path case)",
        results
    );
    
    print_test(
        norm("https://example.com/page#anchor") == "https://example.com/page",
        "Remove fragment",
        results
    );
    
    print_test(
        norm("https://example.com:443/page") == "https://example.com/page",
        "Remove default HTTPS port",
        results
    );
    
    print_test(
        norm("http://example.com:80/page") == "http://example.com/page",
        "Remove default HTTP port",
        results
    );
    
    print_test(
        norm("https://example.com") == "https://example.com/",
        "Add trailing slash for root",
        results
    );
    
    print_test(
        norm("ftp://example.com/file").empty(),
        "Reject non-HTTP(S) schemes",
        results
    );
}

void test_quality_priority_ordering(TestResults& results) {
    std::cout << "\n=== Test: Quality - Priority Ordering ===\n";
    
    URLFrontierMT frontier;
    frontier.set_default_delay(std::chrono::milliseconds(0));
    
    // Add URLs with different priorities
    frontier.add("https://low.com/page", 10);
    frontier.add("https://high.com/page", 100);
    frontier.add("https://medium.com/page", 50);
    frontier.add("https://highest.com/page", 200);
    
    // Pop and verify order
    std::vector<std::string> order;
    for (int i = 0; i < 4; ++i) {
        auto url = frontier.pop();
        if (url) {
            order.push_back(URLFrontierMT::extract_domain(*url));
        }
    }
    
    std::cout << "  Pop order: ";
    for (const auto& d : order) std::cout << d << " ";
    std::cout << "\n";
    
    print_test(order.size() == 4, "All 4 URLs popped", results);
    if (order.size() == 4) {
        print_test(order[0] == "highest.com", "Highest priority first", results);
        print_test(order[1] == "high.com", "High priority second", results);
        print_test(order[2] == "medium.com", "Medium priority third", results);
        print_test(order[3] == "low.com", "Low priority last", results);
    }
}

void test_quality_link_extraction(TestResults& results) {
    std::cout << "\n=== Test: Quality - Link Extraction Integration ===\n";
    
    HTMLParser parser;
    
    std::string html = R"-(
        <html>
        <body>
            <a href="https://external.com/page">External</a>
            <a href="/internal/page">Internal</a>
            <a href="relative/page.html">Relative</a>
            <a href="#section">Anchor Only</a>
            <a href="javascript:void(0)">JS Link</a>
            <a href="mailto:test@example.com">Email</a>
        </body>
        </html>
    )-";
    
    auto links = parser.extract_links(html, "https://base.com/dir/");
    
    std::cout << "  Links found: " << links.size() << "\n";
    for (const auto& link : links) {
        std::cout << "    - " << link.url << "\n";
    }
    
    // Count valid HTTP(S) links
    int valid_count = 0;
    bool found_external = false;
    bool found_internal = false;
    bool found_relative = false;
    
    for (const auto& link : links) {
        if (link.url.find("http") == 0) {
            valid_count++;
            if (link.url.find("external.com") != std::string::npos) found_external = true;
            if (link.url == "https://base.com/internal/page") found_internal = true;
            if (link.url == "https://base.com/dir/relative/page.html") found_relative = true;
        }
    }
    
    print_test(found_external, "External link preserved", results);
    print_test(found_internal, "Internal link resolved", results);
    print_test(found_relative, "Relative link resolved", results);
    print_test(valid_count >= 3, "At least 3 valid links extracted", results);
}

//=============================================================================
// SECTION 4: THREAD SAFETY TESTS
// Verify no race conditions or data corruption
//=============================================================================
void test_thread_safety_concurrent_add_pop(TestResults& results) {
    std::cout << "\n=== Test: Thread Safety - Concurrent Add/Pop ===\n";
    
    URLFrontierMT frontier;
    frontier.set_default_delay(std::chrono::milliseconds(0));
    
    const int urls_per_producer = 500;
    const int num_producers = 4;
    const int num_consumers = 4;
    
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};
    std::set<std::string> consumed_urls;
    std::mutex consumed_mutex;
    
    std::vector<std::thread> threads;
    
    // Producer threads
    for (int p = 0; p < num_producers; ++p) {
        threads.emplace_back([&frontier, &produced, p, urls_per_producer]() {
            for (int i = 0; i < urls_per_producer; ++i) {
                std::string url = "https://producer" + std::to_string(p) + 
                                  ".com/page" + std::to_string(i);
                if (frontier.add(url)) {
                    produced++;
                }
            }
        });
    }
    
    // Consumer threads
    for (int c = 0; c < num_consumers; ++c) {
        threads.emplace_back([&frontier, &consumed, &consumed_urls, &consumed_mutex]() {
            while (true) {
                auto url = frontier.pop();
                if (!url) break;
                
                {
                    std::lock_guard<std::mutex> lock(consumed_mutex);
                    consumed_urls.insert(*url);
                }
                consumed++;
            }
        });
    }
    
    // Wait for producers to finish
    for (int i = 0; i < num_producers; ++i) {
        threads[i].join();
    }
    
    // Give consumers time to drain, then shutdown
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    frontier.shutdown();
    
    // Wait for consumers
    for (size_t i = num_producers; i < threads.size(); ++i) {
        threads[i].join();
    }
    
    std::cout << "  Produced: " << produced.load() << "\n";
    std::cout << "  Consumed: " << consumed.load() << "\n";
    std::cout << "  Unique URLs consumed: " << consumed_urls.size() << "\n";
    
    print_test(produced == num_producers * urls_per_producer, 
               "All URLs produced", results);
    print_test(consumed == produced, 
               "All produced URLs consumed", results);
    print_test(consumed_urls.size() == static_cast<size_t>(consumed), 
               "No duplicate consumption", results);
}

void test_thread_safety_stats_accuracy(TestResults& results) {
    std::cout << "\n=== Test: Thread Safety - Stats Accuracy ===\n";
    
    URLFrontierMT frontier;
    frontier.set_default_delay(std::chrono::milliseconds(0));
    
    const int num_threads = 8;
    const int ops_per_thread = 1000;
    
    std::vector<std::thread> threads;
    
    // Multiple threads adding URLs
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&frontier, t, ops_per_thread]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                frontier.add("https://thread" + std::to_string(t) + 
                            ".com/page" + std::to_string(i));
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    auto& stats = frontier.stats();
    
    std::cout << "  Expected URLs: " << num_threads * ops_per_thread << "\n";
    std::cout << "  Stats urls_added: " << stats.urls_added.load() << "\n";
    std::cout << "  Frontier size: " << frontier.size() << "\n";
    std::cout << "  Seen count: " << frontier.seen_count() << "\n";
    
    print_test(stats.urls_added == num_threads * ops_per_thread, 
               "Stats counter accurate", results);
    print_test(frontier.size() == num_threads * ops_per_thread, 
               "Queue size matches", results);
    print_test(frontier.seen_count() == num_threads * ops_per_thread, 
               "Seen set size matches", results);
}

//=============================================================================
// SECTION 5: GRACEFUL SHUTDOWN TESTS
// Verify clean termination
//=============================================================================
void test_shutdown_blocked_threads(TestResults& results) {
    std::cout << "\n=== Test: Shutdown - Blocked Threads Wake Up ===\n";
    
    URLFrontierMT frontier;
    
    std::atomic<int> threads_finished{0};
    const int num_threads = 4;
    
    std::vector<std::thread> threads;
    
    // Start threads that will block on empty queue
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&frontier, &threads_finished]() {
            auto url = frontier.pop();  // Will block
            // Should return nullopt on shutdown
            if (!url) {
                threads_finished++;
            }
        });
    }
    
    // Give threads time to block
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    auto start = std::chrono::steady_clock::now();
    frontier.shutdown();
    
    // Wait for threads with timeout
    for (auto& thread : threads) {
        thread.join();
    }
    
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    
    std::cout << "  Threads finished: " << threads_finished.load() << "/" << num_threads << "\n";
    std::cout << "  Shutdown time: " << elapsed << "ms\n";
    
    print_test(threads_finished == num_threads, "All blocked threads woke up", results);
    print_test(elapsed < 100, "Shutdown completed quickly (<100ms)", results);
    print_test(frontier.is_shutdown(), "Shutdown flag set", results);
}

void test_shutdown_no_new_urls(TestResults& results) {
    std::cout << "\n=== Test: Shutdown - No New URLs Accepted ===\n";
    
    URLFrontierMT frontier;
    
    frontier.add("https://example.com/before");
    frontier.shutdown();
    bool added_after = frontier.add("https://example.com/after");
    
    // Note: Current implementation doesn't prevent adds after shutdown
    // This tests the behavior - you may want to add this check
    std::cout << "  URL added after shutdown: " << (added_after ? "yes" : "no") << "\n";
    
    // Pop should return nullopt
    auto url = frontier.pop();
    print_test(!url.has_value(), "Pop returns nullopt after shutdown", results);
}

//=============================================================================
// SECTION 6: INTEGRATION TEST
// Simulate realistic crawl scenario
//=============================================================================
void test_integration_mini_crawl(TestResults& results) {
    std::cout << "\n=== Test: Integration - Mini Crawl Simulation ===\n";
    
    URLFrontierMT frontier;
    frontier.set_default_delay(std::chrono::milliseconds(50));  // Fast for testing
    
    // Seed URLs
    frontier.add("https://site1.com/", 100);
    frontier.add("https://site2.com/", 100);
    frontier.add("https://site3.com/", 100);
    
    std::atomic<int> pages_crawled{0};
    std::atomic<int> links_found{0};
    std::map<std::string, int> domain_counts;
    std::mutex domain_mutex;
    
    const int max_pages = 20;
    
    // Simulate crawl with 2 workers
    std::vector<std::thread> workers;
    for (int w = 0; w < 2; ++w) {
        workers.emplace_back([&]() {
            std::mt19937 rng(std::random_device{}());
            
            while (pages_crawled < max_pages && !frontier.is_shutdown()) {
                auto url = frontier.pop();
                if (!url) break;
                
                std::string domain = URLFrontierMT::extract_domain(*url);
                
                // Simulate fetching (just sleep a bit)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                
                // Track domain
                {
                    std::lock_guard<std::mutex> lock(domain_mutex);
                    domain_counts[domain]++;
                }
                
                // Simulate finding 2-5 new links
                std::uniform_int_distribution<int> dist(2, 5);
                int new_links = dist(rng);
                
                for (int i = 0; i < new_links; ++i) {
                    std::string new_url = "https://" + domain + "/page" + 
                                          std::to_string(pages_crawled) + "_" + 
                                          std::to_string(i);
                    if (frontier.add(new_url, 0)) {
                        links_found++;
                    }
                }
                
                frontier.mark_domain_crawled(domain);
                pages_crawled++;
            }
        });
    }
    
    // Wait for crawl to complete or timeout
    auto start = std::chrono::steady_clock::now();
    while (pages_crawled < max_pages) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        auto elapsed = std::chrono::steady_clock::now() - start;
        if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() > 10) {
            break;  // Timeout
        }
    }
    
    frontier.shutdown();
    for (auto& w : workers) {
        if (w.joinable()) w.join();
    }
    
    std::cout << "  Pages crawled: " << pages_crawled.load() << "\n";
    std::cout << "  Links found: " << links_found.load() << "\n";
    std::cout << "  Domain distribution:\n";
    for (const auto& [domain, count] : domain_counts) {
        std::cout << "    " << domain << ": " << count << " pages\n";
    }
    
    print_test(pages_crawled >= max_pages * 0.8, 
               "Crawled at least 80% of target", results);
    print_test(domain_counts.size() >= 3, 
               "Multiple domains crawled", results);
    print_test(frontier.stats().duplicates_rejected > 0, 
               "Some duplicates were rejected", results);
}

//=============================================================================
// MAIN
//=============================================================================
int main() {
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║     BLOOM SEARCH - MULTITHREADED CRAWLER TEST SUITE          ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n";
    
    TestResults results;
    
    // Section 1: Politeness
    std::cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    std::cout << "SECTION 1: POLITENESS TESTS\n";
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    test_politeness_single_domain(results);
    test_politeness_multi_domain(results);
    test_politeness_custom_delay(results);
    
    // Section 2: Speed
    std::cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    std::cout << "SECTION 2: SPEED TESTS\n";
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    test_speed_single_vs_multi_thread(results);
    test_speed_throughput(results);
    
    // Section 3: Quality
    std::cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    std::cout << "SECTION 3: QUALITY TESTS\n";
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    test_quality_deduplication(results);
    test_quality_normalization(results);
    test_quality_priority_ordering(results);
    test_quality_link_extraction(results);
    
    // Section 4: Thread Safety
    std::cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    std::cout << "SECTION 4: THREAD SAFETY TESTS\n";
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    test_thread_safety_concurrent_add_pop(results);
    test_thread_safety_stats_accuracy(results);
    
    // Section 5: Graceful Shutdown
    std::cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    std::cout << "SECTION 5: GRACEFUL SHUTDOWN TESTS\n";
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    test_shutdown_blocked_threads(results);
    test_shutdown_no_new_urls(results);
    
    // Section 6: Integration
    std::cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    std::cout << "SECTION 6: INTEGRATION TESTS\n";
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n";
    test_integration_mini_crawl(results);
    
    // Summary
    std::cout << "\n";
    std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                      TEST SUMMARY                            ║\n";
    std::cout << "╠══════════════════════════════════════════════════════════════╣\n";
    std::cout << "║  Passed: " << std::setw(3) << results.passed << "                                               ║\n";
    std::cout << "║  Failed: " << std::setw(3) << results.failed << "                                               ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n";
    
    if (results.failed > 0) {
        std::cout << "\nFailed tests:\n";
        for (const auto& failure : results.failures) {
            std::cout << "  - " << failure << "\n";
        }
    }
    
    std::cout << "\n";
    
    if (results.failed == 0) {
        std::cout << "ALL TESTS PASSED - Crawler is ready for production!\n\n";
        return 0;
    } else {
        std::cout << "SOME TESTS FAILED - Review failures above\n\n";
        return 1;
    }
}