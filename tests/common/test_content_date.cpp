#include "common/date_utils.h"
#include <iostream>
#include <cassert>

using namespace search;

struct TestResults {
    int passed = 0;
    int failed = 0;
};

void print_test(bool passed, const std::string& name, TestResults& results) {
    std::cout << (passed ? "[PASS] " : "[FAIL] ") << name << "\n";
    passed ? results.passed++ : results.failed++;
}

void test_published_patterns(TestResults& results) {
    std::cout << "\n--- Test: Published/Posted Patterns ---\n";
    
    auto ts1 = DateUtils::extract_date_from_content("Published January 15, 2021");
    print_test(ts1 > 0, "Published January 15, 2021", results);
    print_test(DateUtils::is_pre_ai(ts1), "  -> is Pre-AI", results);
    
    auto ts2 = DateUtils::extract_date_from_content("Posted on 2020-03-22 by admin");
    print_test(ts2 > 0, "Posted on 2020-03-22", results);
    
    auto ts3 = DateUtils::extract_date_from_content("Written: December 5, 2019");
    print_test(ts3 > 0, "Written: December 5, 2019", results);
    
    auto ts4 = DateUtils::extract_date_from_content("Posted on November 30, 2022");
    print_test(ts4 > 0, "Posted on November 30, 2022", results);
    print_test(!DateUtils::is_pre_ai(ts4), "  -> is Post-AI (exact cutoff)", results);
}

void test_labeled_patterns(TestResults& results) {
    std::cout << "\n--- Test: Labeled Date Patterns ---\n";
    
    auto ts1 = DateUtils::extract_date_from_content("Date: March 10, 2018");
    print_test(ts1 > 0, "Date: March 10, 2018", results);
    
    auto ts2 = DateUtils::extract_date_from_content("Last updated: 2021-06-15");
    print_test(ts2 > 0, "Last updated: 2021-06-15", results);
    
    auto ts3 = DateUtils::extract_date_from_content("Modified: Apr 5, 2020");
    print_test(ts3 > 0, "Modified: Apr 5, 2020", results);
}

void test_standalone_dates(TestResults& results) {
    std::cout << "\n--- Test: Standalone Dates ---\n";
    
    // Date at start of content
    auto ts1 = DateUtils::extract_date_from_content("February 28, 2019\n\nThis article explores...");
    print_test(ts1 > 0, "Standalone at start: February 28, 2019", results);
    
    // ISO date in content
    auto ts2 = DateUtils::extract_date_from_content("2018-11-20\n\nWelcome to our guide...");
    print_test(ts2 > 0, "Standalone ISO: 2018-11-20", results);
}

void test_copyright_fallback(TestResults& results) {
    std::cout << "\n--- Test: Copyright Year Fallback ---\n";
    
    auto ts1 = DateUtils::extract_date_from_content("© 2017 Company Inc. All rights reserved.");
    print_test(ts1 > 0, "© 2017", results);
    print_test(DateUtils::is_pre_ai(ts1), "  -> is Pre-AI", results);
    
    auto ts2 = DateUtils::extract_date_from_content("Copyright 2015 by Author");
    print_test(ts2 > 0, "Copyright 2015", results);
}

void test_no_false_positives(TestResults& results) {
    std::cout << "\n--- Test: No False Positives ---\n";
    
    // Historical date shouldn't match (year too old)
    auto ts1 = DateUtils::extract_date_from_content("The year 1776 was important for America.");
    print_test(ts1 == 0, "Historical year 1776 not matched", results);
    
    // Future year shouldn't match
    auto ts2 = DateUtils::extract_date_from_content("Expected completion by 2050.");
    print_test(ts2 == 0, "Future year 2050 not matched", results);
    
    // Random numbers shouldn't match
    auto ts3 = DateUtils::extract_date_from_content("The price is $2024 for the package.");
    print_test(ts3 == 0, "Dollar amount not matched", results);
}

void test_priority_order(TestResults& results) {
    std::cout << "\n--- Test: Priority Order ---\n";
    
    // "Published" should win over copyright
    std::string content = "© 2015 Company\n\nPublished March 10, 2020\n\nArticle text...";
    auto ts = DateUtils::extract_date_from_content(content);
    std::string date = DateUtils::format_date(ts);
    std::cout << "  Found: " << date << "\n";
    print_test(date.find("2020") != std::string::npos, "Published date (2020) wins over copyright (2015)", results);
}

int main() {
    std::cout << "\n============================================================\n";
    std::cout << "    Content Date Extraction Tests                           \n";
    std::cout << "============================================================\n";
    
    TestResults results;
    
    test_published_patterns(results);
    test_labeled_patterns(results);
    test_standalone_dates(results);
    test_copyright_fallback(results);
    test_no_false_positives(results);
    test_priority_order(results);
    
    std::cout << "\n============================================================\n";
    std::cout << "Passed: " << results.passed << " | Failed: " << results.failed << "\n";
    std::cout << (results.failed == 0 ? "[SUCCESS]" : "[FAILED]") << "\n\n";
    
    return results.failed;
}