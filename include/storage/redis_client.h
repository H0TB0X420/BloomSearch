#pragma once

#include <string>
#include <optional>
#include <vector>
#include <chrono>
#include <memory>

namespace search {

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
    
    bool connect();
    
    bool connect(const std::string& host, int port = 6379, int timeout_ms = 5000);
    
    void disconnect();
    
    bool is_connected() const { return connected_; }
    
    bool ping();
    
    bool set(const std::string& key, const std::string& value);
    
    bool set(const std::string& key, const std::string& value, int ttl_seconds);
    
    std::optional<std::string> get(const std::string& key);
    
    bool del(const std::string& key);
    
    bool exists(const std::string& key);
    
    bool expire(const std::string& key, int ttl_seconds);
    
    int ttl(const std::string& key);
    
    bool mset(const std::vector<std::pair<std::string, std::string>>& pairs);
    
    std::vector<std::optional<std::string>> mget(const std::vector<std::string>& keys);
    
    int del(const std::vector<std::string>& keys);
    
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
    
    int64_t incr(const std::string& key);
    
    int64_t incrby(const std::string& key, int64_t amount);
    
    int del_pattern(const std::string& pattern);
    
    std::vector<std::string> keys(const std::string& pattern);
    
    bool flushdb();
    
    int64_t dbsize();
    
    const std::string& last_error() const { return last_error_; }
    
    static std::string robots_key(const std::string& domain) {
        return "robots:" + domain;
    }
    
    static std::string query_key(const std::string& query_hash) {
        return "query:" + query_hash;
    }
    
    static std::string posting_key(const std::string& term) {
        return "posting:" + term;
    }
    
    static constexpr int ROBOTS_TTL = 3600;       // 1 hour
    static constexpr int QUERY_TTL = 300;         // 5 minutes
    static constexpr int POSTING_TTL = 600;       // 10 minutes

private:
    void* ctx_ = nullptr;  // redisContext* (opaque to avoid header dependency)
    bool connected_ = false;
    std::string host_;
    int port_ = 6379;
    std::string last_error_;
    
    void* execute(const char* format, ...);
    void freeReply(void* reply);
};

} // namespace search