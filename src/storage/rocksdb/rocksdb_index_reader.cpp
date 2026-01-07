#include "storage/rocksdb/rocksdb_index_reader.h"
#include "indexer/index_builder.h"
#include <algorithm>
#include <cctype>

namespace search {

//=============================================================================
// Constructor / Destructor
//=============================================================================

RocksDBIndexReader::RocksDBIndexReader() = default;

RocksDBIndexReader::~RocksDBIndexReader() {
    close();
}

//=============================================================================
// Open / Close
//=============================================================================

bool RocksDBIndexReader::open(const std::string& db_path) {
    if (db_) {
        close();
    }
    
    db_ = new RocksDBClient();
    owns_db_ = true;
    
    if (!db_->open(db_path)) {
        last_error_ = "Failed to open RocksDB: " + db_->last_error();
        delete db_;
        db_ = nullptr;
        owns_db_ = false;
        return false;
    }
    
    return true;
}

bool RocksDBIndexReader::open(RocksDBClient* client) {
    if (!client || !client->is_open()) {
        last_error_ = "Invalid or closed database";
        return false;
    }
    
    if (db_) {
        close();
    }
    
    db_ = client;
    owns_db_ = false;
    return true;
}

void RocksDBIndexReader::close() {
    if (owns_db_ && db_) {
        db_->close();
        delete db_;
    }
    db_ = nullptr;
    owns_db_ = false;
    stats_loaded_ = false;
}

//=============================================================================
// Statistics Loading
//=============================================================================

void RocksDBIndexReader::load_stats() const {
    if (stats_loaded_ || !db_) return;
    
    // Load document count - IndexBuilder uses "meta:doc_count"
    auto count_str = db_->get("meta:doc_count");
    if (count_str) {
        try {
            cached_doc_count_ = std::stoull(*count_str);
        } catch (...) {
            cached_doc_count_ = 0;
        }
    }
    
    // Fallback: count from data
    if (cached_doc_count_ == 0) {
        cached_doc_count_ = db_->count_prefix("doc:");
    }
    
    // Calculate average document length by sampling
    if (cached_doc_count_ > 0) {
        uint64_t total_length = 0;
        uint64_t sampled = 0;
        const uint64_t max_sample = 100;
        
        db_->iterate_prefix("doc:", [&]([[maybe_unused]] const std::string& key, const std::string& value) {
            if (sampled >= max_sample) return false;
            
            IndexedDocument doc = IndexedDocument::deserialize(value);
            total_length += doc.word_count;
            sampled++;
            return true;
        });
        
        if (sampled > 0) {
            cached_avg_length_ = static_cast<float>(total_length) / static_cast<float>(sampled);
        }
    }
    
    if (cached_avg_length_ <= 0.0f) {
        cached_avg_length_ = 100.0f;
    }
    
    stats_loaded_ = true;
}

//=============================================================================
// IndexReader Interface Implementation
//=============================================================================

std::vector<Posting> RocksDBIndexReader::get_postings(const std::string& term) const {
    std::vector<Posting> result;
    if (!db_) return result;
    
    auto data = db_->get(term_key(term));
    if (!data) return result;
    
    PostingList pl = PostingList::deserialize(*data);
    
    // Aggregate postings by doc_id (combine across fields)
    std::unordered_map<uint64_t, Posting> aggregated;
    
    for (const auto& posting : pl.postings) {
        auto& agg = aggregated[posting.doc_id];
        if (agg.doc_id == 0) {
            agg.doc_id = posting.doc_id;
            agg.field = posting.field;
            agg.frequency = posting.frequency;
            agg.positions = std::vector<uint32_t>(posting.positions.begin(), posting.positions.end());
        } else {
            agg.frequency += posting.frequency;
            for (auto pos : posting.positions) {
                agg.positions.push_back(pos);
            }
        }
    }
    
    for (auto& [_, posting] : aggregated) {
        std::sort(posting.positions.begin(), posting.positions.end());
        result.push_back(posting);
    }
    
    return result;
}

std::optional<DocumentInfo> RocksDBIndexReader::get_document(uint64_t doc_id) const {
    if (!db_) return std::nullopt;
    
    auto data = db_->get(doc_key(doc_id));
    if (!data) return std::nullopt;
    
    IndexedDocument stored = IndexedDocument::deserialize(*data);
    
    DocumentInfo info;
    info.doc_id = doc_id;
    info.url = stored.url;
    info.title = stored.title;
    info.snippet = stored.snippet;
    info.doc_length = stored.word_count;
    info.published_at = stored.published_at;
    info.modified_at = stored.modified_at;
    info.ai_score = stored.ai_score;
    info.era = string_to_era(stored.era);
    info.domain = extract_domain(stored.url);
    
    return info;
}

uint32_t RocksDBIndexReader::get_doc_frequency(const std::string& term) const {
    if (!db_) return 0;
    
    auto df_str = db_->get(df_key(term));
    if (df_str) {
        try {
            return static_cast<uint32_t>(std::stoul(*df_str));
        } catch (...) {
            return 0;
        }
    }
    return 0;
}

uint64_t RocksDBIndexReader::get_total_docs() const {
    load_stats();
    return cached_doc_count_;
}

float RocksDBIndexReader::get_avg_doc_length() const {
    load_stats();
    return cached_avg_length_;
}

//=============================================================================
// Helper Methods
//=============================================================================

Era RocksDBIndexReader::string_to_era(const std::string& era_str) {
    std::string lower;
    lower.reserve(era_str.size());
    for (char c : era_str) {
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    
    if (lower == "pre-ai" || lower == "pre_ai" || lower == "preai") {
        return Era::PRE_AI;
    } else if (lower == "post-ai" || lower == "post_ai" || lower == "postai" ||
               lower == "ai-era" || lower == "ai_era" || lower == "aiera") {
        return Era::AI_ERA;
    }
    
    return Era::UNKNOWN;
}

std::string RocksDBIndexReader::extract_domain(const std::string& url) {
    size_t start = url.find("://");
    if (start == std::string::npos) return "";
    start += 3;
    
    size_t end = url.find('/', start);
    std::string domain;
    if (end == std::string::npos) {
        domain = url.substr(start);
    } else {
        domain = url.substr(start, end - start);
    }
    
    // Remove www. prefix
    if (domain.substr(0, 4) == "www.") {
        domain = domain.substr(4);
    }
    
    return domain;
}

} // namespace search