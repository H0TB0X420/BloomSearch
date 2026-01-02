#include "crawler/http_fetcher.h"
#include "common/logger.h"
#include "common/config.h"
#include <iostream>
#include <cassert>
#include <string>
#include <vector>
using namespace search;

// Test result tracking
struct TestResults {
    int passed = 0;
    int failed = 0;
    int skipped = 0;
};

void print_test_header(const std::string& test_name) {
    std::cout << "\n========================================\n";
    std::cout << "TEST: " << test_name << "\n";
    std::cout << "========================================\n";
}

void print_test_result(bool passed, const std::string& message = "") {
    if (passed) {
        std::cout << "[PASS] " << message << "\n";
    } else {
        std::cout << "[FAIL] " << message << "\n";
    }
}

void print_skip(const std::string& reason) {
    std::cout << "[SKIP] " << reason << "\n";
}

// Test 1: Basic GET Request
bool test_basic_get_request(TestResults& results) {
    print_test_header("Basic GET Request (HTTP)");
    
    HTTPFetcher fetcher;
    std::string content;
    
    Logger::info("Fetching http://example.com");
    bool success = fetcher.fetch("http://example.com", content);
    
    if (!success) {
        print_test_result(false, "Failed to fetch example.com");
        results.failed++;
        return false;
    }
    
    if (content.empty()) {
        print_test_result(false, "Content is empty");
        results.failed++;
        return false;
    }
    
    if (content.find("Example Domain") == std::string::npos) {
        print_test_result(false, "Content doesn't contain 'Example Domain'");
        results.failed++;
        return false;
    }
    
    print_test_result(true, "Successfully fetched example.com");
    print_test_result(true, "Content is not empty (" + std::to_string(content.size()) + " bytes)");
    print_test_result(true, "Content contains expected text");
    results.passed += 3;
    return true;
}

// Test 2: HTTP Status Codes using reliable endpoints
bool test_http_status_codes(TestResults& results) {
    print_test_header("HTTP Status Codes");
    
    HTTPFetcher fetcher;
    std::string content;
    
    // Test 200 OK - use reliable endpoints
    Logger::info("Testing 200 OK with example.com");
    bool success_200 = fetcher.fetch("https://example.com", content);
    if (success_200 && !content.empty()) {
        print_test_result(true, "200 OK returns success");
        results.passed++;
    } else {
        print_test_result(false, "200 OK should return success");
        results.failed++;
    }
    
    // Test 404 Not Found - GitHub reliably returns 404 for non-existent repos
    Logger::info("Testing 404 with non-existent GitHub URL");
    bool success_404 = fetcher.fetch("https://github.com/nonexistent-user-xyz-99999/nonexistent-repo-abc-12345", content);
    if (!success_404) {
        print_test_result(true, "404 returns failure");
        results.passed++;
    } else {
        print_test_result(false, "404 should return failure");
        results.failed++;
    }
    
    // Note: 500 errors are hard to test reliably without custom infrastructure
    print_skip("500 Internal Server Error - requires custom test server");
    results.skipped++;
    
    return true;
}

// Test 3: Redirects using real-world redirect behavior
bool test_redirects(TestResults& results) {
    print_test_header("Handle Redirects");
    
    HTTPFetcher fetcher;
    std::string content;
    
    // GitHub redirects http -> https, stackoverflow redirects to /questions
    Logger::info("Testing redirect with stackoverflow.com");
    bool success = fetcher.fetch("https://stackoverflow.com", content);
    
    if (success && !content.empty()) {
        print_test_result(true, "Followed redirect successfully");
        results.passed++;
    } else {
        print_test_result(false, "Should follow redirects");
        results.failed++;
    }
    
    // Test HTTP to HTTPS redirect
    Logger::info("Testing HTTP to HTTPS redirect");
    bool http_redirect = fetcher.fetch("http://www.github.com", content);
    if (http_redirect) {
        print_test_result(true, "HTTP to HTTPS redirect works");
        results.passed++;
    } else {
        print_test_result(false, "HTTP to HTTPS redirect should work");
        results.failed++;
    }
    
    print_skip("Max redirect limit test requires custom server");
    results.skipped++;
    
    return true;
}

// Test 4: Timeouts
bool test_timeouts(TestResults& results) {
    print_test_header("Timeouts");
    
    // Note: Testing timeouts reliably requires a server that intentionally delays
    // Without httpstat.us or similar, we can't easily test this
    
    print_skip("Timeout testing requires a server with configurable delays");
    print_skip("Consider setting up a local test server or using Docker");
    results.skipped += 2;
    
    return true;
}

// Test 5: Request Headers
bool test_request_headers(TestResults& results) {
    print_test_header("Request Headers");
    
    // Without httpbin.org, we can't easily inspect headers
    // The headers are set, but we need a reflection service to verify
    
    print_skip("Header verification requires a request reflection service");
    print_skip("Headers are configured in code - manual verification done during code review");
    results.skipped += 2;
    
    // We CAN verify that the User-Agent doesn't cause rejections
    HTTPFetcher fetcher;
    std::string content;
    
    Logger::info("Verifying User-Agent doesn't cause rejections");
    bool success = fetcher.fetch("https://www.wikipedia.org", content);
    if (success) {
        print_test_result(true, "User-Agent accepted by Wikipedia");
        results.passed++;
    } else {
        print_test_result(false, "Request rejected - possible User-Agent issue");
        results.failed++;
    }
    
    return true;
}

// Test 6: Retry Logic
bool test_retry_logic(TestResults& results) {
    print_test_header("Retry Logic");
    
    HTTPFetcher fetcher;
    std::string content;
    
    // Test that 4xx doesn't retry - GitHub 404 should be fast
    Logger::info("Testing that 4xx errors don't retry (should be fast)");
    auto start = std::chrono::steady_clock::now();
    bool success_404 = fetcher.fetch("https://github.com/nonexistent-xyz-99999/repo-abc-12345", content);
    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    // 4xx should fail immediately without retries (< 3 seconds)
    if (!success_404 && duration < 3000) {
        print_test_result(true, "4xx errors don't retry (completed in " + 
                         std::to_string(duration) + "ms)");
        results.passed++;
    } else if (!success_404 && duration >= 3000) {
        print_test_result(false, "4xx took too long (" + std::to_string(duration) + 
                         "ms) - may be retrying when it shouldn't");
        results.failed++;
    } else {
        print_test_result(false, "404 should return failure");
        results.failed++;
    }
    
    print_skip("Network error retry test requires unreliable endpoint");
    results.skipped++;
    
    return true;
}

// Test 7: HTTPS Support
bool test_https_support(TestResults& results) {
    print_test_header("HTTPS Support");
    
    HTTPFetcher fetcher;
    std::string content;
    
    // Test valid HTTPS
    Logger::info("Testing valid HTTPS");
    bool success_https = fetcher.fetch("https://example.com", content);
    if (success_https && !content.empty()) {
        print_test_result(true, "HTTPS works correctly");
        results.passed++;
    } else {
        print_test_result(false, "HTTPS should work");
        results.failed++;
    }
    
    // Test expired SSL certificate - badssl.com provides test certificates
    Logger::info("Testing expired SSL certificate");
    bool success_expired = fetcher.fetch("https://expired.badssl.com/", content);
    if (!success_expired) {
        print_test_result(true, "Expired SSL certificate rejected");
        results.passed++;
    } else {
        print_test_result(false, "Expired SSL should be rejected");
        results.failed++;
    }
    
    // Test self-signed certificate
    Logger::info("Testing self-signed SSL certificate");
    bool success_selfsigned = fetcher.fetch("https://self-signed.badssl.com/", content);
    if (!success_selfsigned) {
        print_test_result(true, "Self-signed SSL certificate rejected");
        results.passed++;
    } else {
        print_test_result(false, "Self-signed SSL should be rejected");
        results.failed++;
    }
    
    // Test that HTTP still works
    Logger::info("Testing that HTTP still works");
    bool success_http = fetcher.fetch("http://example.com", content);
    if (success_http) {
        print_test_result(true, "HTTP still works");
        results.passed++;
    } else {
        print_test_result(false, "HTTP should work");
        results.failed++;
    }
    
    return true;
}

// Test 8: Integration Test with Real URLs
bool test_integration_real_urls(TestResults& results) {
    print_test_header("Integration with Real URLs");
    
    HTTPFetcher fetcher;
    std::string content;
    
    std::vector<std::pair<std::string, std::string>> test_urls = {
        {"https://www.wikipedia.org", "Wikipedia"},
        {"https://github.com", "GitHub"},
        {"https://stackoverflow.com", "Stack Overflow"},
        {"http://example.org", "Example.org"},
        {"https://www.ietf.org", "IETF"}
    };
    
    int successful = 0;
    for (const auto& [url, name] : test_urls) {
        Logger::info("Testing: " + url);
        content.clear();
        if (fetcher.fetch(url, content) && !content.empty()) {
            successful++;
            print_test_result(true, "Fetched " + name + " (" + std::to_string(content.size()) + " bytes)");
            results.passed++;
        } else {
            print_test_result(false, "Failed: " + name);
            results.failed++;
        }
    }
    
    std::cout << "\nSuccessfully fetched " << successful << "/" 
              << test_urls.size() << " URLs\n";
    
    return successful >= 3;
}

// Test 9: Content Size Limits (basic sanity check)
bool test_content_sanity(TestResults& results) {
    print_test_header("Content Sanity Checks");
    
    HTTPFetcher fetcher;
    std::string content;
    
    // Fetch a known page and verify we get reasonable content
    Logger::info("Fetching example.com for sanity check");
    bool success = fetcher.fetch("https://example.com", content);
    
    if (!success) {
        print_test_result(false, "Failed to fetch for sanity check");
        results.failed++;
        return false;
    }
    
    // Check content size is reasonable (example.com is ~1KB)
    if (content.size() > 500 && content.size() < 5000) {
        print_test_result(true, "Content size reasonable: " + std::to_string(content.size()) + " bytes");
        results.passed++;
    } else {
        print_test_result(false, "Content size unexpected: " + std::to_string(content.size()) + " bytes");
        results.failed++;
    }
    
    // Check it looks like HTML
    if (content.find("<!doctype html>") != std::string::npos || 
        content.find("<!DOCTYPE html>") != std::string::npos ||
        content.find("<html") != std::string::npos) {
        print_test_result(true, "Content appears to be HTML");
        results.passed++;
    } else {
        print_test_result(false, "Content doesn't look like HTML");
        results.failed++;
    }
    
    return true;
}

int main() {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "         HTTP Fetcher Test Suite v2                         \n";
    std::cout << "         Bloom Search - Phase 2, Task 1                     \n";
    std::cout << "============================================================\n";
    std::cout << "\nNote: Some tests require dedicated test servers and are skipped.\n";
    std::cout << "Focus is on verifying core functionality with reliable endpoints.\n";
    
    TestResults results;
    
    try {
        test_basic_get_request(results);
        test_http_status_codes(results);
        test_redirects(results);
        test_timeouts(results);
        test_request_headers(results);
        test_retry_logic(results);
        test_https_support(results);
        test_integration_real_urls(results);
        test_content_sanity(results);
        
    } catch (const std::exception& e) {
        std::cerr << "\n[ERROR] Exception caught: " << e.what() << "\n";
        return 1;
    }
    
    // Print summary
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "                    TEST SUMMARY                            \n";
    std::cout << "============================================================\n";
    std::cout << "Passed:  " << results.passed << "\n";
    std::cout << "Failed:  " << results.failed << "\n";
    std::cout << "Skipped: " << results.skipped << "\n";
    std::cout << "Total:   " << (results.passed + results.failed + results.skipped) << "\n";
    std::cout << "\n";
    
    if (results.failed == 0) {
        std::cout << "[SUCCESS] ALL TESTS PASSED\n\n";
        return 0;
    } else {
        std::cout << "[FAILURE] SOME TESTS FAILED\n\n";
        return 1;
    }
}