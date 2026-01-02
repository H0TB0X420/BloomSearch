// Additional PostgresClient tests for new methods
// Add these test functions to your existing test_postgres_client.cpp

#include "storage/postgres_client.h"
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

//=============================================================================
// Test: mark_page_indexed
//=============================================================================
void test_mark_page_indexed(PostgresClient& db, TestResults& results) {
    std::cout << "\n--- Test: mark_page_indexed ---\n";
    
    // Insert a test page
    PageRecord page;
    page.url = "https://test-indexed.example.com/page1";
    page.domain = "test-indexed.example.com";
    page.status_code = 200;
    page.content_hash = "test_hash_indexed";
    page.content_size = 1000;
    page.indexed = false;
    
    bool inserted = db.upsert_page(page);
    print_test(inserted, "Insert test page", results);
    
    // Get the page to find its ID
    auto retrieved = db.get_page(page.url);
    print_test(retrieved.has_value(), "Page retrieved", results);
    
    if (!retrieved) return;
    
    print_test(retrieved->indexed == false, "Initially not indexed", results);
    
    // Mark as indexed
    bool marked = db.mark_page_indexed(retrieved->id);
    print_test(marked, "mark_page_indexed succeeded", results);
    
    // Verify it's now indexed
    auto after = db.get_page(page.url);
    print_test(after.has_value() && after->indexed == true, "Page now indexed", results);
}

//=============================================================================
// Test: get_indexed_page_count
//=============================================================================
void test_get_indexed_page_count(PostgresClient& db, TestResults& results) {
    std::cout << "\n--- Test: get_indexed_page_count ---\n";
    
    // Get initial count
    int64_t initial_count = db.get_indexed_page_count();
    std::cout << "[INFO] Initial indexed count: " << initial_count << "\n";
    
    // Insert and index a new page
    PageRecord page;
    page.url = "https://test-count.example.com/page" + std::to_string(time(nullptr));
    page.domain = "test-count.example.com";
    page.status_code = 200;
    page.content_hash = "test_hash_count";
    page.indexed = false;
    
    db.upsert_page(page);
    auto retrieved = db.get_page(page.url);
    
    if (retrieved) {
        db.mark_page_indexed(retrieved->id);
    }
    
    int64_t new_count = db.get_indexed_page_count();
    std::cout << "[INFO] New indexed count: " << new_count << "\n";
    
    print_test(new_count >= initial_count, "Indexed count increased or stable", results);
}

//=============================================================================
// Test: execute (raw SQL)
//=============================================================================
void test_execute_raw_sql(PostgresClient& db, TestResults& results) {
    std::cout << "\n--- Test: execute (raw SQL) ---\n";
    
    // Test CREATE INDEX IF NOT EXISTS (should succeed)
    bool create_index = db.execute(
        "CREATE INDEX IF NOT EXISTS idx_test_temp ON pages(url_hash)"
    );
    print_test(create_index, "CREATE INDEX executed", results);
    
    // Test ALTER TABLE ADD COLUMN IF NOT EXISTS
    bool alter_table = db.execute(
        "ALTER TABLE pages ADD COLUMN IF NOT EXISTS test_column_temp INTEGER"
    );
    print_test(alter_table, "ALTER TABLE executed", results);
    
    // Clean up - drop the test column (may fail if it doesn't exist, that's ok)
    db.execute("ALTER TABLE pages DROP COLUMN IF EXISTS test_column_temp");
    
    // Test invalid SQL (should fail gracefully)
    bool invalid = db.execute("THIS IS NOT VALID SQL");
    print_test(!invalid, "Invalid SQL returns false", results);
    
    // Check last_error is set
    print_test(!db.last_error().empty(), "last_error set after failure", results);
}

//=============================================================================
// Test: get_unindexed_pages (verify filters work)
//=============================================================================
void test_get_unindexed_pages_filters(PostgresClient& db, TestResults& results) {
    std::cout << "\n--- Test: get_unindexed_pages filters ---\n";
    
    // Insert a page that should NOT appear (indexed = true)
    PageRecord indexed_page;
    indexed_page.url = "https://test-filter.example.com/indexed";
    indexed_page.domain = "test-filter.example.com";
    indexed_page.status_code = 200;
    indexed_page.content_hash = "hash_indexed";
    indexed_page.indexed = true;
    db.upsert_page(indexed_page);
    
    // Insert a page that should NOT appear (status_code != 200)
    PageRecord error_page;
    error_page.url = "https://test-filter.example.com/error";
    error_page.domain = "test-filter.example.com";
    error_page.status_code = 404;
    error_page.content_hash = "hash_error";
    error_page.indexed = false;
    db.upsert_page(error_page);
    
    // Insert a page that SHOULD appear
    PageRecord valid_page;
    valid_page.url = "https://test-filter.example.com/valid";
    valid_page.domain = "test-filter.example.com";
    valid_page.status_code = 200;
    valid_page.content_hash = "hash_valid";
    valid_page.indexed = false;
    db.upsert_page(valid_page);
    
    // Mark as crawled (sets crawled_at)
    db.mark_crawled(valid_page.url, 200, "hash_valid", 500);
    
    // Get unindexed pages
    auto unindexed = db.get_unindexed_pages(100);
    
    // Check valid_page appears
    bool found_valid = false;
    bool found_indexed = false;
    bool found_error = false;
    
    for (const auto& p : unindexed) {
        if (p.url == valid_page.url) found_valid = true;
        if (p.url == indexed_page.url) found_indexed = true;
        if (p.url == error_page.url) found_error = true;
    }
    
    print_test(found_valid, "Valid unindexed page found", results);
    print_test(!found_indexed, "Indexed page filtered out", results);
    print_test(!found_error, "Error page filtered out", results);
}

//=============================================================================
// Main
//=============================================================================
int main() {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "    PostgresClient Additional Tests                         \n";
    std::cout << "============================================================\n";
    
    TestResults results;
    
    PostgresClient db;
    if (!db.connect()) {
        std::cout << "[ERROR] Failed to connect to PostgreSQL: " << db.last_error() << "\n";
        return 1;
    }
    
    std::cout << "[INFO] Connected to PostgreSQL\n";
    
    // Ensure schema exists
    db.initialize_schema();
    
    // Add indexed column if not exists
    db.execute("ALTER TABLE pages ADD COLUMN IF NOT EXISTS indexed BOOLEAN DEFAULT FALSE");
    
    test_mark_page_indexed(db, results);
    test_get_indexed_page_count(db, results);
    test_execute_raw_sql(db, results);
    test_get_unindexed_pages_filters(db, results);
    
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "                    TEST SUMMARY                            \n";
    std::cout << "============================================================\n";
    std::cout << "Passed: " << results.passed << "\n";
    std::cout << "Failed: " << results.failed << "\n";
    std::cout << "\n";
    
    if (results.failed == 0) {
        std::cout << "[SUCCESS] All additional PostgresClient tests passed!\n\n";
        return 0;
    } else {
        std::cout << "[FAILED] Some tests failed\n\n";
        return 1;
    }
}