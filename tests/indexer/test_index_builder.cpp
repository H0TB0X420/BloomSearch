#include "indexer/index_builder.h"
#include "storage/rocksdb/rocksdb_client.h"
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

std::string get_test_db_path() {
    return "/tmp/test_index_" + std::to_string(time(nullptr));
}

void cleanup_db(const std::string& path) {
    std::filesystem::remove_all(path);
}

//=============================================================================
// Test 1: Posting serialization (2.4.1)
//=============================================================================
void test_posting_serialization(TestResults& results) {
    std::cout << "\n--- Test: Posting Serialization (2.4.1) ---\n";
    
    // Create a posting
    Posting p;
    p.doc_id = 12345;
    p.field = Field::TITLE;
    p.frequency = 3;
    p.positions = {5, 23, 67};
    
    // Serialize and deserialize
    std::string serialized = p.serialize();
    std::cout << "[INFO] Serialized: " << serialized << "\n";
    
    Posting p2 = Posting::deserialize(serialized);
    
    print_test(p2.doc_id == 12345, "doc_id preserved", results);
    print_test(p2.field == Field::TITLE, "field preserved", results);
    print_test(p2.frequency == 3, "frequency preserved", results);
    print_test(p2.positions.size() == 3, "positions count preserved", results);
    print_test(p2.positions[0] == 5 && p2.positions[1] == 23 && p2.positions[2] == 67,
               "positions values preserved", results);
}

//=============================================================================
// Test 2: Posting list operations
//=============================================================================
void test_posting_list(TestResults& results) {
    std::cout << "\n--- Test: Posting List Operations ---\n";
    
    PostingList pl;
    pl.term = "test";
    
    // Add postings from different documents
    Posting p1{.doc_id = 1, .field = Field::BODY, .frequency = 2, .positions = {10, 20}};
    Posting p2{.doc_id = 2, .field = Field::TITLE, .frequency = 1, .positions = {5}};
    Posting p3{.doc_id = 1, .field = Field::TITLE, .frequency = 1, .positions = {0}};  // Same doc, diff field
    
    pl.add_posting(p1);
    pl.add_posting(p2);
    pl.add_posting(p3);
    
    print_test(pl.postings.size() == 3, "Three postings added", results);
    
    // Serialize round-trip
    std::string serialized = pl.serialize();
    PostingList pl2 = PostingList::deserialize(serialized);
    
    print_test(pl2.postings.size() == 3, "Postings preserved after serialize", results);
    
    // Remove document
    pl.remove_document(1);
    print_test(pl.postings.size() == 1, "Document removed", results);
    print_test(pl.postings[0].doc_id == 2, "Correct document remains", results);
}

//=============================================================================
// Test 3: Document metadata serialization (2.4.4)
//=============================================================================
void test_document_serialization(TestResults& results) {
    std::cout << "\n--- Test: Document Metadata Serialization (2.4.4) ---\n";
    
    IndexedDocument doc;
    doc.doc_id = 999;
    doc.url = "https://example.com/test";
    doc.title = "Test Page Title";
    doc.snippet = "This is a test snippet...";
    doc.word_count = 1500;
    doc.indexed_at = 1704067200;  // 2024-01-01
    doc.ai_score = 0.25f;
    doc.era = "transition";
    
    std::string serialized = doc.serialize();
    std::cout << "[INFO] Serialized length: " << serialized.length() << "\n";
    
    IndexedDocument doc2 = IndexedDocument::deserialize(serialized);
    
    print_test(doc2.url == "https://example.com/test", "URL preserved", results);
    print_test(doc2.title == "Test Page Title", "Title preserved", results);
    print_test(doc2.snippet == "This is a test snippet...", "Snippet preserved", results);
    print_test(doc2.word_count == 1500, "Word count preserved", results);
    print_test(doc2.indexed_at == 1704067200, "Indexed timestamp preserved", results);
    print_test(doc2.ai_score == 0.25f, "AI score preserved", results);
    print_test(doc2.era == "transition", "Era preserved", results);
}

//=============================================================================
// Test 4: Basic indexing (2.4.2, 2.4.3)
//=============================================================================
void test_basic_indexing(TestResults& results) {
    std::cout << "\n--- Test: Basic Indexing (2.4.2, 2.4.3) ---\n";
    
    std::string db_path = get_test_db_path();
    
    IndexBuilder builder;
    bool init = builder.initialize(db_path);
    print_test(init, "Builder initialized", results);
    
    if (!init) {
        std::cout << "[INFO] Error: " << builder.last_error() << "\n";
        cleanup_db(db_path);
        return;
    }
    
    // Index a document
    bool indexed = builder.index_parsed(
        1,  // doc_id
        "https://example.com/page1",
        "Quick Brown Fox",  // title
        "The quick brown fox jumps over the lazy dog. The fox is very quick.",  // body
        "A page about foxes",  // meta description
        {"Foxes and Dogs"},  // h1
        {}  // h2
    );
    
    print_test(indexed, "Document indexed", results);
    print_test(builder.pending_documents() == 1, "Document pending", results);
    
    // Flush to database
    bool flushed = builder.flush();
    print_test(flushed, "Flush succeeded", results);
    print_test(builder.pending_documents() == 0, "Pending cleared", results);
    print_test(builder.document_count() == 1, "Document count = 1", results);
    
    // Check document is indexed
    print_test(builder.is_indexed(1), "Document 1 is indexed", results);
    print_test(!builder.is_indexed(999), "Document 999 not indexed", results);
    
    // Get document metadata
    auto doc = builder.get_document(1);
    print_test(doc.has_value(), "Document metadata retrieved", results);
    if (doc) {
        print_test(doc->url == "https://example.com/page1", "URL correct", results);
        print_test(doc->title == "Quick Brown Fox", "Title correct", results);
        print_test(doc->word_count > 0, "Word count > 0", results);
    }
    
    cleanup_db(db_path);
}

//=============================================================================
// Test 5: TF-IDF preparation (2.4.5) FIXME
//=============================================================================
void test_tfidf_preparation(TestResults& results) {
    std::cout << "\n--- Test: TF-IDF Preparation (2.4.5) ---\n";
    
    std::string db_path = get_test_db_path();
    
    IndexBuilder builder;
    builder.initialize(db_path);
    builder.set_batch_size(10);  // Small batch for testing
    
    // Index multiple documents with overlapping terms
    builder.index_parsed(1, "http://1.com", "Fox Story", "The fox runs fast", "", {}, {});
    builder.index_parsed(2, "http://2.com", "Dog Story", "The dog runs slow", "", {}, {});
    builder.index_parsed(3, "http://3.com", "Fox and Dog", "The fox and dog play", "", {}, {});
    builder.flush();
    
    
    cleanup_db(db_path);
}

//=============================================================================
// Test 6: Replace strategy (2.4.6)
//=============================================================================
void test_replace_strategy(TestResults& results) {
    std::cout << "\n--- Test: Replace Strategy (2.4.6) ---\n";
    
    std::string db_path = get_test_db_path();
    
    IndexBuilder builder;
    builder.initialize(db_path);
    
    // Index original document
    builder.index_parsed(1, "http://1.com", "Bitcoin Article", 
                        "Bitcoin is a cryptocurrency. Bitcoin price is rising.", "", {}, {});
    builder.flush();
    
    print_test(builder.document_count() == 1, "Doc count still 1", results);
    
    // Verify document metadata updated
    auto doc = builder.get_document(1);
    print_test(doc.has_value(), "Document still exists", results);
    if (doc) {
        print_test(doc->title == "Ethereum Article", "Title updated", results);
    }
    
    cleanup_db(db_path);
}

//=============================================================================
// Test 7: HTML indexing (end-to-end)
//=============================================================================
void test_html_indexing(TestResults& results) {
    std::cout << "\n--- Test: HTML Indexing (End-to-End) ---\n";
    
    std::string db_path = get_test_db_path();
    
    IndexBuilder builder;
    builder.initialize(db_path);
    
    std::string html = R"(
        <!DOCTYPE html>
        <html>
        <head>
            <title>Search Engine Tutorial</title>
            <meta name="description" content="Learn how to build a search engine">
        </head>
        <body>
            <h1>Building a Search Engine</h1>
            <p>Search engines use inverted indexes to find documents quickly.</p>
            <p>The index maps terms to document IDs.</p>
        </body>
        </html>
    )";
    
    bool indexed = builder.index_document(42, "https://example.com/tutorial", html);
    print_test(indexed, "HTML document indexed", results);
    
    builder.flush();
    
    // Check document was indexed
    print_test(builder.is_indexed(42), "Document 42 indexed", results);
    
    auto doc = builder.get_document(42);
    print_test(doc.has_value(), "Document metadata exists", results);
    if (doc) {
        std::cout << "[INFO] Title: " << doc->title << "\n";
        std::cout << "[INFO] Snippet: " << doc->snippet.substr(0, 50) << "...\n";
        std::cout << "[INFO] Word count: " << doc->word_count << "\n";
        
        print_test(doc->title == "Search Engine Tutorial", "Title extracted", results);
        print_test(doc->word_count > 0, "Words counted", results);
    }
    
    cleanup_db(db_path);
}

int main() {
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "    Index Builder Test Suite - Task 2.4                     \n";
    std::cout << "============================================================\n";
    
    TestResults results;
    
    test_posting_serialization(results);
    test_posting_list(results);
    test_document_serialization(results);
    test_basic_indexing(results);
    test_tfidf_preparation(results);
    test_replace_strategy(results);
    test_html_indexing(results);
    
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "                    TEST SUMMARY                            \n";
    std::cout << "============================================================\n";
    std::cout << "Passed: " << results.passed << "\n";
    std::cout << "Failed: " << results.failed << "\n";
    std::cout << "\n";
    
    if (results.failed == 0) {
        std::cout << "[SUCCESS] All index builder tests passed!\n\n";
        return 0;
    } else {
        std::cout << "[FAILED] Some tests failed\n\n";
        return 1;
    }
}