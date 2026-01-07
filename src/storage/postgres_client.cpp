#include "storage/postgres_client.h"
#include <functional>
#include <sstream>
#include <iomanip>

namespace search {

std::string PostgresClient::compute_hash(const std::string& url) {
    std::hash<std::string> hasher;
    size_t hash = hasher(url);
    
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << hash;
    return oss.str();
}

PostgresClient::~PostgresClient() {
    disconnect();
}

bool PostgresClient::connect() {
    const char* conn_str = std::getenv("POSTGRES_CONNECTION");
    if (conn_str) {
        return connect(std::string(conn_str));
    }
    
    const char* host = std::getenv("POSTGRES_HOST");
    const char* port_str = std::getenv("POSTGRES_PORT");
    const char* db = std::getenv("POSTGRES_DB");
    const char* user = std::getenv("POSTGRES_USER");
    const char* pass = std::getenv("POSTGRES_PASSWORD");
    
    if (host && db && user && pass) {
        int port = port_str ? std::stoi(port_str) : 5432;
        return connect(host, port, db, user, pass);
    }
    
    return connect("host=localhost port=5432 dbname=postgres user=postgres password=postgres");
}

bool PostgresClient::connect(const std::string& connection_string) {
    try {
        connection_string_ = connection_string;
        conn_ = std::make_unique<pqxx::connection>(connection_string);
        return conn_->is_open();
    } catch (const std::exception& e) {
        last_error_ = e.what();
        conn_.reset();
        return false;
    }
}

bool PostgresClient::connect(const std::string& host, int port, const std::string& dbname,
                             const std::string& user, const std::string& password) {
    std::ostringstream oss;
    oss << "host=" << host 
        << " port=" << port 
        << " dbname=" << dbname
        << " user=" << user 
        << " password=" << password;
    return connect(oss.str());
}

void PostgresClient::disconnect() {
    if (conn_) {
        conn_.reset();
    }
}

bool PostgresClient::is_connected() const {
    return conn_ && conn_->is_open();
}

bool PostgresClient::reconnect() {
    if (connection_string_.empty()) {
        return false;
    }
    disconnect();
    return connect(connection_string_);
}

bool PostgresClient::initialize_schema() {
    if (!is_connected()) return false;
    
    try {
        pqxx::work txn(*conn_);
        
        txn.exec(R"(
            CREATE TABLE IF NOT EXISTS pages (
                id SERIAL PRIMARY KEY,
                url TEXT UNIQUE NOT NULL,
                url_hash TEXT NOT NULL,
                domain TEXT NOT NULL,
                crawled_at TIMESTAMP,
                status_code INTEGER,
                content_hash TEXT,
                content_size BIGINT DEFAULT 0,
                ai_score FLOAT,
                era TEXT,
                indexed BOOLEAN DEFAULT FALSE,
                created_at TIMESTAMP DEFAULT NOW()
            )
        )");
        
        txn.exec(R"(
            CREATE INDEX IF NOT EXISTS idx_pages_url_hash ON pages(url_hash)
        )");
        
        txn.exec(R"(
            CREATE INDEX IF NOT EXISTS idx_pages_domain ON pages(domain)
        )");
        
        txn.exec(R"(
            CREATE INDEX IF NOT EXISTS idx_pages_unindexed 
            ON pages(crawled_at) WHERE indexed = FALSE
        )");
        
        txn.exec(R"(
            CREATE TABLE IF NOT EXISTS frontier_state (
                id SERIAL PRIMARY KEY,
                url TEXT NOT NULL,
                domain TEXT NOT NULL,
                priority INTEGER DEFAULT 0,
                added_at TIMESTAMP DEFAULT NOW()
            )
        )");
        
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        last_error_ = e.what();
        return false;
    }
}

bool PostgresClient::upsert_page(const PageRecord& page) {
    if (!is_connected()) return false;
    
    try {
        pqxx::work txn(*conn_);
        
        std::string url_hash = compute_hash(page.url);
        
        txn.exec_params(R"(
            INSERT INTO pages (url, url_hash, domain, status_code, content_hash, content_size, ai_score, era, indexed)
            VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9)
            ON CONFLICT (url) DO UPDATE SET
                status_code = EXCLUDED.status_code,
                content_hash = EXCLUDED.content_hash,
                content_size = EXCLUDED.content_size,
                ai_score = COALESCE(EXCLUDED.ai_score, pages.ai_score),
                era = COALESCE(EXCLUDED.era, pages.era),
                indexed = COALESCE(EXCLUDED.indexed, pages.indexed),
                crawled_at = CASE WHEN EXCLUDED.status_code IS NOT NULL THEN NOW() ELSE pages.crawled_at END
        )",
            page.url,
            url_hash,
            page.domain,
            page.status_code,
            page.content_hash,
            page.content_size,
            page.ai_score ? *page.ai_score : std::optional<double>{},
            page.era.empty() ? std::optional<std::string>{} : page.era,
            page.indexed
        );
        
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        last_error_ = e.what();
        return false;
    }
}

std::optional<PageRecord> PostgresClient::get_page(const std::string& url) {
    if (!is_connected()) return std::nullopt;
    
    try {
        pqxx::work txn(*conn_);
        
        auto result = txn.exec_params(
            "SELECT id, url, url_hash, domain, crawled_at, status_code, "
            "content_hash, content_size, ai_score, era, indexed FROM pages WHERE url = $1",
            url
        );
        
        if (result.empty()) {
            return std::nullopt;
        }
        
        PageRecord page;
        page.id = result[0][0].as<int64_t>();
        page.url = result[0][1].as<std::string>();
        page.url_hash = result[0][2].as<std::string>();
        page.domain = result[0][3].as<std::string>();
        page.status_code = result[0][5].is_null() ? 0 : result[0][5].as<int>();
        page.content_hash = result[0][6].is_null() ? "" : result[0][6].as<std::string>();
        page.content_size = result[0][7].is_null() ? 0 : result[0][7].as<int64_t>();
        if (!result[0][8].is_null()) {
            page.ai_score = result[0][8].as<float>();
        }
        page.era = result[0][9].is_null() ? "" : result[0][9].as<std::string>();
        page.indexed = result[0][10].is_null() ? false : result[0][10].as<bool>();
        
        return page;
    } catch (const std::exception& e) {
        last_error_ = e.what();
        return std::nullopt;
    }
}

bool PostgresClient::mark_crawled(const std::string& url, int status_code,
                                   const std::string& content_hash, int64_t content_size) {
    if (!is_connected()) return false;
    
    try {
        pqxx::work txn(*conn_);
        
        std::string url_hash = compute_hash(url);
        
        // Extract domain from URL
        std::string domain;
        size_t scheme_end = url.find("://");
        if (scheme_end != std::string::npos) {
            size_t host_start = scheme_end + 3;
            size_t host_end = url.find('/', host_start);
            if (host_end == std::string::npos) {
                domain = url.substr(host_start);
            } else {
                domain = url.substr(host_start, host_end - host_start);
            }
            // Remove www. prefix
            if (domain.substr(0, 4) == "www.") {
                domain = domain.substr(4);
            }
        }
        
        // Upsert: INSERT if not exists, UPDATE if exists
        txn.exec_params(R"(
            INSERT INTO pages (url, url_hash, domain, crawled_at, status_code, content_hash, content_size, indexed)
            VALUES ($1, $2, $3, NOW(), $4, $5, $6, FALSE)
            ON CONFLICT (url) DO UPDATE SET
                crawled_at = NOW(),
                status_code = EXCLUDED.status_code,
                content_hash = EXCLUDED.content_hash,
                content_size = EXCLUDED.content_size
        )",
            url,
            url_hash,
            domain,
            status_code,
            content_hash,
            content_size
        );
        
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        last_error_ = e.what();
        return false;
    }
}

std::vector<PageRecord> PostgresClient::get_unindexed_pages(int limit) {
    std::vector<PageRecord> pages;
    
    if (!is_connected()) return pages;
    
    try {
        pqxx::work txn(*conn_);
        
        auto result = txn.exec_params(
            "SELECT id, url, url_hash, domain, status_code, content_hash, content_size "
            "FROM pages WHERE crawled_at IS NOT NULL AND status_code = 200 AND indexed = FALSE "
            "ORDER BY crawled_at LIMIT $1",
            limit
        );
        
        for (const auto& row : result) {
            PageRecord page;
            page.id = row[0].as<int64_t>();
            page.url = row[1].as<std::string>();
            page.url_hash = row[2].as<std::string>();
            page.domain = row[3].as<std::string>();
            page.status_code = row[4].is_null() ? 0 : row[4].as<int>();
            page.content_hash = row[5].is_null() ? "" : row[5].as<std::string>();
            page.content_size = row[6].is_null() ? 0 : row[6].as<int64_t>();
            page.indexed = false;
            pages.push_back(page);
        }
        
        return pages;
    } catch (const std::exception& e) {
        last_error_ = e.what();
        return pages;
    }
}

bool PostgresClient::mark_page_indexed(int64_t page_id) {
    if (!is_connected()) return false;
    
    try {
        pqxx::work txn(*conn_);
        
        txn.exec_params(
            "UPDATE pages SET indexed = TRUE WHERE id = $1",
            page_id
        );
        
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        last_error_ = e.what();
        return false;
    }
}

int64_t PostgresClient::get_page_count() {
    if (!is_connected()) return 0;
    
    try {
        pqxx::work txn(*conn_);
        auto result = txn.exec("SELECT COUNT(*) FROM pages");
        return result[0][0].as<int64_t>();
    } catch (const std::exception& e) {
        last_error_ = e.what();
        return 0;
    }
}

int64_t PostgresClient::get_indexed_page_count() {
    if (!is_connected()) return 0;
    
    try {
        pqxx::work txn(*conn_);
        auto result = txn.exec("SELECT COUNT(*) FROM pages WHERE indexed = TRUE");
        return result[0][0].as<int64_t>();
    } catch (const std::exception& e) {
        last_error_ = e.what();
        return 0;
    }
}

bool PostgresClient::save_frontier(const std::vector<FrontierEntry>& entries) {
    if (!is_connected()) return false;
    
    try {
        pqxx::work txn(*conn_);
        
        txn.exec("TRUNCATE frontier_state");
        
        for (const auto& entry : entries) {
            txn.exec_params(
                "INSERT INTO frontier_state (url, domain, priority) VALUES ($1, $2, $3)",
                entry.url, entry.domain, entry.priority
            );
        }
        
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        last_error_ = e.what();
        return false;
    }
}

std::vector<FrontierEntry> PostgresClient::load_frontier() {
    std::vector<FrontierEntry> entries;
    
    if (!is_connected()) return entries;
    
    try {
        pqxx::work txn(*conn_);
        
        auto result = txn.exec(
            "SELECT url, domain, priority FROM frontier_state ORDER BY priority, added_at"
        );
        
        for (const auto& row : result) {
            FrontierEntry entry;
            entry.url = row[0].as<std::string>();
            entry.domain = row[1].as<std::string>();
            entry.priority = row[2].as<int>();
            entries.push_back(entry);
        }
        
        return entries;
    } catch (const std::exception& e) {
        last_error_ = e.what();
        return entries;
    }
}

bool PostgresClient::clear_frontier() {
    if (!is_connected()) return false;
    
    try {
        pqxx::work txn(*conn_);
        txn.exec("TRUNCATE frontier_state");
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        last_error_ = e.what();
        return false;
    }
}

bool PostgresClient::execute(const std::string& sql) {
    if (!is_connected()) return false;
    
    try {
        pqxx::work txn(*conn_);
        txn.exec(sql);
        txn.commit();
        return true;
    } catch (const std::exception& e) {
        last_error_ = e.what();
        return false;
    }
}

} // namespace search