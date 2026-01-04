#include "common/date_utils.h"
#include <iostream>

using namespace search;

struct TestResults {
    int passed = 0;
    int failed = 0;
};

void print_test(bool passed, const std::string& name, TestResults& results) {
    std::cout << (passed ? "[PASS] " : "[FAIL] ") << name << "\n";
    passed ? results.passed++ : results.failed++;
}

void test_iso8601_parsing(TestResults& results) {
    std::cout << "\n--- Test: ISO 8601 Parsing ---\n";
    
    auto ts1 = DateUtils::parse_date("2024-01-15T10:00:00Z");
    print_test(ts1 > 0, "Parses full ISO 8601", results);
    
    auto ts2 = DateUtils::parse_date("2024-01-15");
    print_test(ts2 > 0, "Parses date-only ISO", results);
    
    auto ts3 = DateUtils::parse_date("2022-11-01");
    print_test(DateUtils::is_pre_ai(ts3), "Nov 1, 2022 is Pre-AI", results);
    
    auto ts4 = DateUtils::parse_date("2022-12-01");
    print_test(!DateUtils::is_pre_ai(ts4), "Dec 1, 2022 is Post-AI", results);
}

void test_text_date_parsing(TestResults& results) {
    std::cout << "\n--- Test: Text Date Parsing ---\n";
    
    auto ts1 = DateUtils::parse_date("January 15, 2024");
    print_test(ts1 > 0, "Parses 'January 15, 2024'", results);
    
    auto ts2 = DateUtils::parse_date("Jan 15, 2024");
    print_test(ts2 > 0, "Parses 'Jan 15, 2024'", results);
    
    auto ts3 = DateUtils::parse_date("March 5, 2020");
    print_test(DateUtils::is_pre_ai(ts3), "March 2020 is Pre-AI", results);
}

void test_url_date_extraction(TestResults& results) {
    std::cout << "\n--- Test: URL Date Extraction ---\n";
    
    auto ts1 = DateUtils::extract_date_from_url("https://blog.com/2023/05/article");
    print_test(ts1 > 0, "Extracts /2023/05/ pattern", results);
    
    auto ts2 = DateUtils::extract_date_from_url("https://site.com/post-2021-06-15-title");
    print_test(ts2 > 0, "Extracts YYYY-MM-DD pattern", results);
    print_test(DateUtils::is_pre_ai(ts2), "June 2021 is Pre-AI", results);
}

void test_era_classification(TestResults& results) {
    std::cout << "\n--- Test: Era Classification ---\n";
    
    // Nov 30, 2022 is the cutoff
    auto pre = DateUtils::parse_date("2022-11-29");
    auto post = DateUtils::parse_date("2022-11-30");
    
    print_test(DateUtils::is_pre_ai(pre), "Nov 29, 2022 is Pre-AI", results);
    print_test(!DateUtils::is_pre_ai(post), "Nov 30, 2022 is Post-AI", results);
    
    print_test(DateUtils::era_string(pre) == "pre-ai", "era_string returns 'pre-ai'", results);
    print_test(DateUtils::era_string(post) == "post-ai", "era_string returns 'post-ai'", results);
    print_test(DateUtils::era_string(0) == "unknown", "era_string(0) returns 'unknown'", results);
}

void test_date_formatting(TestResults& results) {
    std::cout << "\n--- Test: Date Formatting ---\n";
    
    auto ts = DateUtils::parse_date("2024-01-15");
    std::string formatted = DateUtils::format_date(ts);
    std::cout << "  Formatted: " << formatted << "\n";
    print_test(formatted.find("Jan") != std::string::npos, "Contains month name", results);
    print_test(formatted.find("2024") != std::string::npos, "Contains year", results);
    
    std::string with_era = DateUtils::format_with_era(ts);
    std::cout << "  With era: " << with_era << "\n";
    print_test(with_era.find("Post-AI") != std::string::npos, "Shows Post-AI era", results);
}

int main() {
    std::cout << "\n============================================================\n";
    std::cout << "    DateUtils Test Suite                                     \n";
    std::cout << "============================================================\n";
    
    TestResults results;
    
    test_iso8601_parsing(results);
    test_text_date_parsing(results);
    test_url_date_extraction(results);
    test_era_classification(results);
    test_date_formatting(results);
    
    std::cout << "\n============================================================\n";
    std::cout << "Passed: " << results.passed << " | Failed: " << results.failed << "\n";
    std::cout << (results.failed == 0 ? "[SUCCESS]" : "[FAILED]") << "\n\n";
    
    return results.failed;
}