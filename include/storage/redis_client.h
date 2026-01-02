#pragma once

#include <string>
#include <optional>
#include <vector>
#include <chrono>
#include <memory>

namespace search {

//=============================================================================
// Redis Client - caching layer for hot data
//=============================================================================
class RedisClient {
public:
    RedisClient();
    ~RedisClient();
    
    // Prevent copying
    RedisClient(const RedisClient&) = delete;
    RedisClient& operator=(const RedisClient&) = delete;
    
    // Allow moving
    RedisClient(RedisClient&&) noexcept;
    RedisClient& operator=(RedisClient&&) noexcept;
    
    //=========================================================================
    // Connection (2.5.1)
    //=========================================================================
    
    // Connect using environment variables (REDIS_HOST, REDIS_PORT)
    bool connect();
    
    // Connect with explicit parameters
    bool connect(const std::string& host, int port = 6379, int timeout_ms = 5000);
    
    // Disconnect
    void disconnect();
    
    // Check connection
    bool is_connected() const { return connected_; }
    
    // Ping server
    bool ping();
    
    //=========================================================================
    // Basic Operations (2.5.2)
    //=========================================================================
    
    // Set a key-value pair
    bool set(const std::string& key, const std::string& value);
    
    // Set with TTL (time-to-live in seconds)
    bool set(const std::string& key, const std::string& value, int ttl_seconds);
    
    // Get a value
    std::optional<std::string> get(const std::string& key);
    
    // Delete a key
    bool del(const std::string& key);
    
    // Check if key exists
    bool exists(const std::string& key);
    
    // Set TTL on existing key
    bool expire(const std::string& key, int ttl_seconds);
    
    // Get TTL of key (-1 if no TTL, -2 if key doesn't exist)
    int ttl(const std::string& key);
    
    //=========================================================================
    // Batch Operations
    //=========================================================================
    
    // Set multiple keys
    bool mset(const std::vector<std::pair<std::string, std::string>>& pairs);
    
    // Get multiple keys
    std::vector<std::optional<std::string>> mget(const std::vector<std::string>& keys);
    
    // Delete multiple keys
    int del(const std::vector<std::string>& keys);
    
    //=========================================================================
    // Cache Helpers (2.5.3)
    //=========================================================================
    
    // Get or set (returns cached value or calls generator and caches result)
    template<typename Generator>
    std::string get_or_set(const std::string& key, int ttl_seconds, Generator gen) {
        auto cached = get(key);
        if (cached) {
            return *cached;
        }
        
        std::string value = gen();
        set(key, value, ttl_seconds);
        return value;
    }
    
    // Increment counter
    int64_t incr(const std::string& key);
    
    // Increment by amount
    int64_t incrby(const std::string& key, int64_t amount);
    
    //=========================================================================
    // Key Patterns
    //=========================================================================
    
    // Delete all keys matching pattern (use with caution!)
    int del_pattern(const std::string& pattern);
    
    // Get all keys matching pattern
    std::vector<std::string> keys(const std::string& pattern);
    
    //=========================================================================
    // Utility
    //=========================================================================
    
    // Flush all data (use with caution!)
    bool flushdb();
    
    // Get database size (number of keys)
    int64_t dbsize();
    
    // Get last error
    const std::string& last_error() const { return last_error_; }
    
    //=========================================================================
    // Predefined cache key generators
    //=========================================================================
    
    // Cache keys for different data types
    static std::string robots_key(const std::string& domain) {
        return "robots:" + domain;
    }
    
    static std::string query_key(const std::string& query_hash) {
        return "query:" + query_hash;
    }
    
    static std::string posting_key(const std::string& term) {
        return "posting:" + term;
    }
    
    // Default TTLs (in seconds)
    static constexpr int ROBOTS_TTL = 3600;       // 1 hour
    static constexpr int QUERY_TTL = 300;         // 5 minutes
    static constexpr int POSTING_TTL = 600;       // 10 minutes

private:
    void* ctx_ = nullptr;  // redisContext* (opaque to avoid header dependency)
    bool connected_ = false;
    std::string host_;
    int port_ = 6379;
    std::string last_error_;
    
    // Execute command and get reply
    void* execute(const char* format, ...);
    void freeReply(void* reply);
};

} // namespace search