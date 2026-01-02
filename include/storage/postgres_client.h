#pragma once

#include <string>
#include <optional>
#include <vector>
#include <memory>
#include <chrono>
#include <pqxx/pqxx>

namespace search {

//=============================================================================
// Page metadata structure
//=============================================================================
struct PageRecord {
    int64_t id = 0;
    std::string url;
    std::string url_hash;
    std::string domain;
    std::optional<std::chrono::system_clock::time_point> crawled_at;
    int status_code = 0;
    std::string content_hash;
    int64_t content_size = 0;
    std::optional<float> ai_score;
    std::string era;  // "pre-ai", "transition", "ai-era"
    bool indexed = false;
};

//=============================================================================
// Frontier entry for persistence
//=============================================================================
struct FrontierEntry {
    std::string url;
    std::string domain;
    int priority = 0;
};

//=============================================================================
// PostgreSQL Client
//=============================================================================
class PostgresClient {
public:
    PostgresClient() = default;
    ~PostgresClient();
    
    // Prevent copying (connection is not copyable)
    PostgresClient(const PostgresClient&) = delete;
    PostgresClient& operator=(const PostgresClient&) = delete;
    
    // Allow moving
    PostgresClient(PostgresClient&&) = default;
    PostgresClient& operator=(PostgresClient&&) = default;
    
    //=========================================================================
    // Connection Management (1.3.1)
    //=========================================================================
    
    // Connect using environment variable POSTGRES_CONNECTION
    // Falls back to individual POSTGRES_* env vars
    bool connect();
    
    // Connect using connection string
    // Format: "host=localhost port=5432 dbname=bloom user=postgres password=secret"
    bool connect(const std::string& connection_string);
    
    // Connect using individual parameters
    bool connect(const std::string& host, int port, const std::string& dbname,
                 const std::string& user, const std::string& password);
    
    // Disconnect (also called by destructor)
    void disconnect();
    
    // Check if connected
    bool is_connected() const;
    
    // Reconnect if connection was lost
    bool reconnect();
    
    //=========================================================================
    // Schema Management (1.3.2)
    //=========================================================================
    
    // Create tables if they don't exist
    bool initialize_schema();
    
    //=========================================================================
    // Page CRUD Operations (1.3.3)
    //=========================================================================
    
    // Insert or update a page record
    bool upsert_page(const PageRecord& page);
    
    // Get page by URL
    std::optional<PageRecord> get_page(const std::string& url);
    
    // Get page by URL hash (faster)
    std::optional<PageRecord> get_page_by_hash(const std::string& url_hash);
    
    // Check if URL has been crawled
    bool is_crawled(const std::string& url);
    
    // Mark page as crawled with metadata
    bool mark_crawled(const std::string& url, int status_code, 
                      const std::string& content_hash, int64_t content_size);
    
    // Update AI score for a page
    bool update_ai_score(const std::string& url, float score, const std::string& era);
    
    // Get pages that need indexing (crawled but not indexed)
    std::vector<PageRecord> get_unindexed_pages(int limit = 100);
    
    // Mark a page as indexed
    bool mark_page_indexed(int64_t page_id);
    
    // Get page count
    int64_t get_page_count();
    
    // Get indexed page count
    int64_t get_indexed_page_count();
    
    //=========================================================================
    // Frontier Persistence (1.3.4)
    //=========================================================================
    
    // Save frontier state (for crash recovery)
    bool save_frontier(const std::vector<FrontierEntry>& entries);
    
    // Load frontier state
    std::vector<FrontierEntry> load_frontier();
    
    // Clear frontier state (after successful load)
    bool clear_frontier();
    
    //=========================================================================
    // Raw SQL Execution
    //=========================================================================
    
    // Execute arbitrary SQL (for schema migrations, etc.)
    bool execute(const std::string& sql);
    
    // Get last error message
    const std::string& last_error() const { return last_error_; }

private:
    std::unique_ptr<pqxx::connection> conn_;
    std::string connection_string_;
    std::string last_error_;
    
    // Helper to compute URL hash
    static std::string compute_hash(const std::string& url);
    
    // Execute with reconnect on failure
    template<typename F>
    bool execute_with_retry(F&& func);
};

} // namespace search