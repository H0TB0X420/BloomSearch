#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <cstdint>
#include <optional>

#include "common/search_types.h"

namespace search {

class RocksDBClient;
class HTMLParser;
class Tokenizer;


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

class IndexBuilder {
public:
    IndexBuilder();
    ~IndexBuilder();
    
    bool initialize(const std::string& db_path);
    
    bool initialize(RocksDBClient* db);
    
    bool is_initialized() const { return db_ != nullptr; }
    
    bool index_document(uint64_t doc_id,
                       const std::string& url,
                       const std::string& html_content);
    
    bool index_parsed(uint64_t doc_id,
                     const std::string& url,
                     const std::string& title,
                     const std::string& body,
                     const std::string& meta_description = "",
                     const std::vector<std::string>& h1_tags = {},
                     const std::vector<std::string>& h2_tags = {});
    
    bool flush();
    
    void set_batch_size(size_t size) { batch_size_ = size; }
    
    uint64_t document_count() const { return doc_count_; }
    uint64_t term_count() const { return term_count_; }
    uint64_t pending_documents() const { return pending_docs_; }
    
    uint32_t get_document_frequency(const std::string& term);
    
    bool remove_document(uint64_t doc_id);
    
    bool is_indexed(uint64_t doc_id);
    
    std::optional<IndexedDocument> get_document(uint64_t doc_id);
    
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
    
    uint64_t doc_count_ = 0;
    uint64_t term_count_ = 0;
    
    std::string last_error_;
    
    void add_to_pending(const std::string& term, const Posting& posting);
    void tokenize_field(const std::string& text, Field field, uint64_t doc_id,
                       std::vector<std::string>& terms_out);
    std::string generate_snippet(const std::string& text, size_t max_length = 200);
    void load_statistics();
    void save_statistics();
    
    static std::string term_key(const std::string& term) { return "term:" + term; }
    static std::string doc_key(uint64_t doc_id) { return "doc:" + std::to_string(doc_id); }
    static std::string doc_terms_key(uint64_t doc_id) { return "docterms:" + std::to_string(doc_id); }
    static std::string df_key(const std::string& term) { return "df:" + term; }
};

} // namespace search