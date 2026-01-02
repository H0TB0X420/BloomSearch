#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <cstdint>
#include <optional>

namespace search {

// Forward declarations
class RocksDBClient;
class HTMLParser;
class Tokenizer;

//=============================================================================
// Field types for boosting
//=============================================================================
enum class Field : uint8_t {
    BODY = 0,
    TITLE = 1,
    H1 = 2,
    H2 = 3,
    URL = 4,
    META_DESC = 5,
    ANCHOR = 6  // Anchor text from incoming links
};

// Field boost multipliers (applied at query time)
constexpr float FIELD_BOOSTS[] = {
    1.0f,   // BODY
    10.0f,  // TITLE
    5.0f,   // H1
    3.0f,   // H2
    5.0f,   // URL
    2.0f,   // META_DESC
    4.0f    // ANCHOR
};

//=============================================================================
// Single posting entry (one term occurrence in one document)
//=============================================================================
struct Posting {
    uint64_t doc_id;
    Field field;
    uint16_t frequency;                 // How many times in this field
    std::vector<uint16_t> positions;    // Positions within field
    
    // Serialization
    std::string serialize() const;
    static Posting deserialize(const std::string& data);
};

//=============================================================================
// Posting list (all occurrences of a term across documents)
//=============================================================================
struct PostingList {
    std::string term;
    uint32_t doc_frequency;             // Number of docs containing term
    std::vector<Posting> postings;
    
    // Add a posting, merging if doc_id+field already exists
    void add_posting(const Posting& posting);
    
    // Remove all postings for a document
    void remove_document(uint64_t doc_id);
    
    // Serialization
    std::string serialize() const;
    static PostingList deserialize(const std::string& data);
};

//=============================================================================
// Document metadata stored in index
//=============================================================================
struct IndexedDocument {
    uint64_t doc_id;
    std::string url;
    std::string title;
    std::string snippet;                // First ~200 chars for search results
    uint32_t word_count;
    uint64_t indexed_at;                // Unix timestamp
    float ai_score;                     // From AI detector (Week 3)
    std::string era;                    // "pre-ai", "transition", "ai-era"
    
    // Serialization
    std::string serialize() const;
    static IndexedDocument deserialize(const std::string& data);
};

//=============================================================================
// Index Builder - builds inverted index from documents
//=============================================================================
class IndexBuilder {
public:
    IndexBuilder();
    ~IndexBuilder();
    
    //=========================================================================
    // Setup
    //=========================================================================
    
    // Initialize with RocksDB path
    bool initialize(const std::string& db_path);
    
    // Use existing RocksDB client
    bool initialize(RocksDBClient* db);
    
    bool is_initialized() const { return db_ != nullptr; }
    
    //=========================================================================
    // Document Processing (2.4.2)
    //=========================================================================
    
    // Index a document (main entry point)
    bool index_document(uint64_t doc_id,
                       const std::string& url,
                       const std::string& html_content);
    
    // Index pre-parsed content (for testing or custom parsing)
    bool index_parsed(uint64_t doc_id,
                     const std::string& url,
                     const std::string& title,
                     const std::string& body,
                     const std::string& meta_description = "",
                     const std::vector<std::string>& h1_tags = {},
                     const std::vector<std::string>& h2_tags = {});
    
    //=========================================================================
    // Batch Operations (2.4.3)
    //=========================================================================
    
    // Flush pending writes to database
    bool flush();
    
    // Set batch size (flush after N documents)
    void set_batch_size(size_t size) { batch_size_ = size; }
    
    //=========================================================================
    // Statistics (2.4.5)
    //=========================================================================
    
    uint64_t document_count() const { return doc_count_; }
    uint64_t term_count() const { return term_count_; }
    uint64_t pending_documents() const { return pending_docs_; }
    
    // Get document frequency for a term (for IDF calculation)
    uint32_t get_document_frequency(const std::string& term);
    
    //=========================================================================
    // Document Management (2.4.6)
    //=========================================================================
    
    // Remove a document from index (for re-indexing)
    bool remove_document(uint64_t doc_id);
    
    // Check if document is indexed
    bool is_indexed(uint64_t doc_id);
    
    // Get document metadata
    std::optional<IndexedDocument> get_document(uint64_t doc_id);
    
    //=========================================================================
    // Utility
    //=========================================================================
    
    const std::string& last_error() const { return last_error_; }

private:
    RocksDBClient* db_ = nullptr;
    bool owns_db_ = false;
    
    std::unique_ptr<HTMLParser> parser_;
    std::unique_ptr<Tokenizer> tokenizer_;
    
    // In-memory buffer for batch writes
    std::unordered_map<std::string, PostingList> pending_postings_;
    std::vector<IndexedDocument> pending_docs_metadata_;
    std::unordered_map<uint64_t, std::vector<std::string>> pending_doc_terms_;
    
    size_t batch_size_ = 100;
    size_t pending_docs_ = 0;
    
    // Statistics
    uint64_t doc_count_ = 0;
    uint64_t term_count_ = 0;
    
    std::string last_error_;
    
    // Internal helpers
    void add_to_pending(const std::string& term, const Posting& posting);
    void tokenize_field(const std::string& text, Field field, uint64_t doc_id,
                       std::vector<std::string>& terms_out);
    std::string generate_snippet(const std::string& text, size_t max_length = 200);
    void load_statistics();
    void save_statistics();
    
    // Key generation
    static std::string term_key(const std::string& term) { return "term:" + term; }
    static std::string doc_key(uint64_t doc_id) { return "doc:" + std::to_string(doc_id); }
    static std::string doc_terms_key(uint64_t doc_id) { return "docterms:" + std::to_string(doc_id); }
    static std::string df_key(const std::string& term) { return "df:" + term; }
};

} // namespace search