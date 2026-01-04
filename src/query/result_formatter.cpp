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
            out << doc.title;
        } else {
            out << "(No title)";
        }
        out << "\n";
        
        // URL (truncate if too long)
        std::string url = doc.url;
        if (url.length() > 70) {
            url = url.substr(0, 67) + "...";
        }
        out << "   " << url << "\n";
        
        // Date and era line
        out << "   ";
        if (doc.published_at > 0) {
            out << DateUtils::format_with_era(doc.published_at);
        } else {
            out << "[Date: Unknown]";
        }
        
        // Show modified date if significantly different
        if (doc.modified_at > 0 && doc.modified_at != doc.published_at) {
            // Only show if modified date is at least a month after published
            int64_t diff = doc.modified_at - doc.published_at;
            if (diff > 30 * 24 * 60 * 60) {  // 30 days
                out << "  Updated: " << DateUtils::format_date(doc.modified_at);
                // Warning if crossed era boundary
                if (doc.published_at > 0 && 
                    DateUtils::is_pre_ai(doc.published_at) && 
                    !DateUtils::is_pre_ai(doc.modified_at)) {
                    out << " ⚠";
                }
            }
        }
        out << "\n";
        
        // Snippet (if available)
        if (!doc.snippet.empty()) {
            std::string snippet = doc.snippet;
            // Clean up snippet - remove excessive whitespace
            size_t pos;
            while ((pos = snippet.find("  ")) != std::string::npos) {
                snippet.replace(pos, 2, " ");
            }
            while ((pos = snippet.find("\n")) != std::string::npos) {
                snippet.replace(pos, 1, " ");
            }
            // Truncate if needed
            if (snippet.length() > 160) {
                snippet = snippet.substr(0, 157) + "...";
            }
            out << "   " << snippet << "\n";
        }
        
        out << "\n";
        rank++;
    }
    
    return out.str();
}

} // namespace search