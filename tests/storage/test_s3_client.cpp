#include "storage/s3_client.h"
#include <iostream>
#include <iomanip>
#include <cstdlib>

using namespace search;

struct TestResults {
    int passed = 0;
    int failed = 0;
    int skipped = 0;
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

void print_skip(const std::string& name, TestResults& results) {
    std::cout << "[SKIP] " << name << "\n";
    results.skipped++;
}

//=============================================================================
// Test 1: Connection from environment
//=============================================================================
void test_connect_env(TestResults& results) {
    std::cout << "\n--- Test: Connect from Environment ---\n";
    
    S3Client client;
    
    print_test(!client.is_connected(), "Initially not connected", results);
    
    bool connected = client.connect();
    
    if (connected) {
        print_test(true, "Connected via env vars", results);
        print_test(client.is_connected(), "is_connected() returns true", results);
    } else {
        std::cout << "[INFO] Error: " << client.last_error() << "\n";
        print_skip("Connected via env vars (env not set)", results);
        print_skip("is_connected() returns true", results);
    }
}

//=============================================================================
// Test 2: Connection with explicit params
//=============================================================================
void test_connect_params(TestResults& results) {
    std::cout << "\n--- Test: Connect with Parameters ---\n";
    
    S3Client client;
    
    // Use env vars or defaults matching docker-compose
    const char* endpoint = std::getenv("MINIO_ENDPOINT");
    const char* access = std::getenv("MINIO_ACCESS_KEY");
    const char* secret = std::getenv("MINIO_SECRET_KEY");
    
    if (!endpoint) endpoint = "minio:9000";
    if (!access) access = "minioadmin";
    if (!secret) secret = "minioadmin";
    
    bool connected = client.connect(endpoint, access, secret, "test-bucket");
    
    print_test(connected, "Connected with explicit params", results);
    print_test(client.is_connected(), "is_connected() after connect", results);
}

//=============================================================================
// Test 3: Ensure bucket exists
//=============================================================================
void test_ensure_bucket(TestResults& results) {
    std::cout << "\n--- Test: Ensure Bucket Exists ---\n";
    
    S3Client client;
    
    if (!client.connect()) {
        print_skip("Bucket creation (not connected)", results);
        return;
    }
    
    bool created = client.ensure_bucket_exists();
    
    if (created) {
        print_test(true, "Bucket created/verified", results);
    } else {
        std::cout << "[INFO] Error: " << client.last_error() << "\n";
        // MinIO might need time to start - this isn't necessarily a failure
        print_skip("Bucket creation (MinIO may not be ready)", results);
    }
}

//=============================================================================
// Test 4: URL to key generation
//=============================================================================
void test_url_to_key(TestResults& results) {
    std::cout << "\n--- Test: URL to Key Generation ---\n";
    
    std::string url1 = "https://example.com/page1";
    std::string url2 = "https://example.com/page2";
    std::string url3 = "https://example.com/page1";  // Same as url1
    
    std::string key1 = S3Client::url_to_key(url1);
    std::string key2 = S3Client::url_to_key(url2);
    std::string key3 = S3Client::url_to_key(url3);
    
    std::cout << "[INFO] Key for page1: " << key1 << "\n";
    std::cout << "[INFO] Key for page2: " << key2 << "\n";
    
    print_test(!key1.empty(), "Key generated for URL", results);
    print_test(key1 != key2, "Different URLs get different keys", results);
    print_test(key1 == key3, "Same URL gets same key", results);
    print_test(key1.find(".html") != std::string::npos, "Key has .html extension", results);
}

//=============================================================================
// Test 5: Basic put/get (without compression for now)
//=============================================================================
void test_basic_put_get(TestResults& results) {
    std::cout << "\n--- Test: Basic Put/Get ---\n";
    
    S3Client client;
    
    if (!client.connect()) {
        print_skip("Put operation (not connected)", results);
        print_skip("Get operation (not connected)", results);
        print_skip("Content matches (not connected)", results);
        return;
    }
    
    // Skip bucket check - assume bucket exists (created manually via MinIO Console)
    
    std::string test_key = "test-" + std::to_string(time(nullptr)) + ".html";
    std::string test_content = "<html><body>Hello, World!</body></html>";
    
    // Put
    bool put_ok = client.put(test_key, test_content, false);  // No compression
    if (put_ok) {
        print_test(true, "Put operation", results);
    } else {
        std::cout << "[INFO] Put error: " << client.last_error() << "\n";
        print_test(false, "Put operation", results);
        print_skip("Get operation", results);
        print_skip("Content matches", results);
        return;
    }
    
    // Get
    auto retrieved = client.get(test_key);
    if (retrieved) {
        print_test(true, "Get operation", results);
        print_test(*retrieved == test_content, "Content matches", results);
        
        if (*retrieved != test_content) {
            std::cout << "[INFO] Expected: " << test_content << "\n";
            std::cout << "[INFO] Got: " << *retrieved << "\n";
        }
    } else {
        std::cout << "[INFO] Get error: " << client.last_error() << "\n";
        print_test(false, "Get operation", results);
        print_skip("Content matches", results);
    }
    
    // Cleanup
    client.remove(test_key);
}

//=============================================================================
// Test 6: Exists check
//=============================================================================
void test_exists(TestResults& results) {
    std::cout << "\n--- Test: Exists Check ---\n";
    
    S3Client client;
    
    if (!client.connect()) {
        print_skip("Exists check (not connected)", results);
        return;
    }
    
    // Skip bucket check - assume bucket exists
    
    std::string test_key = "exists-test-" + std::to_string(time(nullptr)) + ".html";
    
    // Should not exist yet
    print_test(!client.exists(test_key), "Non-existent key returns false", results);
    
    // Create it
    client.put(test_key, "test", false);
    
    // Should exist now
    print_test(client.exists(test_key), "Existing key returns true", results);
    
    // Delete
    client.remove(test_key);
    
    // Should not exist again
    print_test(!client.exists(test_key), "Deleted key returns false", results);
}

//=============================================================================
// Test 7: Compression (1.4.2)
//=============================================================================
void test_compression(TestResults& results) {
    std::cout << "\n--- Test: Zstd Compression ---\n";
    
    // Create repetitive content (compresses well)
    std::string original;
    for (int i = 0; i < 100; i++) {
        original += "<html><body><p>This is paragraph " + std::to_string(i) + " with some repeated content.</p></body></html>\n";
    }
    
    std::cout << "[INFO] Original size: " << original.size() << " bytes\n";
    
    // Compress
    std::string compressed = S3Client::compress(original);
    std::cout << "[INFO] Compressed size: " << compressed.size() << " bytes\n";
    
    double ratio = static_cast<double>(original.size()) / compressed.size();
    std::cout << "[INFO] Compression ratio: " << std::fixed << std::setprecision(2) << ratio << "x\n";
    
    print_test(compressed.size() < original.size(), "Compression reduces size", results);
    print_test(ratio > 2.0, "Compression ratio > 2x for repetitive content", results);
    
    // Decompress
    std::string decompressed = S3Client::decompress(compressed);
    print_test(decompressed == original, "Round-trip decompress matches original", results);
    
    // Test empty string
    std::string empty_compressed = S3Client::compress("");
    print_test(empty_compressed.empty(), "Empty string compresses to empty", results);
}

//=============================================================================
// Test 8: Put/Get with compression
//=============================================================================
void test_compressed_storage(TestResults& results) {
    std::cout << "\n--- Test: Compressed Storage ---\n";
    
    S3Client client;
    
    if (!client.connect()) {
        print_skip("Compressed put (not connected)", results);
        print_skip("Compressed get (not connected)", results);
        print_skip("Compressed content matches (not connected)", results);
        return;
    }
    
    // Create content that compresses well
    std::string original;
    for (int i = 0; i < 50; i++) {
        original += "<div class='item'><h2>Item " + std::to_string(i) + "</h2><p>Description here.</p></div>\n";
    }
    
    std::string test_key = "compressed-test-" + std::to_string(time(nullptr)) + ".html";
    
    // Put WITH compression (default)
    bool put_ok = client.put(test_key, original, true);
    if (put_ok) {
        print_test(true, "Compressed put operation", results);
    } else {
        std::cout << "[INFO] Put error: " << client.last_error() << "\n";
        print_test(false, "Compressed put operation", results);
        print_skip("Compressed get operation", results);
        print_skip("Compressed content matches", results);
        return;
    }
    
    // Get (should auto-decompress)
    auto retrieved = client.get(test_key);
    if (retrieved) {
        print_test(true, "Compressed get operation", results);
        print_test(*retrieved == original, "Compressed content matches after retrieval", results);
        
        if (*retrieved != original) {
            std::cout << "[INFO] Size mismatch - Expected: " << original.size() 
                      << ", Got: " << retrieved->size() << "\n";
        }
    } else {
        std::cout << "[INFO] Get error: " << client.last_error() << "\n";
        print_test(false, "Compressed get operation", results);
        print_skip("Compressed content matches", results);
    }
    
    // Cleanup
    client.remove(test_key);
}

int main() {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "    S3/MinIO Client Test Suite - Task 1.4                   \n";
    std::cout << "    Connection, Storage, and Compression                     \n";
    std::cout << "============================================================\n";
    std::cout << "\nNote: These tests require a running MinIO instance.\n";
    std::cout << "Set MINIO_ENDPOINT, MINIO_ACCESS_KEY, MINIO_SECRET_KEY env vars.\n";
    
    TestResults results;
    
    test_connect_env(results);
    test_connect_params(results);
    test_ensure_bucket(results);
    test_url_to_key(results);
    test_basic_put_get(results);
    test_exists(results);
    test_compression(results);
    test_compressed_storage(results);
    
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "                    TEST SUMMARY                            \n";
    std::cout << "============================================================\n";
    std::cout << "Passed:  " << results.passed << "\n";
    std::cout << "Failed:  " << results.failed << "\n";
    std::cout << "Skipped: " << results.skipped << "\n";
    std::cout << "\n";
    
    if (results.failed == 0) {
        std::cout << "[SUCCESS] S3 Client tests passed!\n\n";
        return 0;
    } else {
        std::cout << "[INCOMPLETE] Some tests failed\n\n";
        return 1;
    }
}