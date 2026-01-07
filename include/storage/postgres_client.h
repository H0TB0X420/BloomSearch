#pragma once

#include <string>
#include <optional>
#include <vector>
#include <memory>
#include <chrono>
#include <pqxx/pqxx>

namespace search {

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

struct FrontierEntry {
    std::string url;
    std::string domain;
    int priority = 0;
};

class PostgresClient {
public:
    PostgresClient() = default;
    ~PostgresClient();
    
    PostgresClient(const PostgresClient&) = delete;
    PostgresClient& operator=(const PostgresClient&) = delete;
    
    PostgresClient(PostgresClient&&) = default;
    PostgresClient& operator=(PostgresClient&&) = default;
    
    bool connect();
    
    bool connect(const std::string& connection_string);
    
    bool connect(const std::string& host, int port, const std::string& dbname,
                 const std::string& user, const std::string& password);
    
    void disconnect();
    
    bool is_connected() const;
    
    bool reconnect();
    
    bool initialize_schema();
    
    bool upsert_page(const PageRecord& page);
    
    std::optional<PageRecord> get_page(const std::string& url);
    
    bool mark_crawled(const std::string& url, int status_code, 
                      const std::string& content_hash, int64_t content_size);
    
    std::vector<PageRecord> get_unindexed_pages(int limit = 100);
    
    bool mark_page_indexed(int64_t page_id);
    
    int64_t get_page_count();
    
    int64_t get_indexed_page_count();
    
    bool execute(const std::string& sql);
    
    const std::string& last_error() const { return last_error_; }

    bool save_frontier(const std::vector<FrontierEntry>& entries);
    std::vector<FrontierEntry> load_frontier();
    bool clear_frontier();

private:
    std::unique_ptr<pqxx::connection> conn_;
    std::string connection_string_;
    std::string last_error_;
    
    static std::string compute_hash(const std::string& url);
    
    template<typename F>
    bool execute_with_retry(F&& func);
};

} // namespace search