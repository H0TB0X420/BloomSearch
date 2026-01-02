#include "crawler/url_frontier.h"  // or url_utils.h
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

// Test 1: Lowercase scheme and host
void test_lowercase(TestResults& results) {
    std::cout << "\n--- Test: Lowercase Scheme and Host ---\n";
    
    print_test(URLNormalizer::normalize("HTTP://EXAMPLE.COM/Page") == 
               "http://example.com/Page", 
               "Uppercase scheme and host lowercased", results);
    
    print_test(URLNormalizer::normalize("https://Example.Com/Path") == 
               "https://example.com/Path", 
               "Mixed case host lowercased", results);
    
    // Path should remain case-sensitive
    print_test(URLNormalizer::normalize("http://example.com/CaseSensitive") == 
               "http://example.com/CaseSensitive", 
               "Path case preserved", results);
}

// Test 2: Remove default ports
void test_default_ports(TestResults& results) {
    std::cout << "\n--- Test: Remove Default Ports ---\n";
    
    print_test(URLNormalizer::normalize("http://example.com:80/page") == 
               "http://example.com/page", 
               "HTTP port 80 removed", results);
    
    print_test(URLNormalizer::normalize("https://example.com:443/page") == 
               "https://example.com/page", 
               "HTTPS port 443 removed", results);
    
    // Non-default ports should remain
    print_test(URLNormalizer::normalize("http://example.com:8080/page") == 
               "http://example.com:8080/page", 
               "Non-default port 8080 kept", results);
    
    print_test(URLNormalizer::normalize("https://example.com:8443/page") == 
               "https://example.com:8443/page", 
               "Non-default port 8443 kept", results);
}

// Test 3: Remove fragments
void test_remove_fragments(TestResults& results) {
    std::cout << "\n--- Test: Remove Fragments ---\n";
    
    print_test(URLNormalizer::normalize("http://example.com/page#section") == 
               "http://example.com/page", 
               "Fragment #section removed", results);
    
    print_test(URLNormalizer::normalize("http://example.com/page#") == 
               "http://example.com/page", 
               "Empty fragment # removed", results);
    
    print_test(URLNormalizer::normalize("http://example.com/page?q=1#anchor") == 
               "http://example.com/page?q=1", 
               "Fragment after query removed", results);
}

// Test 4: Query string handling
void test_query_strings(TestResults& results) {
    std::cout << "\n--- Test: Query String Handling ---\n";
    
    print_test(URLNormalizer::normalize("http://example.com/page?") == 
               "http://example.com/page", 
               "Empty query ? removed", results);
    
    print_test(URLNormalizer::normalize("http://example.com/page?q=test") == 
               "http://example.com/page?q=test", 
               "Query string preserved", results);
    
    print_test(URLNormalizer::normalize("http://example.com/search?a=1&b=2") == 
               "http://example.com/search?a=1&b=2", 
               "Multiple query params preserved", results);
}

// Test 5: Path normalization
void test_path_normalization(TestResults& results) {
    std::cout << "\n--- Test: Path Normalization ---\n";
    
    print_test(URLNormalizer::normalize("http://example.com") == 
               "http://example.com/", 
               "Add trailing slash to bare domain", results);
    
    print_test(URLNormalizer::normalize("http://example.com/") == 
               "http://example.com/", 
               "Keep single trailing slash", results);
    
    // Note: We're keeping it simple - not resolving . and .. 
    // That can be added if needed
}

// Test 6: Percent encoding
void test_percent_encoding(TestResults& results) {
    std::cout << "\n--- Test: Percent Encoding ---\n";
    
    // Safe characters that can be decoded
    print_test(URLNormalizer::normalize("http://example.com/%7Euser") == 
               "http://example.com/~user", 
               "Decode %7E to ~", results);
    
    // Characters that should stay encoded
    print_test(URLNormalizer::normalize("http://example.com/path%20with%20spaces") == 
               "http://example.com/path%20with%20spaces", 
               "Keep %20 for spaces", results);
}

// Test 7: Invalid URLs
void test_invalid_urls(TestResults& results) {
    std::cout << "\n--- Test: Invalid URLs ---\n";
    
    print_test(URLNormalizer::normalize("not a url") == "", 
               "Garbage returns empty", results);
    
    print_test(URLNormalizer::normalize("") == "", 
               "Empty string returns empty", results);
    
    print_test(URLNormalizer::normalize("ftp://example.com/file") == "", 
               "Non-http(s) schemes return empty", results);
    
    print_test(URLNormalizer::normalize("javascript:alert(1)") == "", 
               "Javascript URLs return empty", results);
    
    print_test(URLNormalizer::normalize("mailto:test@example.com") == "", 
               "Mailto URLs return empty", results);
}

// Test 8: Duplicate detection (normalized URLs should match)
void test_duplicate_detection(TestResults& results) {
    std::cout << "\n--- Test: Duplicate Detection ---\n";
    
    std::vector<std::string> duplicates = {
        "http://example.com/page",
        "HTTP://EXAMPLE.COM/page",
        "http://example.com:80/page",
        "http://example.com/page#section",
        "http://example.com/page#other"
    };
    
    std::string normalized = URLNormalizer::normalize(duplicates[0]);
    bool all_match = true;
    
    for (const auto& url : duplicates) {
        if (URLNormalizer::normalize(url) != normalized) {
            all_match = false;
            std::cout << "[INFO] Mismatch: " << url << " -> " 
                      << URLNormalizer::normalize(url) << "\n";
        }
    }
    
    print_test(all_match, "All duplicate variations normalize to same URL", results);
}

// Test 9: Real-world URLs
void test_real_world_urls(TestResults& results) {
    std::cout << "\n--- Test: Real-World URLs ---\n";
    
    auto n = [](const std::string& url) { return URLNormalizer::normalize(url); };
    
    print_test(!n("https://en.wikipedia.org/wiki/Main_Page").empty(),
               "Wikipedia URL normalizes", results);
    
    print_test(!n("https://github.com/user/repo?tab=readme").empty(),
               "GitHub URL with query normalizes", results);
    
    print_test(!n("https://stackoverflow.com/questions/12345/title-here").empty(),
               "StackOverflow URL normalizes", results);
    
    print_test(n("https://example.com/path/../other") != "",
               "Path with .. handled (may or may not resolve)", results);
}

// Test 10: Edge cases
void test_edge_cases(TestResults& results) {
    std::cout << "\n--- Test: Edge Cases ---\n";
    
    print_test(URLNormalizer::normalize("http://example.com:80") == 
               "http://example.com/", 
               "Port 80 with no path", results);
    
    print_test(URLNormalizer::normalize("http://example.com?query") == 
               "http://example.com/?query", 
               "Query without path gets /", results);
    
    print_test(!URLNormalizer::normalize("http://localhost/page").empty(),
               "Localhost is valid", results);
    
    print_test(!URLNormalizer::normalize("http://192.168.1.1/page").empty(),
               "IP address is valid", results);
}

int main() {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "    URL Frontier Test Suite - Subtask 1.2.1                 \n";
    std::cout << "    URL Normalization                                        \n";
    std::cout << "============================================================\n";
    
    TestResults results;
    
    test_lowercase(results);
    test_default_ports(results);
    test_remove_fragments(results);
    test_query_strings(results);
    test_path_normalization(results);
    test_percent_encoding(results);
    test_invalid_urls(results);
    test_duplicate_detection(results);
    test_real_world_urls(results);
    test_edge_cases(results);
    
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "                    TEST SUMMARY                            \n";
    std::cout << "============================================================\n";
    std::cout << "Passed: " << results.passed << "\n";
    std::cout << "Failed: " << results.failed << "\n";
    std::cout << "\n";
    
    if (results.failed == 0) {
        std::cout << "[SUCCESS] All tests passed - Subtask 1.2.1 complete!\n\n";
        return 0;
    } else {
        std::cout << "[INCOMPLETE] Some tests failed\n\n";
        return 1;
    }
}