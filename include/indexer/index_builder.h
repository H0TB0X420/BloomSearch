#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <cstdint>
#include <optional>
#include <sstream>

#include "common/search_types.h"

namespace search {

class RocksDBClient;
class HTMLParser;
class Tokenizer;

struct IndexedDocument {
    uint64_t doc_id = 0;
    std::string url;
    std::string title;
    std::string snippet;
    uint32_t word_count = 0;
    uint64_t indexed_at = 0;
    int64_t published_at = 0;       // Original publish date (0 = unknown)
    int64_t modified_at = 0;        // Last modified date (0 = unknown)
    float ai_score = 0.0f;
    std::string era;                // "pre-ai", "post-ai", "unknown"
    
    // Format: url\ttitle\tsnippet\tword_count\tindexed_at\tpublished_at\tmodified_at\tai_score\tera
    std::string serialize() const {
        std::ostringstream oss;
        oss << url << "\t" << title << "\t" << snippet << "\t"
            << word_count << "\t" << indexed_at << "\t"
            << published_at << "\t" << modified_at << "\t"
            << ai_score << "\t" << era;
        return oss.str();
    }
    
    static IndexedDocument deserialize(const std::string& data) {
    IndexedDocument doc;
    std::istringstream iss(data);
    std::string token;
    
    std::getline(iss, doc.url, '\t');
    std::getline(iss, doc.title, '\t');
    std::getline(iss, doc.snippet, '\t');
    
    std::getline(iss, token, '\t');
    try { doc.word_count = token.empty() ? 0 : std::stoul(token); } catch (...) { doc.word_count = 0; }
    
    std::getline(iss, token, '\t');
    try { doc.indexed_at = token.empty() ? 0 : std::stoull(token); } catch (...) { doc.indexed_at = 0; }
    
    std::getline(iss, token, '\t');
    try { doc.published_at = token.empty() ? 0 : std::stoll(token); } catch (...) { doc.published_at = 0; }
    
    std::getline(iss, token, '\t');
    try { doc.modified_at = token.empty() ? 0 : std::stoll(token); } catch (...) { doc.modified_at = 0; }
    
    std::getline(iss, token, '\t');
    try { doc.ai_score = token.empty() ? 0.0f : std::stof(token); } catch (...) { doc.ai_score = 0.0f; }
    
    std::getline(iss, doc.era, '\t');
    if (doc.era.empty()) doc.era = "unknown";
    
    return doc;
}
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
                     const std::vector<std::string>& h2_tags = {},
                     int64_t published_at = 0,
                     int64_t modified_at = 0,
                     const std::string& era = "unknown");
    
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