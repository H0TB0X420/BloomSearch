#include "common/search_types.h"

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
    p.frequency = static_cast<uint16_t>(std::stoi(token));
    
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
}