#include "query/result_formatter.h"
#include "common/date_utils.h"
#include <sstream>
#include <iomanip>

namespace search {

std::string ResultFormatter::format(SearchResponse& response, int start_rank) {
    if (response.empty()) {
        return "No results found.\n";
    }
    
    std::ostringstream out;
    
    int rank = start_rank;
    for (const auto& result : response.results) {
        const auto& doc = result.doc;
        
        // Rank and title
        out << rank << ". ";
        if (!doc.title.empty()) {
            std::string title = doc.title;
            if (title.length() > 60) {
                title = title.substr(0, 57) + "...";
            }
            out << title;
        } else {
            out << "(No title)";
        }
        out << "\n";
        
        // URL (truncate if too long)
        std::string url = doc.url;
        if (url.length() > 65) {
            url = url.substr(0, 62) + "...";
        }
        out << "   " << url << "\n";
        
        // Date/era and AI score line
        out << "   ";
        bool is_pre_ai = false;
        if (doc.published_at > 0) {
            is_pre_ai = DateUtils::is_pre_ai(doc.published_at);
            out << DateUtils::format_with_era(doc.published_at);
        } else {
            out << "[Date: Unknown]";
        }
        
        // Only show AI score for post-AI or unknown era content
        if (!is_pre_ai && doc.ai_score > 0.01f) {
            out << "  AI: " << static_cast<int>(doc.ai_score * 100) << "%";
        }
        out << "\n";
        
        // Snippet (shorter, cleaner)
        if (!doc.snippet.empty()) {
            std::string snippet = doc.snippet;
            
            // Clean up whitespace
            for (size_t i = 0; i < snippet.size(); ++i) {
                if (snippet[i] == '\n' || snippet[i] == '\r' || snippet[i] == '\t') {
                    snippet[i] = ' ';
                }
            }
            // Collapse multiple spaces
            std::string clean;
            bool last_space = false;
            for (char c : snippet) {
                if (c == ' ') {
                    if (!last_space) {
                        clean += c;
                        last_space = true;
                    }
                } else {
                    clean += c;
                    last_space = false;
                }
            }
            snippet = clean;
            
            // Truncate to ~100 chars
            if (snippet.length() > 100) {
                snippet = snippet.substr(0, 97) + "...";
            }
            
            out << "   " << snippet << "\n";
        }
        
        out << "\n";
        rank++;
    }
    
    return out.str();
}

} // namespace search