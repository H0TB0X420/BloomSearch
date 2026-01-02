#include "indexer/index_builder.h"
#include "indexer/html_parser.h"
#include "indexer/tokenizer.h"
#include "storage/rocksdb_client.h"

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
    
    // doc_id
    std::getline(iss, token, '|');
    p.doc_id = std::stoull(token);
    
    // field
    std::getline(iss, token, '|');
    p.field = static_cast<Field>(std::stoi(token));
    
    // frequency
    std::getline(iss, token, '|');
    p.frequency = static_cast<uint16_t>(std::stoi(token));
    
    // positions
    std::getline(iss, token, '|');
    if (!token.empty()) {
        std::istringstream pos_stream(token);
        std::string pos;
        while (std::getline(pos_stream, pos, ',')) {
            if (!pos.empty()) {
                p.positions.push_back(static_cast<uint16_t>(std::stoi(pos)));
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
    
    // doc_frequency
    std::getline(iss, token, ';');
    pl.doc_frequency = std::stoul(token);
    
    // postings
    while (std::getline(iss, token, ';')) {
        if (!token.empty()) {
            pl.postings.push_back(Posting::deserialize(token));
        }
    }
    
    return pl;
}

void PostingList::add_posting(const Posting& posting) {
    // Check if we already have a posting for this doc_id + field
    for (auto& existing : postings) {
        if (existing.doc_id == posting.doc_id && existing.field == posting.field) {
            // Merge positions
            existing.frequency += posting.frequency;
            existing.positions.insert(existing.positions.end(),
                                     posting.positions.begin(),
                                     posting.positions.end());
            return;
        }
    }
    
    // New posting
    postings.push_back(posting);
}

void PostingList::remove_document(uint64_t doc_id) {
    postings.erase(
        std::remove_if(postings.begin(), postings.end(),
                      [doc_id](const Posting& p) { return p.doc_id == doc_id; }),
        postings.end()
    );
    
    // Recalculate doc_frequency
    std::unordered_set<uint64_t> unique_docs;
    for (const auto& p : postings) {
        unique_docs.insert(p.doc_id);
    }
    doc_frequency = static_cast<uint32_t>(unique_docs.size());
}

//=============================================================================
// IndexedDocument Serialization
// Format: url\ttitle\tsnippet\tword_count\tindexed_at\tai_score\tera
//=============================================================================
std::string IndexedDocument::serialize() const {
    std::ostringstream oss;
    oss << url << "\t" << title << "\t" << snippet << "\t"
        << word_count << "\t" << indexed_at << "\t"
        << ai_score << "\t" << era;
    return oss.str();
}

IndexedDocument IndexedDocument::deserialize(const std::string& data) {
    IndexedDocument doc;
    
    std::istringstream iss(data);
    std::string token;
    
    std::getline(iss, doc.url, '\t');
    std::getline(iss, doc.title, '\t');
    std::getline(iss, doc.snippet, '\t');
    
    std::getline(iss, token, '\t');
    doc.word_count = token.empty() ? 0 : std::stoul(token);
    
    std::getline(iss, token, '\t');
    doc.indexed_at = token.empty() ? 0 : std::stoull(token);
    
    std::getline(iss, token, '\t');
    doc.ai_score = token.empty() ? 0.0f : std::stof(token);
    
    std::getline(iss, doc.era, '\t');
    
    return doc;
}

//=============================================================================
// IndexBuilder Constructor/Destructor
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

//=============================================================================
// Setup
//=============================================================================
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

//=============================================================================
// Document Processing (2.4.2)
//=============================================================================
bool IndexBuilder::index_document(uint64_t doc_id,
                                  const std::string& url,
                                  const std::string& html_content) {
    if (!db_) {
        last_error_ = "Not initialized";
        return false;
    }
    
    // Parse HTML
    auto parsed = parser_->parse(html_content, url);
    
    // Extract H1 and H2 tags (basic extraction from text_content for now)
    // In a more complete implementation, HTMLParser would extract these separately
    std::vector<std::string> h1_tags;
    std::vector<std::string> h2_tags;
    
    return index_parsed(doc_id, url, parsed.title, parsed.text_content,
                       parsed.description, h1_tags, h2_tags);
}

bool IndexBuilder::index_parsed(uint64_t doc_id,
                               const std::string& url,
                               const std::string& title,
                               const std::string& body,
                               const std::string& meta_description,
                               const std::vector<std::string>& h1_tags,
                               const std::vector<std::string>& h2_tags) {
    if (!db_) {
        last_error_ = "Not initialized";
        return false;
    }
    
    // If document exists, remove old postings first (Replace strategy)
    if (is_indexed(doc_id)) {
        remove_document(doc_id);
    }
    
    // Track all terms for this document (for future removal)
    std::vector<std::string> doc_terms;
    
    // Tokenize each field
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
    
    // Store document term list
    pending_doc_terms_[doc_id] = doc_terms;
    
    // Create document metadata
    IndexedDocument doc_meta;
    doc_meta.doc_id = doc_id;
    doc_meta.url = url;
    doc_meta.title = title;
    doc_meta.snippet = generate_snippet(body);
    doc_meta.word_count = static_cast<uint32_t>(tokenizer_->tokenize_simple(body).size());
    doc_meta.indexed_at = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    doc_meta.ai_score = 0.0f;  // Set later by AI detector
    doc_meta.era = "";         // Set later by AI detector
    
    pending_docs_metadata_.push_back(doc_meta);
    pending_docs_++;
    
    // Flush if batch is full
    if (pending_docs_ >= batch_size_) {
        return flush();
    }
    
    return true;
}

void IndexBuilder::tokenize_field(const std::string& text, Field field, 
                                  uint64_t doc_id, std::vector<std::string>& terms_out) {
    if (text.empty()) return;
    
    auto tokens = tokenizer_->tokenize(text);
    
    // Group by term to count frequency and positions
    std::unordered_map<std::string, Posting> term_postings;
    
    for (const auto& token : tokens) {
        auto& posting = term_postings[token.text];
        posting.doc_id = doc_id;
        posting.field = field;
        posting.frequency++;
        posting.positions.push_back(static_cast<uint16_t>(token.position));
    }
    
    // Add to pending postings
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
    
    // Find a good break point (end of sentence or word)
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

//=============================================================================
// Batch Operations (2.4.3)
//=============================================================================
bool IndexBuilder::flush() {
    if (!db_ || pending_docs_ == 0) {
        return true;
    }
    
    db_->begin_batch();
    
    // Write posting lists
    for (auto& [term, pending_pl] : pending_postings_) {
        std::string key = term_key(term);
        
        // Merge with existing posting list
        auto existing = db_->get(key);
        if (existing) {
            PostingList existing_pl = PostingList::deserialize(*existing);
            for (const auto& posting : pending_pl.postings) {
                existing_pl.add_posting(posting);
            }
            // Recalculate doc frequency
            std::unordered_set<uint64_t> unique_docs;
            for (const auto& p : existing_pl.postings) {
                unique_docs.insert(p.doc_id);
            }
            existing_pl.doc_frequency = static_cast<uint32_t>(unique_docs.size());
            db_->batch_put(key, existing_pl.serialize());
            
            // Update document frequency
            db_->batch_put(df_key(term), std::to_string(existing_pl.doc_frequency));
        } else {
            // New term
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
    
    // Write document metadata
    for (const auto& doc : pending_docs_metadata_) {
        db_->batch_put(doc_key(doc.doc_id), doc.serialize());
        doc_count_++;
    }
    
    // Write document term lists
    for (const auto& [doc_id, terms] : pending_doc_terms_) {
        std::ostringstream oss;
        for (size_t i = 0; i < terms.size(); ++i) {
            if (i > 0) oss << ",";
            oss << terms[i];
        }
        db_->batch_put(doc_terms_key(doc_id), oss.str());
    }
    
    // Commit batch
    bool success = db_->commit_batch();
    
    if (success) {
        // Save updated statistics
        save_statistics();
        
        // Clear pending data
        pending_postings_.clear();
        pending_docs_metadata_.clear();
        pending_doc_terms_.clear();
        pending_docs_ = 0;
    } else {
        last_error_ = "Batch commit failed: " + db_->last_error();
    }
    
    return success;
}

//=============================================================================
// Statistics (2.4.5)
//=============================================================================
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

//=============================================================================
// Document Management (2.4.6)
//=============================================================================
bool IndexBuilder::remove_document(uint64_t doc_id) {
    if (!db_) {
        last_error_ = "Not initialized";
        return false;
    }
    
    // Get list of terms for this document
    auto terms_data = db_->get(doc_terms_key(doc_id));
    if (!terms_data) {
        // Document not indexed
        return true;
    }
    
    // Parse term list
    std::vector<std::string> terms;
    std::istringstream iss(*terms_data);
    std::string term;
    while (std::getline(iss, term, ',')) {
        if (!term.empty()) {
            terms.push_back(term);
        }
    }
    
    db_->begin_batch();
    
    // Remove from each posting list
    for (const auto& t : terms) {
        auto pl_data = db_->get(term_key(t));
        if (pl_data) {
            PostingList pl = PostingList::deserialize(*pl_data);
            pl.remove_document(doc_id);
            
            if (pl.postings.empty()) {
                // Term no longer exists
                db_->batch_delete(term_key(t));
                db_->batch_delete(df_key(t));
                if (term_count_ > 0) term_count_--;
            } else {
                db_->batch_put(term_key(t), pl.serialize());
                db_->batch_put(df_key(t), std::to_string(pl.doc_frequency));
            }
        }
    }
    
    // Remove document metadata and term list
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