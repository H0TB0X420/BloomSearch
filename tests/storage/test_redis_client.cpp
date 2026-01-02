#include "storage/redis_client.h"
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
// Test 1: Connection (2.5.1)
//=============================================================================
void test_connection(RedisClient& client, TestResults& results) {
    std::cout << "\n--- Test: Connection (2.5.1) ---\n";
    
    print_test(client.is_connected(), "Client connected", results);
    print_test(client.ping(), "Ping successful", results);
}

//=============================================================================
// Test 2: Basic operations (2.5.2)
//=============================================================================
void test_basic_operations(RedisClient& client, TestResults& results) {
    std::cout << "\n--- Test: Basic Operations (2.5.2) ---\n";
    
    // Clean up any existing test keys
    client.del("test:key1");
    client.del("test:key2");
    
    // Set
    bool set_ok = client.set("test:key1", "value1");
    print_test(set_ok, "SET operation", results);
    
    // Get
    auto value = client.get("test:key1");
    print_test(value.has_value(), "GET returns value", results);
    print_test(value.value_or("") == "value1", "Value matches", results);
    
    // Get non-existent
    auto missing = client.get("test:nonexistent");
    print_test(!missing.has_value(), "GET missing returns nullopt", results);
    
    // Exists
    print_test(client.exists("test:key1"), "EXISTS returns true", results);
    print_test(!client.exists("test:nonexistent"), "EXISTS returns false for missing", results);
    
    // Delete
    bool del_ok = client.del("test:key1");
    print_test(del_ok, "DEL operation", results);
    print_test(!client.exists("test:key1"), "Key gone after DEL", results);
}

//=============================================================================
// Test 3: TTL operations
//=============================================================================
void test_ttl_operations(RedisClient& client, TestResults& results) {
    std::cout << "\n--- Test: TTL Operations ---\n";
    
    // Set with TTL
    bool set_ok = client.set("test:ttl_key", "ttl_value", 10);
    print_test(set_ok, "SET with TTL", results);
    
    // Check TTL
    int key_ttl = client.ttl("test:ttl_key");
    std::cout << "[INFO] TTL: " << key_ttl << " seconds\n";
    print_test(key_ttl > 0 && key_ttl <= 10, "TTL in expected range", results);
    
    // Set TTL on existing key
    client.set("test:expire_key", "value");
    bool expire_ok = client.expire("test:expire_key", 5);
    print_test(expire_ok, "EXPIRE operation", results);
    
    int expire_ttl = client.ttl("test:expire_key");
    print_test(expire_ttl > 0 && expire_ttl <= 5, "EXPIRE TTL set", results);
    
    // TTL on key without expiry
    client.set("test:no_ttl", "value");
    int no_ttl = client.ttl("test:no_ttl");
    print_test(no_ttl == -1, "No TTL returns -1", results);
    
    // TTL on non-existent key
    int missing_ttl = client.ttl("test:missing_key");
    print_test(missing_ttl == -2, "Missing key TTL returns -2", results);
    
    // Cleanup
    client.del("test:ttl_key");
    client.del("test:expire_key");
    client.del("test:no_ttl");
}

//=============================================================================
// Test 4: Batch operations
//=============================================================================
void test_batch_operations(RedisClient& client, TestResults& results) {
    std::cout << "\n--- Test: Batch Operations ---\n";
    
    // MSET
    std::vector<std::pair<std::string, std::string>> pairs = {
        {"test:batch1", "value1"},
        {"test:batch2", "value2"},
        {"test:batch3", "value3"}
    };
    
    bool mset_ok = client.mset(pairs);
    print_test(mset_ok, "MSET operation", results);
    
    // MGET
    std::vector<std::string> keys = {"test:batch1", "test:batch2", "test:missing", "test:batch3"};
    auto values = client.mget(keys);
    
    print_test(values.size() == 4, "MGET returns correct count", results);
    print_test(values[0].value_or("") == "value1", "MGET value 1", results);
    print_test(values[1].value_or("") == "value2", "MGET value 2", results);
    print_test(!values[2].has_value(), "MGET missing is nullopt", results);
    print_test(values[3].value_or("") == "value3", "MGET value 3", results);
    
    // DEL multiple
    std::vector<std::string> del_keys = {"test:batch1", "test:batch2", "test:batch3"};
    int deleted = client.del(del_keys);
    print_test(deleted == 3, "DEL multiple keys", results);
}

//=============================================================================
// Test 5: Cache helpers (2.5.3)
//=============================================================================
void test_cache_helpers(RedisClient& client, TestResults& results) {
    std::cout << "\n--- Test: Cache Helpers (2.5.3) ---\n";
    
    // Clean up
    client.del("test:counter");
    client.del("test:getorset");
    
    // INCR
    int64_t count1 = client.incr("test:counter");
    print_test(count1 == 1, "INCR from 0", results);
    
    int64_t count2 = client.incr("test:counter");
    print_test(count2 == 2, "INCR again", results);
    
    // INCRBY
    int64_t count3 = client.incrby("test:counter", 10);
    print_test(count3 == 12, "INCRBY 10", results);
    
    // get_or_set
    int generator_calls = 0;
    auto generator = [&generator_calls]() {
        generator_calls++;
        return "generated_value";
    };
    
    std::string val1 = client.get_or_set("test:getorset", 60, generator);
    print_test(val1 == "generated_value", "get_or_set generates value", results);
    print_test(generator_calls == 1, "Generator called once", results);
    
    std::string val2 = client.get_or_set("test:getorset", 60, generator);
    print_test(val2 == "generated_value", "get_or_set returns cached", results);
    print_test(generator_calls == 1, "Generator not called again", results);
    
    // Cleanup
    client.del("test:counter");
    client.del("test:getorset");
}

//=============================================================================
// Test 6: Key patterns
//=============================================================================
void test_key_patterns(RedisClient& client, TestResults& results) {
    std::cout << "\n--- Test: Key Patterns ---\n";
    
    // Set up test keys
    client.set("test:pattern:a", "1");
    client.set("test:pattern:b", "2");
    client.set("test:pattern:c", "3");
    client.set("test:other:x", "4");
    
    // KEYS
    auto matching = client.keys("test:pattern:*");
    print_test(matching.size() == 3, "KEYS finds matching keys", results);
    
    // DEL pattern
    int deleted = client.del_pattern("test:pattern:*");
    print_test(deleted == 3, "DEL pattern deletes matching", results);
    
    // Verify deletion
    print_test(!client.exists("test:pattern:a"), "Pattern key deleted", results);
    print_test(client.exists("test:other:x"), "Non-matching key preserved", results);
    
    // Cleanup
    client.del("test:other:x");
}

//=============================================================================
// Test 7: Cache key generators
//=============================================================================
void test_key_generators(TestResults& results) {
    std::cout << "\n--- Test: Cache Key Generators ---\n";
    
    std::string robots = RedisClient::robots_key("example.com");
    print_test(robots == "robots:example.com", "robots_key format", results);
    
    std::string query = RedisClient::query_key("abc123");
    print_test(query == "query:abc123", "query_key format", results);
    
    std::string posting = RedisClient::posting_key("search");
    print_test(posting == "posting:search", "posting_key format", results);
    
    // TTL constants
    print_test(RedisClient::ROBOTS_TTL == 3600, "ROBOTS_TTL = 1 hour", results);
    print_test(RedisClient::QUERY_TTL == 300, "QUERY_TTL = 5 min", results);
    print_test(RedisClient::POSTING_TTL == 600, "POSTING_TTL = 10 min", results);
}

int main() {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "    Redis Client Test Suite - Task 2.5                      \n";
    std::cout << "============================================================\n";
    
    TestResults results;
    
    // Connect to Redis
    RedisClient client;
    if (!client.connect()) {
        std::cout << "[ERROR] Failed to connect to Redis: " << client.last_error() << "\n";
        std::cout << "[INFO] Make sure Redis is running (docker-compose up -d redis)\n";
        return 1;
    }
    
    std::cout << "[INFO] Connected to Redis\n";
    
    test_connection(client, results);
    test_basic_operations(client, results);
    test_ttl_operations(client, results);
    test_batch_operations(client, results);
    test_cache_helpers(client, results);
    test_key_patterns(client, results);
    test_key_generators(results);
    
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "                    TEST SUMMARY                            \n";
    std::cout << "============================================================\n";
    std::cout << "Passed: " << results.passed << "\n";
    std::cout << "Failed: " << results.failed << "\n";
    std::cout << "\n";
    
    if (results.failed == 0) {
        std::cout << "[SUCCESS] All Redis client tests passed!\n\n";
        return 0;
    } else {
        std::cout << "[FAILED] Some tests failed\n\n";
        return 1;
    }
}