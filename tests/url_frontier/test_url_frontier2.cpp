#include "crawler/url_frontier.h"
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

int main() {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "    URL Frontier Test Suite - Subtask 1.2.2                 \n";
    std::cout << "    Domain Extraction                                        \n";
    std::cout << "============================================================\n\n";
    
    TestResults results;
    
    // Basic extraction
    print_test(URLNormalizer::extract_domain("http://example.com/path") == "example.com",
               "Basic domain extraction", results);
    
    print_test(URLNormalizer::extract_domain("https://www.example.com/page") == "www.example.com",
               "Domain with www prefix", results);
    
    print_test(URLNormalizer::extract_domain("http://sub.domain.example.com/") == "sub.domain.example.com",
               "Subdomain extraction", results);
    
    // Case normalization
    print_test(URLNormalizer::extract_domain("http://EXAMPLE.COM/path") == "example.com",
               "Uppercase domain lowercased", results);
    
    // With port (port should NOT be included in domain)
    print_test(URLNormalizer::extract_domain("http://example.com:8080/path") == "example.com",
               "Port excluded from domain", results);
    
    // IP addresses
    print_test(URLNormalizer::extract_domain("http://192.168.1.1/path") == "192.168.1.1",
               "IP address as domain", results);
    
    print_test(URLNormalizer::extract_domain("http://localhost/path") == "localhost",
               "Localhost as domain", results);
    
    // Invalid URLs
    print_test(URLNormalizer::extract_domain("not-a-url") == "",
               "Invalid URL returns empty", results);
    
    print_test(URLNormalizer::extract_domain("") == "",
               "Empty string returns empty", results);
    
    print_test(URLNormalizer::extract_domain("ftp://example.com/file") == "",
               "Non-http scheme returns empty", results);

    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "                    TEST SUMMARY                            \n";
    std::cout << "============================================================\n";
    std::cout << "Passed: " << results.passed << "\n";
    std::cout << "Failed: " << results.failed << "\n";
    std::cout << "\n";
    
    if (results.failed == 0) {
        std::cout << "[SUCCESS] All tests passed - Subtask 1.2.2 complete!\n\n";
        return 0;
    } else {
        std::cout << "[INCOMPLETE] Some tests failed\n\n";
        return 1;
    }
}