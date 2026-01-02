#include "storage/rocksdb_client.h"
#include <iostream>
#include <filesystem>
#include <cstdlib>

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

// Get test database path
std::string get_test_db_path() {
    const char* rocksdb_path = std::getenv("ROCKSDB_PATH");
    std::string base = rocksdb_path ? rocksdb_path : "/tmp";
    return base + "/test_rocksdb_" + std::to_string(time(nullptr));
}

// Clean up test database
void cleanup_db(const std::string& path) {
    std::filesystem::remove_all(path);
}

//=============================================================================
// Test 1: Database setup (2.3.1)
//=============================================================================
void test_database_setup(TestResults& results) {
    std::cout << "\n--- Test: Database Setup (2.3.1) ---\n";
    
    std::string db_path = get_test_db_path();
    
    RocksDBClient client;
    
    // Initially not open
    print_test(!client.is_open(), "Initially not open", results);
    
    // Open database
    bool opened = client.open(db_path);
    if (!opened) {
        std::cout << "[INFO] Error: " << client.last_error() << "\n";
    }
    print_test(opened, "Database opened", results);
    print_test(client.is_open(), "is_open() returns true", results);
    print_test(client.path() == db_path, "Path stored correctly", results);
    
    // Close database
    client.close();
    print_test(!client.is_open(), "Closed successfully", results);
    
    // Reopen
    bool reopened = client.open(db_path);
    print_test(reopened, "Database reopened", results);
    
    client.close();
    cleanup_db(db_path);
}

//=============================================================================
// Test 2: Basic operations (2.3.2)
//=============================================================================
void test_basic_operations(TestResults& results) {
    std::cout << "\n--- Test: Basic Operations (2.3.2) ---\n";
    
    std::string db_path = get_test_db_path();
    RocksDBClient client;
    client.open(db_path);
    
    // Put
    bool put_ok = client.put("key1", "value1");
    print_test(put_ok, "Put operation", results);
    
    // Get
    auto value = client.get("key1");
    print_test(value.has_value(), "Get returns value", results);
    print_test(value.value_or("") == "value1", "Value matches", results);
    
    // Get non-existent
    auto missing = client.get("nonexistent");
    print_test(!missing.has_value(), "Get missing returns nullopt", results);
    
    // Exists
    print_test(client.exists("key1"), "Exists returns true for existing", results);
    print_test(!client.exists("nonexistent"), "Exists returns false for missing", results);
    
    // Update
    client.put("key1", "updated");
    auto updated = client.get("key1");
    print_test(updated.value_or("") == "updated", "Update works", results);
    
    // Delete
    bool deleted = client.remove("key1");
    print_test(deleted, "Delete operation", results);
    print_test(!client.exists("key1"), "Key gone after delete", results);
    
    client.close();
    cleanup_db(db_path);
}

//=============================================================================
// Test 3: Batch operations (2.3.3)
//=============================================================================
void test_batch_operations(TestResults& results) {
    std::cout << "\n--- Test: Batch Operations (2.3.3) ---\n";
    
    std::string db_path = get_test_db_path();
    RocksDBClient client;
    client.open(db_path);
    
    // Begin batch
    client.begin_batch();
    print_test(client.has_pending_batch(), "Batch started", results);
    
    // Add to batch
    for (int i = 0; i < 100; ++i) {
        client.batch_put("batch_key_" + std::to_string(i), "batch_value_" + std::to_string(i));
    }
    
    // Not visible until commit
    print_test(!client.exists("batch_key_0"), "Not visible before commit", results);
    
    // Commit
    bool committed = client.commit_batch();
    print_test(committed, "Batch committed", results);
    print_test(!client.has_pending_batch(), "No pending batch after commit", results);
    
    // Now visible
    print_test(client.exists("batch_key_0"), "Visible after commit", results);
    print_test(client.exists("batch_key_99"), "All items committed", results);
    
    // Rollback test
    client.begin_batch();
    client.batch_put("rollback_key", "rollback_value");
    client.rollback_batch();
    print_test(!client.has_pending_batch(), "Batch rolled back", results);
    print_test(!client.exists("rollback_key"), "Rollback key not persisted", results);
    
    client.close();
    cleanup_db(db_path);
}

//=============================================================================
// Test 4: Prefix iteration (2.3.4)
//=============================================================================
void test_prefix_iteration(TestResults& results) {
    std::cout << "\n--- Test: Prefix Iteration (2.3.4) ---\n";
    
    std::string db_path = get_test_db_path();
    RocksDBClient client;
    client.open(db_path);
    
    // Insert data with different prefixes
    client.put("term:apple", "doc1,doc2,doc3");
    client.put("term:banana", "doc4,doc5");
    client.put("term:apricot", "doc6");
    client.put("doc:1", "metadata1");
    client.put("doc:2", "metadata2");
    client.put("term:avocado", "doc7,doc8");
    
    // Get keys with prefix
    auto term_keys = client.get_keys_with_prefix("term:");
    print_test(term_keys.size() == 4, "Found 4 term keys", results);
    
    auto doc_keys = client.get_keys_with_prefix("doc:");
    print_test(doc_keys.size() == 2, "Found 2 doc keys", results);
    
    // Get all with prefix
    auto terms = client.get_all_with_prefix("term:a");
    print_test(terms.size() == 3, "Found 3 terms starting with 'a'", results);
    
    // Check values
    bool found_apple = false;
    for (const auto& [key, value] : terms) {
        if (key == "term:apple" && value == "doc1,doc2,doc3") {
            found_apple = true;
        }
    }
    print_test(found_apple, "Apple term has correct value", results);
    
    // Count prefix
    size_t term_count = client.count_prefix("term:");
    print_test(term_count == 4, "Count prefix works", results);
    
    // Iterate with callback
    int callback_count = 0;
    client.iterate_prefix("term:", [&callback_count](const std::string&, const std::string&) {
        callback_count++;
        return true;
    });
    print_test(callback_count == 4, "Callback iteration works", results);
    
    // Early termination
    int early_count = 0;
    client.iterate_prefix("term:", [&early_count](const std::string&, const std::string&) {
        early_count++;
        return early_count < 2;  // Stop after 2
    });
    print_test(early_count == 2, "Early termination works", results);
    
    client.close();
    cleanup_db(db_path);
}

//=============================================================================
// Test 5: Persistence
//=============================================================================
void test_persistence(TestResults& results) {
    std::cout << "\n--- Test: Persistence ---\n";
    
    std::string db_path = get_test_db_path();
    
    // Write data and close
    {
        RocksDBClient client;
        client.open(db_path);
        client.put("persistent_key", "persistent_value");
        client.close();
    }
    
    // Reopen and verify
    {
        RocksDBClient client;
        client.open(db_path);
        auto value = client.get("persistent_key");
        print_test(value.has_value(), "Data persisted after close", results);
        print_test(value.value_or("") == "persistent_value", "Value correct after reopen", results);
        client.close();
    }
    
    cleanup_db(db_path);
}

int main() {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "    RocksDB Client Test Suite - Task 2.3                    \n";
    std::cout << "============================================================\n";
    
    TestResults results;
    
    test_database_setup(results);
    test_basic_operations(results);
    test_batch_operations(results);
    test_prefix_iteration(results);
    test_persistence(results);
    
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "                    TEST SUMMARY                            \n";
    std::cout << "============================================================\n";
    std::cout << "Passed: " << results.passed << "\n";
    std::cout << "Failed: " << results.failed << "\n";
    std::cout << "\n";
    
    if (results.failed == 0) {
        std::cout << "[SUCCESS] All RocksDB client tests passed!\n\n";
        return 0;
    } else {
        std::cout << "[FAILED] Some tests failed\n\n";
        return 1;
    }
}