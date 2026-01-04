#include "indexer/index_builder.h"
#include "indexer/html_parser.h"
#include "indexer/tokenizer.h"
#include "storage/rocksdb/rocksdb_client.h"
#include "common/search_types.h"
#include "common/date_utils.h"
#include <sstream>
#include <chrono>
#include <algorithm>
#include <unordered_set>

namespace search {

//=============================================================================
// Posting Serialization
// Format: doc_id|field|freq|pos1,pos2,pos3
//=============================================================================
std::string Posting::serialize() const {
    std::ostringstream oss;
    oss << doc_id << "|" << static_cast<int>(field) << "|" << frequency << "|";
    
    for (size_t i = 0; i < positions.size(); ++i) {
        if (i > 0) oss << ",";
        oss << positions[i];
    }
    
    return oss.str();
}

Posting Posting::deserialize(const std::string& data) {
    Posting p;
    
    std::istringstream iss(data);
    std::string token;
    
    std::getline(iss, token, '|');
    p.doc_id = std::stoull(token);
    
    std::getline(iss, token, '|');
    p.field = static_cast<Field>(std::stoi(token));
    
    std::getline(iss, token, '|');
    p.frequency = static_cast<uint32_t>(std::stoi(token));
    
    std::getline(iss, token, '|');
    if (!token.empty()) {
        std::istringstream pos_stream(token);
        std::string pos;
        while (std::getline(pos_stream, pos, ',')) {
            if (!pos.empty()) {
                p.positions.push_back(static_cast<uint32_t>(std::stoi(pos)));
            }
        }
    }
    
    return p;
}

//=============================================================================
// PostingList Serialization
// Format: doc_freq;posting1;posting2;posting3
//=============================================================================
std::string PostingList::serialize() const {
    std::ostringstream oss;
    oss << doc_frequency;
    
    for (const auto& posting : postings) {
        oss << ";" << posting.serialize();
    }
    
    return oss.str();
}

PostingList PostingList::deserialize(const std::string& data) {
    PostingList pl;
    
    std::istringstream iss(data);
    std::string token;
    
    std::getline(iss, token, ';');
    pl.doc_frequency = std::stoul(token);
    
    while (std::getline(iss, token, ';')) {
        if (!token.empty()) {
            pl.postings.push_back(Posting::deserialize(token));
        }
    }
    
    return pl;
}

void PostingList::add_posting(const Posting& posting) {
    for (auto& existing : postings) {
        if (existing.doc_id == posting.doc_id && existing.field == posting.field) {
            existing.frequency += posting.frequency;
            existing.positions.insert(existing.positions.end(),
                                     posting.positions.begin(),
                                     posting.positions.end());
            return;
        }
    }
    
    postings.push_back(posting);
}

void PostingList::remove_document(uint64_t doc_id) {
    postings.erase(
        std::remove_if(postings.begin(), postings.end(),
                      [doc_id](const Posting& p) { return p.doc_id == doc_id; }),
        postings.end()
    );
    
    std::unordered_set<uint64_t> unique_docs;
    for (const auto& p : postings) {
        unique_docs.insert(p.doc_id);
    }
    doc_frequency = static_cast<uint32_t>(unique_docs.size());
}

//=============================================================================
// IndexBuilder
//=============================================================================

IndexBuilder::IndexBuilder()
    : parser_(std::make_unique<HTMLParser>())
    , tokenizer_(std::make_unique<Tokenizer>()) {
}

IndexBuilder::~IndexBuilder() {
    if (pending_docs_ > 0) {
        flush();
    }
    if (owns_db_ && db_) {
        delete db_;
    }
}

bool IndexBuilder::initialize(const std::string& db_path) {
    db_ = new RocksDBClient();
    owns_db_ = true;
    
    if (!db_->open(db_path)) {
        last_error_ = "Failed to open RocksDB: " + db_->last_error();
        delete db_;
        db_ = nullptr;
        owns_db_ = false;
        return false;
    }
    
    load_statistics();
    return true;
}

bool IndexBuilder::initialize(RocksDBClient* db) {
    if (!db || !db->is_open()) {
        last_error_ = "Invalid or closed database";
        return false;
    }
    
    db_ = db;
    owns_db_ = false;
    
    load_statistics();
    return true;
}

bool IndexBuilder::index_document(uint64_t doc_id,
                                  const std::string& url,
                                  const std::string& html_content) {
    if (!db_) {
        last_error_ = "Not initialized";
        return false;
    }
    
    // Parse HTML
    auto parsed = parser_->parse(html_content, url);
    
    // Extract dates from meta tags
    int64_t published_at = 0;
    int64_t modified_at = 0;
    
    if (parsed.published_date.has_value()) {
        published_at = DateUtils::parse_date(parsed.published_date.value());
    }
    if (parsed.modified_date.has_value()) {
        modified_at = DateUtils::parse_date(parsed.modified_date.value());
    }
    
    // Fallback: extract date from URL pattern
    if (published_at == 0) {
        published_at = DateUtils::extract_date_from_url(url);
    }
    
    // Determine era from best available date
    int64_t era_date = published_at > 0 ? published_at : modified_at;
    std::string era = DateUtils::era_string(era_date);
    
    std::vector<std::string> h1_tags;
    std::vector<std::string> h2_tags;
    
    return index_parsed(doc_id, url, parsed.title, parsed.text_content,
                       parsed.description, h1_tags, h2_tags,
                       published_at, modified_at, era);
}

bool IndexBuilder::index_parsed(uint64_t doc_id,
                               const std::string& url,
                               const std::string& title,
                               const std::string& body,
                               const std::string& meta_description,
                               const std::vector<std::string>& h1_tags,
                               const std::vector<std::string>& h2_tags,
                               int64_t published_at,
                               int64_t modified_at,
                               const std::string& era) {
    if (!db_) {
        last_error_ = "Not initialized";
        return false;
    }
    
    if (is_indexed(doc_id)) {
        remove_document(doc_id);
    }
    
    std::vector<std::string> doc_terms;
    
    tokenize_field(title, Field::TITLE, doc_id, doc_terms);
    tokenize_field(body, Field::BODY, doc_id, doc_terms);
    tokenize_field(meta_description, Field::META_DESC, doc_id, doc_terms);
    tokenize_field(url, Field::URL, doc_id, doc_terms);
    
    for (const auto& h1 : h1_tags) {
        tokenize_field(h1, Field::H1, doc_id, doc_terms);
    }
    for (const auto& h2 : h2_tags) {
        tokenize_field(h2, Field::H2, doc_id, doc_terms);
    }
    
    pending_doc_terms_[doc_id] = doc_terms;
    
    IndexedDocument doc_meta;
    doc_meta.doc_id = doc_id;
    doc_meta.url = url;
    doc_meta.title = title;
    doc_meta.snippet = generate_snippet(body);
    doc_meta.word_count = static_cast<uint32_t>(doc_terms.size());
    doc_meta.indexed_at = static_cast<uint64_t>(std::time(nullptr));
    doc_meta.published_at = published_at;
    doc_meta.modified_at = modified_at;
    doc_meta.ai_score = 0.0f;
    doc_meta.era = era;
    
    pending_docs_metadata_.push_back(doc_meta);
    pending_docs_++;
    
    if (pending_docs_ >= batch_size_) {
        return flush();
    }
    
    return true;
}

void IndexBuilder::tokenize_field(const std::string& text, Field field,
                                  uint64_t doc_id, std::vector<std::string>& terms_out) {
    if (text.empty()) return;
    
    auto tokens = tokenizer_->tokenize(text);
    
    std::unordered_map<std::string, Posting> term_postings;
    
    for (const auto& token : tokens) {
        auto& posting = term_postings[token.text];
        posting.doc_id = doc_id;
        posting.field = field;
        posting.frequency++;
        posting.positions.push_back(static_cast<uint32_t>(token.position));
    }
    
    for (auto& [term, posting] : term_postings) {
        add_to_pending(term, posting);
        terms_out.push_back(term);
    }
}

void IndexBuilder::add_to_pending(const std::string& term, const Posting& posting) {
    auto& pl = pending_postings_[term];
    pl.term = term;
    pl.add_posting(posting);
}

std::string IndexBuilder::generate_snippet(const std::string& text, size_t max_length) {
    if (text.length() <= max_length) {
        return text;
    }
    
    size_t end = max_length;
    while (end > max_length - 50 && end > 0) {
        char c = text[end];
        if (c == '.' || c == '!' || c == '?' || c == ' ') {
            break;
        }
        end--;
    }
    
    if (end < max_length - 50) {
        end = max_length;
    }
    
    return text.substr(0, end) + "...";
}

bool IndexBuilder::flush() {
    if (!db_ || pending_docs_ == 0) {
        return true;
    }
    
    db_->begin_batch();
    
    for (auto& [term, pending_pl] : pending_postings_) {
        std::string key = term_key(term);
        auto existing = db_->get(key);
        if (existing) {
            PostingList existing_pl = PostingList::deserialize(*existing);
            for (const auto& posting : pending_pl.postings) {
                existing_pl.add_posting(posting);
            }
            std::unordered_set<uint64_t> unique_docs;
            for (const auto& p : existing_pl.postings) {
                unique_docs.insert(p.doc_id);
            }
            existing_pl.doc_frequency = static_cast<uint32_t>(unique_docs.size());
            db_->batch_put(key, existing_pl.serialize());
            db_->batch_put(df_key(term), std::to_string(existing_pl.doc_frequency));
        } else {
            std::unordered_set<uint64_t> unique_docs;
            for (const auto& p : pending_pl.postings) {
                unique_docs.insert(p.doc_id);
            }
            pending_pl.doc_frequency = static_cast<uint32_t>(unique_docs.size());
            db_->batch_put(key, pending_pl.serialize());
            db_->batch_put(df_key(term), std::to_string(pending_pl.doc_frequency));
            term_count_++;
        }
    }
    
    for (const auto& doc : pending_docs_metadata_) {
        db_->batch_put(doc_key(doc.doc_id), doc.serialize());
        doc_count_++;
    }
    
    for (const auto& [doc_id, terms] : pending_doc_terms_) {
        std::ostringstream oss;
        for (size_t i = 0; i < terms.size(); ++i) {
            if (i > 0) oss << ",";
            oss << terms[i];
        }
        db_->batch_put(doc_terms_key(doc_id), oss.str());
    }
    
    bool success = db_->commit_batch();
    
    if (success) {
        save_statistics();
        pending_postings_.clear();
        pending_docs_metadata_.clear();
        pending_doc_terms_.clear();
        pending_docs_ = 0;
    } else {
        last_error_ = "Batch commit failed: " + db_->last_error();
    }
    
    return success;
}

uint32_t IndexBuilder::get_document_frequency(const std::string& term) {
    if (!db_) return 0;
    
    auto df = db_->get(df_key(term));
    if (df) {
        return static_cast<uint32_t>(std::stoul(*df));
    }
    return 0;
}

void IndexBuilder::load_statistics() {
    if (!db_) return;
    
    auto doc_count = db_->get("meta:doc_count");
    if (doc_count) {
        doc_count_ = std::stoull(*doc_count);
    }
    
    auto term_count = db_->get("meta:term_count");
    if (term_count) {
        term_count_ = std::stoull(*term_count);
    }
}

void IndexBuilder::save_statistics() {
    if (!db_) return;
    
    db_->put("meta:doc_count", std::to_string(doc_count_));
    db_->put("meta:term_count", std::to_string(term_count_));
}

bool IndexBuilder::remove_document(uint64_t doc_id) {
    if (!db_) {
        last_error_ = "Not initialized";
        return false;
    }
    
    auto terms_data = db_->get(doc_terms_key(doc_id));
    if (!terms_data) {
        return true;
    }
    
    std::vector<std::string> terms;
    std::istringstream iss(*terms_data);
    std::string term;
    while (std::getline(iss, term, ',')) {
        if (!term.empty()) {
            terms.push_back(term);
        }
    }
    
    db_->begin_batch();
    
    for (const auto& t : terms) {
        auto pl_data = db_->get(term_key(t));
        if (pl_data) {
            PostingList pl = PostingList::deserialize(*pl_data);
            pl.remove_document(doc_id);
            
            if (pl.postings.empty()) {
                db_->batch_delete(term_key(t));
                db_->batch_delete(df_key(t));
                if (term_count_ > 0) term_count_--;
            } else {
                db_->batch_put(term_key(t), pl.serialize());
                db_->batch_put(df_key(t), std::to_string(pl.doc_frequency));
            }
        }
    }
    
    db_->batch_delete(doc_key(doc_id));
    db_->batch_delete(doc_terms_key(doc_id));
    
    bool success = db_->commit_batch();
    
    if (success && doc_count_ > 0) {
        doc_count_--;
        save_statistics();
    }
    
    return success;
}

bool IndexBuilder::is_indexed(uint64_t doc_id) {
    if (!db_) return false;
    return db_->exists(doc_key(doc_id));
}

std::optional<IndexedDocument> IndexBuilder::get_document(uint64_t doc_id) {
    if (!db_) return std::nullopt;
    
    auto data = db_->get(doc_key(doc_id));
    if (!data) return std::nullopt;
    
    IndexedDocument doc = IndexedDocument::deserialize(*data);
    doc.doc_id = doc_id;
    return doc;
}

} // namespace search