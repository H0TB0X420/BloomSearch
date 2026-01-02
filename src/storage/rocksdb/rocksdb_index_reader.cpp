#include "storage/rocksdb/rocksdb_index_reader.h"
#include "common/search_types.h" 
#include "indexer/index_builder.h"
#include <algorithm>
#include <cctype>

namespace search {

RocksDBIndexReader::RocksDBIndexReader() = default;

RocksDBIndexReader::~RocksDBIndexReader() {
    close();
}

bool RocksDBIndexReader::open(const std::string& db_path) {
    close();
    
    auto client = new RocksDBClient();
    if (!client->open(db_path, false)) {
        last_error_ = "Failed to open RocksDB: " + client->last_error();
        delete client;
        return false;
    }
    
    db_ = client;
    owns_db_ = true;
    stats_loaded_ = false;
    
    return true;
}

void RocksDBIndexReader::set_client(RocksDBClient* client) {
    close();
    db_ = client;
    owns_db_ = false;
    stats_loaded_ = false;
}

bool RocksDBIndexReader::is_open() const {
    return db_ != nullptr && db_->is_open();
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

void RocksDBIndexReader::load_stats() const {
    if (stats_loaded_ || !db_) return;
    
    auto count_str = db_->get("stats:doc_count");
    if (count_str) {
        try {
            cached_doc_count_ = std::stoull(*count_str);
        } catch (...) {
            cached_doc_count_ = 0;
        }
    }
    
    auto avg_str = db_->get("stats:avg_doc_length");
    if (avg_str) {
        try {
            cached_avg_length_ = std::stof(*avg_str);
        } catch (...) {
            cached_avg_length_ = 100.0f;
        }
    }
    
    if (cached_doc_count_ == 0) {
        cached_doc_count_ = db_->count_prefix("doc:");
    }
    
    stats_loaded_ = true;
}

std::vector<Posting> RocksDBIndexReader::get_postings(const std::string& term) const {
    std::vector<Posting> result;
    
    if (!db_) return result;
    
    auto data = db_->get(term_key(term));
    if (!data) return result;
    
    PostingList pl = PostingList::deserialize(*data);
    
    std::unordered_map<uint64_t, Posting> doc_postings;
    
    for (const auto& stored_posting : pl.postings) {
        auto& out = doc_postings[stored_posting.doc_id];
        
        if (out.doc_id == 0) {
            out.doc_id = stored_posting.doc_id;
            out.frequency = stored_posting.frequency;
            for (uint16_t pos : stored_posting.positions) {
                out.positions.push_back(static_cast<uint32_t>(pos));
            }
        } else {
            out.frequency += stored_posting.frequency;
            for (uint16_t pos : stored_posting.positions) {
                out.positions.push_back(static_cast<uint32_t>(pos));
            }
        }
    }
    
    for (auto& [doc_id, posting] : doc_postings) {
        std::sort(posting.positions.begin(), posting.positions.end());
    }
    
    result.reserve(doc_postings.size());
    for (auto& [doc_id, posting] : doc_postings) {
        result.push_back(std::move(posting));
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
    info.ai_score = stored.ai_score;
    info.era = string_to_era(stored.era);
    info.domain = extract_domain(stored.url);
    
    return info;
}

uint32_t RocksDBIndexReader::get_doc_frequency(const std::string& term) const {
    if (!db_) return 0;
    
    auto data = db_->get(df_key(term));
    if (!data) return 0;
    
    try {
        return static_cast<uint32_t>(std::stoul(*data));
    } catch (...) {
        return 0;
    }
}

uint64_t RocksDBIndexReader::get_total_docs() const {
    load_stats();
    return cached_doc_count_;
}

float RocksDBIndexReader::get_avg_doc_length() const {
    load_stats();
    return cached_avg_length_;
}

Era RocksDBIndexReader::string_to_era(const std::string& era_str) {
    std::string lower;
    lower.reserve(era_str.size());
    for (char c : era_str) {
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    
    if (lower == "pre-ai" || lower == "pre_ai" || lower == "preai") {
        return Era::PRE_AI;
    } else if (lower == "transition" || lower == "trans") {
        return Era::TRANSITION;
    } else if (lower == "ai-era" || lower == "ai_era" || lower == "aiera") {
        return Era::AI_ERA;
    }
    
    return Era::UNKNOWN;
}

std::string RocksDBIndexReader::extract_domain(const std::string& url) {
    auto scheme_end = url.find("://");
    if (scheme_end == std::string::npos) {
        return "";
    }
    
    size_t domain_start = scheme_end + 3;
    
    size_t domain_end = url.find('/', domain_start);
    if (domain_end == std::string::npos) {
        domain_end = url.length();
    }
    
    std::string domain = url.substr(domain_start, domain_end - domain_start);
    
    if (domain.substr(0, 4) == "www.") {
        domain = domain.substr(4);
    }
    
    return domain;
}

} // namespace search