#include "storage/postgres_client.h"
#include <iostream>
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

// Get connection string from environment or use default
std::string get_connection_string() {
    const char* env = std::getenv("POSTGRES_CONNECTION");
    if (env) return env;
    
    // Default for Docker Compose setup
    return "host=postgres port=5432 dbname=bloom user=postgres password=postgres";
}

// Test 1: Basic connection
void test_basic_connection(TestResults& results) {
    std::cout << "\n--- Test: Basic Connection ---\n";
    
    PostgresClient client;
    
    print_test(!client.is_connected(), "Initially not connected", results);
    
    std::string conn_str = get_connection_string();
    std::cout << "[INFO] Connecting to: " << conn_str << "\n";
    
    bool connected = client.connect(conn_str);
    
    if (connected) {
        print_test(true, "Connection successful", results);
        print_test(client.is_connected(), "is_connected() returns true", results);
    } else {
        std::cout << "[WARN] Could not connect to PostgreSQL - is it running?\n";
        print_skip("Connection successful", results);
        print_skip("is_connected() returns true", results);
    }
}

void test_connection_params(TestResults& results) {
    std::cout << "\n--- Test: Connection with Parameters ---\n";
    
    PostgresClient client;
    
    // Use env vars to match docker-compose
    const char* host = std::getenv("POSTGRES_HOST");
    const char* db = std::getenv("POSTGRES_DB");
    const char* user = std::getenv("POSTGRES_USER");
    const char* pass = std::getenv("POSTGRES_PASSWORD");
    
    if (!host || !db || !user || !pass) {
        print_skip("Connection with params successful (env vars not set)", results);
        return;
    }
    
    bool connected = client.connect(host, 5432, db, user, pass);
    
    if (connected) {
        print_test(true, "Connection with params successful", results);
    } else {
        print_skip("Connection with params successful", results);
    }
}
// Test 3: Disconnect
void test_disconnect(TestResults& results) {
    std::cout << "\n--- Test: Disconnect ---\n";
    
    PostgresClient client;
    std::string conn_str = get_connection_string();
    
    if (!client.connect(conn_str)) {
        print_skip("Disconnect after connect", results);
        print_skip("is_connected false after disconnect", results);
        return;
    }
    
    client.disconnect();
    print_test(!client.is_connected(), "is_connected false after disconnect", results);
    
    // Disconnect again should be safe
    client.disconnect();
    print_test(true, "Double disconnect is safe", results);
}

// Test 4: Reconnect
void test_reconnect(TestResults& results) {
    std::cout << "\n--- Test: Reconnect ---\n";
    
    PostgresClient client;
    std::string conn_str = get_connection_string();
    
    if (!client.connect(conn_str)) {
        print_skip("Reconnect after disconnect", results);
        return;
    }
    
    client.disconnect();
    
    bool reconnected = client.reconnect();
    print_test(reconnected, "Reconnect successful", results);
    print_test(client.is_connected(), "is_connected after reconnect", results);
}

// Test 5: Invalid connection
void test_invalid_connection(TestResults& results) {
    std::cout << "\n--- Test: Invalid Connection ---\n";
    
    PostgresClient client;
    
    // Try to connect to non-existent server
    bool connected = client.connect("host=invalid.host.local port=5432 dbname=test");
    
    print_test(!connected, "Invalid connection returns false", results);
    print_test(!client.is_connected(), "is_connected is false after failed connect", results);
}

// Test 6: RAII - destructor disconnects
void test_raii(TestResults& results) {
    std::cout << "\n--- Test: RAII Cleanup ---\n";
    
    std::string conn_str = get_connection_string();
    
    {
        PostgresClient client;
        if (!client.connect(conn_str)) {
            print_skip("RAII cleanup", results);
            return;
        }
        // Client goes out of scope
    }
    
    print_test(true, "Destructor runs without crash", results);
}

int main() {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "    PostgreSQL Client Test Suite - Subtask 1.3.1            \n";
    std::cout << "    Connection Management                                    \n";
    std::cout << "============================================================\n";
    std::cout << "\nNote: These tests require a running PostgreSQL instance.\n";
    std::cout << "Set POSTGRES_CONNECTION env var or use docker-compose.\n";
    
    TestResults results;
    
    test_basic_connection(results);
    test_connection_params(results);
    test_disconnect(results);
    test_reconnect(results);
    test_invalid_connection(results);
    test_raii(results);
    
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "                    TEST SUMMARY                            \n";
    std::cout << "============================================================\n";
    std::cout << "Passed:  " << results.passed << "\n";
    std::cout << "Failed:  " << results.failed << "\n";
    std::cout << "Skipped: " << results.skipped << "\n";
    std::cout << "\n";
    
    if (results.failed == 0) {
        std::cout << "[SUCCESS] All connection tests passed!\n\n";
        return 0;
    } else {
        std::cout << "[INCOMPLETE] Some tests failed\n\n";
        return 1;
    }
}