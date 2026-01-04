#include "crawler/url_filter.h"
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

void test_length_filter(TestResults& results) {
    std::cout << "\n--- Test: URL Length Filter ---\n";
    
    // Normal URL should pass
    print_test(URLFilter::is_acceptable("https://example.com/page"), 
               "Normal URL accepted", results);
    
    // Very long URL should fail
    std::string long_url = "https://example.com/";
    for (int i = 0; i < 300; ++i) {
        long_url += "a";
    }
    print_test(!URLFilter::is_acceptable(long_url), 
               "Long URL (300+ chars) rejected", results);
}

void test_extension_filter(TestResults& results) {
    std::cout << "\n--- Test: File Extension Filter ---\n";
    
    // Good extensions
    print_test(URLFilter::is_acceptable("https://example.com/page.html"), 
               "HTML accepted", results);
    print_test(URLFilter::is_acceptable("https://example.com/article"), 
               "No extension accepted", results);
    
    // Bad extensions
    print_test(!URLFilter::is_acceptable("https://example.com/file.pdf"), 
               "PDF rejected", results);
    print_test(!URLFilter::is_acceptable("https://example.com/image.jpg"), 
               "JPG rejected", results);
    print_test(!URLFilter::is_acceptable("https://example.com/archive.zip"), 
               "ZIP rejected", results);
    print_test(!URLFilter::is_acceptable("https://example.com/style.css"), 
               "CSS rejected", results);
    print_test(!URLFilter::is_acceptable("https://example.com/script.js"), 
               "JS rejected", results);
}

void test_pagination_filter(TestResults& results) {
    std::cout << "\n--- Test: Pagination Filter ---\n";
    
    // Pagination URLs should be rejected
    print_test(!URLFilter::is_acceptable("https://example.com/articles?page=2"), 
               "?page= rejected", results);
    print_test(!URLFilter::is_acceptable("https://example.com/list?p=5"), 
               "?p= rejected", results);
    print_test(!URLFilter::is_acceptable("https://example.com/results?offset=100"), 
               "?offset= rejected", results);
    print_test(!URLFilter::is_acceptable("https://example.com/blog/page/3"), 
               "/page/N rejected", results);
    
    // Non-pagination should pass
    print_test(URLFilter::is_acceptable("https://example.com/homepage"), 
               "homepage accepted", results);
}

void test_query_param_filter(TestResults& results) {
    std::cout << "\n--- Test: Query Parameter Filter ---\n";
    
    // Few params OK
    print_test(URLFilter::is_acceptable("https://example.com/interesting?q=test"), 
               "1 param accepted", results);
    print_test(URLFilter::is_acceptable("https://example.com/api?a=1&b=2&c=3"), 
               "3 params accepted", results);
    
    // Too many params rejected
    print_test(!URLFilter::is_acceptable("https://example.com/api?a=1&b=2&c=3&d=4&e=5"), 
               "5 params rejected", results);
}

void test_low_value_patterns(TestResults& results) {
    std::cout << "\n--- Test: Low Value Pattern Filter ---\n";
    
    // Auth pages
    print_test(!URLFilter::is_acceptable("https://example.com/login"), 
               "/login rejected", results);
    print_test(!URLFilter::is_acceptable("https://example.com/user/signup"), 
               "/signup rejected", results);
    
    // GitHub patterns
    print_test(!URLFilter::is_acceptable("https://github.com/user/repo/forks"), 
               "/forks rejected", results);
    print_test(!URLFilter::is_acceptable("https://github.com/user/repo/stargazers"), 
               "/stargazers rejected", results);
    
    // Wikipedia special pages
    print_test(!URLFilter::is_acceptable("https://en.wikipedia.org/wiki/Special:Random"), 
               "Wiki Special: rejected", results);
    print_test(!URLFilter::is_acceptable("https://en.wikipedia.org/wiki/Talk:Article"), 
               "Wiki Talk: rejected", results);
    print_test(!URLFilter::is_acceptable("https://en.wikipedia.org/w/index.php?action=edit"), 
               "Wiki action=edit rejected", results);
    
    // Good Wikipedia pages should pass
    print_test(URLFilter::is_acceptable("https://en.wikipedia.org/wiki/Computer_science"), 
               "Wiki article accepted", results);
    
    // Tracking params
    print_test(!URLFilter::is_acceptable("https://example.com/article?utm_source=twitter"), 
               "utm_ rejected", results);
}

void test_rejection_reason(TestResults& results) {
    std::cout << "\n--- Test: Rejection Reasons ---\n";
    
    std::string long_url = "https://example.com/" + std::string(300, 'x');
    std::cout << "  Long URL reason: " << URLFilter::rejection_reason(long_url) << "\n";
    print_test(URLFilter::rejection_reason(long_url).find("too long") != std::string::npos,
               "Long URL reason correct", results);
    
    std::cout << "  PDF reason: " << URLFilter::rejection_reason("https://x.com/f.pdf") << "\n";
    print_test(URLFilter::rejection_reason("https://x.com/f.pdf") == "bad extension",
               "Bad extension reason correct", results);
    
    std::cout << "  Pagination reason: " << URLFilter::rejection_reason("https://x.com/?page=2") << "\n";
    print_test(URLFilter::rejection_reason("https://x.com/?page=2") == "pagination",
               "Pagination reason correct", results);
}

int main() {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "    URL Filter Test Suite                                   \n";
    std::cout << "============================================================\n";
    
    TestResults results;
    
    test_length_filter(results);
    test_extension_filter(results);
    test_pagination_filter(results);
    test_query_param_filter(results);
    test_low_value_patterns(results);
    test_rejection_reason(results);
    
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "                    TEST SUMMARY                            \n";
    std::cout << "============================================================\n";
    std::cout << "Passed: " << results.passed << "\n";
    std::cout << "Failed: " << results.failed << "\n";
    std::cout << "\n";
    
    if (results.failed == 0) {
        std::cout << "[SUCCESS] All URL filter tests passed!\n\n";
        return 0;
    } else {
        std::cout << "[WARNING] Some tests failed.\n\n";
        return 1;
    }
}