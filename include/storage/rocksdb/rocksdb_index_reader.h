#pragma once

#include "query/ranker.h"           
#include "common/search_types.h"   
#include "storage/rocksdb/rocksdb_client.h"
#include <memory>
#include <string>

namespace search {

struct PostingList;
struct IndexedDocument;

/**
 * RocksDBIndexReader - Implements IndexReader interface for the Ranker
 * 
 * This adapter reads from RocksDB using the same key schema as IndexBuilder:
 *   - "term:<term>"     -> PostingList (serialized)
 *   - "doc:<doc_id>"    -> IndexedDocument (serialized)  
 *   - "df:<term>"       -> document frequency (string)
 *   - "stats:doc_count" -> total document count
 *   - "stats:avg_len"   -> average document length
 */
class RocksDBIndexReader : public IndexReader {
public:
    RocksDBIndexReader();
    ~RocksDBIndexReader() override;
    
    RocksDBIndexReader(const RocksDBIndexReader&) = delete;
    RocksDBIndexReader& operator=(const RocksDBIndexReader&) = delete;
    
    bool open(const std::string& db_path);
    
    void set_client(RocksDBClient* client);
    
    bool is_open() const;
    
    void close();
    
    std::vector<Posting> get_postings(const std::string& term) const override;
    
    std::optional<DocumentInfo> get_document(uint64_t doc_id) const override;
    
    uint32_t get_doc_frequency(const std::string& term) const override;
    
    uint64_t get_total_docs() const override;
    
    float get_avg_doc_length() const override;
    
    const std::string& last_error() const { return last_error_; }

private:
    RocksDBClient* db_ = nullptr;
    bool owns_db_ = false;
    std::string last_error_;
    
    mutable uint64_t cached_doc_count_ = 0;
    mutable float cached_avg_length_ = 100.0f;
    mutable bool stats_loaded_ = false;
    
    void load_stats() const;
    
    static std::string term_key(const std::string& term) { return "term:" + term; }
    static std::string doc_key(uint64_t doc_id) { return "doc:" + std::to_string(doc_id); }
    static std::string df_key(const std::string& term) { return "df:" + term; }
    
    static Era string_to_era(const std::string& era_str);
    static std::string extract_domain(const std::string& url);
};

} // namespace search