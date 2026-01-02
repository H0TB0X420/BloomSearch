#include "storage/rocksdb/rocksdb_index_reader.h"
#include "storage/rocksdb/rocksdb_client.h"
#include "common/search_types.h"
#include "indexer/index_builder.h"
#include "query/ranker.h"
#include <iostream>
#include <filesystem>
#include <cstdlib>
#include <memory>

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

std::string get_test_db_path() {
    return "/tmp/test_index_reader_" + std::to_string(time(nullptr));
}

void cleanup_db(const std::string& path) {
    std::filesystem::remove_all(path);
}

//=============================================================================
// Test 1: Basic open/close
//=============================================================================
void test_open_close(TestResults& results) {
    std::cout << "\n--- Test: Open/Close ---\n";
    
    std::string db_path = get_test_db_path();
    
    // First create a database with IndexBuilder
    {
        IndexBuilder builder;
        bool init = builder.initialize(db_path);
        print_test(init, "IndexBuilder created database", results);
        // Builder destructor closes it
    }
    
    // Now open with IndexReader
    RocksDBIndexReader reader;
    print_test(!reader.is_open(), "Initially not open", results);
    
    bool opened = reader.open(db_path);
    print_test(opened, "Opened existing database", results);
    print_test(reader.is_open(), "is_open() returns true", results);
    
    reader.close();
    print_test(!reader.is_open(), "Closed successfully", results);
    
    // Try opening non-existent database
    RocksDBIndexReader reader2;
    bool bad_open = reader2.open("/tmp/nonexistent_db_12345");
    print_test(!bad_open, "Fails to open non-existent db", results);
    
    cleanup_db(db_path);
}

//=============================================================================
// Test 2: Read indexed documents
//=============================================================================
void test_get_document(TestResults& results) {
    std::cout << "\n--- Test: Get Document ---\n";
    
    std::string db_path = get_test_db_path();
    
    // Create and populate index
    {
        IndexBuilder builder;
        builder.initialize(db_path);
        
        // Index a test document
        builder.index_parsed(
            1,  // doc_id
            "https://example.com/test-page",
            "Test Page Title",
            "This is the body content with some words for testing the index.",
            "A test page description",
            {"Main Heading"},
            {}
        );
        
        builder.index_parsed(
            2,
            "https://www.another-site.com/article",
            "Another Article",
            "Different content here about various topics.",
            "",
            {},
            {}
        );
        
        builder.flush();
    }
    
    // Read with IndexReader
    RocksDBIndexReader reader;
    reader.open(db_path);
    
    // Get document 1
    auto doc1 = reader.get_document(1);
    print_test(doc1.has_value(), "Document 1 found", results);
    
    if (doc1) {
        print_test(doc1->doc_id == 1, "doc_id correct", results);
        print_test(doc1->url == "https://example.com/test-page", "URL correct", results);
        print_test(doc1->title == "Test Page Title", "Title correct", results);
        print_test(!doc1->snippet.empty(), "Snippet not empty", results);
        print_test(doc1->doc_length > 0, "doc_length > 0", results);
        print_test(doc1->domain == "example.com", "Domain extracted correctly", results);
        
        std::cout << "[INFO] Doc 1 - domain: " << doc1->domain 
                  << ", length: " << doc1->doc_length << "\n";
    }
    
    // Get document 2
    auto doc2 = reader.get_document(2);
    print_test(doc2.has_value(), "Document 2 found", results);
    
    if (doc2) {
        print_test(doc2->domain == "another-site.com", "www. prefix removed from domain", results);
    }
    
    // Get non-existent document
    auto doc999 = reader.get_document(999);
    print_test(!doc999.has_value(), "Non-existent doc returns nullopt", results);
    
    reader.close();
    cleanup_db(db_path);
}

//=============================================================================
// Test 3: Read postings
//=============================================================================
void test_get_postings(TestResults& results) {
    std::cout << "\n--- Test: Get Postings ---\n";
    
    std::string db_path = get_test_db_path();
    
    // Create index with known terms
    {
        IndexBuilder builder;
        builder.initialize(db_path);
        
        // Doc 1 has "bitcoin" multiple times
        builder.index_parsed(
            1,
            "https://crypto.com/bitcoin",
            "Bitcoin Price Guide",
            "Bitcoin is a cryptocurrency. Bitcoin trading is popular. Learn about bitcoin.",
            "Guide to bitcoin",
            {},
            {}
        );
        
        // Doc 2 also has "bitcoin"
        builder.index_parsed(
            2,
            "https://news.com/crypto",
            "Crypto News",
            "Bitcoin and Ethereum are leading cryptocurrencies.",
            "",
            {},
            {}
        );
        
        // Doc 3 has no "bitcoin"
        builder.index_parsed(
            3,
            "https://other.com/page",
            "Other Topic",
            "This page discusses something completely different.",
            "",
            {},
            {}
        );
        
        builder.flush();
    }
    
    // Read postings
    RocksDBIndexReader reader;
    reader.open(db_path);
    
    // Note: Terms are stemmed/normalized by the tokenizer
    // "bitcoin" should remain "bitcoin" after stemming
    auto postings = reader.get_postings("bitcoin");
    
    std::cout << "[INFO] Found " << postings.size() << " postings for 'bitcoin'\n";
    
    print_test(postings.size() >= 1, "At least one posting for 'bitcoin'", results);
    
    if (!postings.empty()) {
        // Check that postings have valid data
        bool all_valid = true;
        for (const auto& p : postings) {
            if (p.doc_id == 0 || p.frequency == 0) {
                all_valid = false;
            }
            std::cout << "[INFO] Posting: doc_id=" << p.doc_id 
                      << ", freq=" << p.frequency 
                      << ", positions=" << p.positions.size() << "\n";
        }
        print_test(all_valid, "All postings have valid doc_id and frequency", results);
    }
    
    // Check non-existent term
    auto empty_postings = reader.get_postings("xyznonexistent123");
    print_test(empty_postings.empty(), "Non-existent term returns empty", results);
    
    reader.close();
    cleanup_db(db_path);
}

//=============================================================================
// Test 4: Document frequency
//=============================================================================
void test_doc_frequency(TestResults& results) {
    std::cout << "\n--- Test: Document Frequency ---\n";
    
    std::string db_path = get_test_db_path();
    
    {
        IndexBuilder builder;
        builder.initialize(db_path);
        
        // Create 3 docs, 2 containing "test"
        builder.index_parsed(1, "http://a.com/1", "Test One", "This is a test document.", "", {}, {});
        builder.index_parsed(2, "http://a.com/2", "Test Two", "Another test here.", "", {}, {});
        builder.index_parsed(3, "http://a.com/3", "Other", "No matching term.", "", {}, {});
        
        builder.flush();
    }
    
    RocksDBIndexReader reader;
    reader.open(db_path);
    
    uint32_t df = reader.get_doc_frequency("test");
    std::cout << "[INFO] Document frequency for 'test': " << df << "\n";
    
    print_test(df >= 1, "Document frequency >= 1", results);
    
    uint32_t df_none = reader.get_doc_frequency("nonexistentterm");
    print_test(df_none == 0, "Non-existent term has df=0", results);
    
    reader.close();
    cleanup_db(db_path);
}

//=============================================================================
// Test 5: Statistics (total docs, avg length)
//=============================================================================
void test_statistics(TestResults& results) {
    std::cout << "\n--- Test: Statistics ---\n";
    
    std::string db_path = get_test_db_path();
    
    {
        IndexBuilder builder;
        builder.initialize(db_path);
        
        builder.index_parsed(1, "http://a.com/1", "Doc 1", "Short content.", "", {}, {});
        builder.index_parsed(2, "http://a.com/2", "Doc 2", "Longer content with more words in the body.", "", {}, {});
        builder.index_parsed(3, "http://a.com/3", "Doc 3", "Medium length content here.", "", {}, {});
        
        builder.flush();
        
        std::cout << "[INFO] IndexBuilder doc count: " << builder.document_count() << "\n";
    }
    
    RocksDBIndexReader reader;
    reader.open(db_path);
    
    uint64_t total_docs = reader.get_total_docs();
    float avg_length = reader.get_avg_doc_length();
    
    std::cout << "[INFO] Total docs: " << total_docs << "\n";
    std::cout << "[INFO] Avg doc length: " << avg_length << "\n";
    
    print_test(total_docs >= 1, "Total docs >= 1", results);
    print_test(avg_length > 0.0f, "Average length > 0", results);
    
    reader.close();
    cleanup_db(db_path);
}

//=============================================================================
// Test 6: Use with Ranker (integration)
//=============================================================================
void test_ranker_integration(TestResults& results) {
    std::cout << "\n--- Test: Ranker Integration ---\n";
    
    std::string db_path = get_test_db_path();
    
    // Build index
    {
        IndexBuilder builder;
        builder.initialize(db_path);
        
        builder.index_parsed(
            1,
            "https://example.com/bitcoin-guide",
            "Bitcoin Investment Guide",
            "Learn how to invest in bitcoin. Bitcoin is a cryptocurrency that has grown significantly.",
            "A guide to bitcoin investment",
            {},
            {}
        );
        
        builder.index_parsed(
            2,
            "https://example.com/ethereum",
            "Ethereum Overview",
            "Ethereum is another popular cryptocurrency platform.",
            "",
            {},
            {}
        );
        
        builder.flush();
    }
    
    // Create reader and ranker
    auto reader = std::make_shared<RocksDBIndexReader>();
    bool opened = reader->open(db_path);
    print_test(opened, "Reader opened for Ranker", results);
    
    Ranker ranker(reader);
    
    // Create a simple query
    ParsedQuery query;
    query.terms = {"bitcoin"};
    query.original_query = "bitcoin";
    
    // Execute search
    SearchResponse response = ranker.rank(query);
    
    std::cout << "[INFO] Search time: " << response.search_time_ms << " ms\n";
    std::cout << "[INFO] Results: " << response.results.size() << "\n";
    
    print_test(!response.results.empty(), "Search returned results", results);
    
    if (!response.results.empty()) {
        const auto& top = response.results[0];
        std::cout << "[INFO] Top result: " << top.doc.title << " (score: " << top.score << ")\n";
        
        print_test(top.doc.title.find("Bitcoin") != std::string::npos, 
                   "Top result is about bitcoin", results);
        print_test(top.score > 0.0f, "Score is positive", results);
    }
    
    reader->close();
    cleanup_db(db_path);
}

//=============================================================================
// Test 7: Era conversion
//=============================================================================
void test_era_conversion(TestResults& results) {
    std::cout << "\n--- Test: Era Conversion ---\n";
    
    std::string db_path = get_test_db_path();
    
    // We need to manually set era in the document
    // For now, test that UNKNOWN is returned when era not set
    {
        IndexBuilder builder;
        builder.initialize(db_path);
        
        // Index without setting era (will be empty string)
        builder.index_parsed(1, "http://a.com/1", "Test", "Content.", "", {}, {});
        builder.flush();
    }
    
    RocksDBIndexReader reader;
    reader.open(db_path);
    
    auto doc = reader.get_document(1);
    print_test(doc.has_value(), "Document retrieved", results);
    
    if (doc) {
        // Era should be UNKNOWN since we didn't set it
        print_test(doc->era == Era::UNKNOWN, "Default era is UNKNOWN", results);
        std::cout << "[INFO] Era: " << era_to_string(doc->era) << "\n";
    }
    
    reader.close();
    cleanup_db(db_path);
}

//=============================================================================
// Main
//=============================================================================
int main() {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "    RocksDBIndexReader Test Suite                           \n";
    std::cout << "============================================================\n";
    
    TestResults results;
    
    test_open_close(results);
    test_get_document(results);
    test_get_postings(results);
    test_doc_frequency(results);
    test_statistics(results);
    test_ranker_integration(results);
    test_era_conversion(results);
    
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "                    TEST SUMMARY                            \n";
    std::cout << "============================================================\n";
    std::cout << "Passed: " << results.passed << "\n";
    std::cout << "Failed: " << results.failed << "\n";
    std::cout << "\n";
    
    if (results.failed == 0) {
        std::cout << "[SUCCESS] All tests passed!\n";
        std::cout << "[SUCCESS] RocksDBIndexReader is ready for use.\n\n";
        return 0;
    } else {
        std::cout << "[INCOMPLETE] Some tests failed - review implementation.\n\n";
        return 1;
    }
}