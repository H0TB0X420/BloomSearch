#include "storage/redis_client.h"
#include <hiredis/hiredis.h>
#include <cstdlib>
#include <cstdarg>

namespace search {

RedisClient::RedisClient() = default;

RedisClient::~RedisClient() {
    disconnect();
}

RedisClient::RedisClient(RedisClient&& other) noexcept
    : ctx_(other.ctx_)
    , connected_(other.connected_)
    , host_(std::move(other.host_))
    , port_(other.port_)
    , last_error_(std::move(other.last_error_)) {
    other.ctx_ = nullptr;
    other.connected_ = false;
}

RedisClient& RedisClient::operator=(RedisClient&& other) noexcept {
    if (this != &other) {
        disconnect();
        ctx_ = other.ctx_;
        connected_ = other.connected_;
        host_ = std::move(other.host_);
        port_ = other.port_;
        last_error_ = std::move(other.last_error_);
        other.ctx_ = nullptr;
        other.connected_ = false;
    }
    return *this;
}

bool RedisClient::connect() {
    const char* host = std::getenv("REDIS_HOST");
    const char* port_str = std::getenv("REDIS_PORT");
    
    std::string redis_host = host ? host : "localhost";
    int redis_port = port_str ? std::atoi(port_str) : 6379;
    
    return connect(redis_host, redis_port);
}

bool RedisClient::connect(const std::string& host, int port, int timeout_ms) {
    if (connected_) {
        disconnect();
    }
    
    host_ = host;
    port_ = port;
    
    struct timeval timeout = {
        timeout_ms / 1000,
        (timeout_ms % 1000) * 1000
    };
    
    redisContext* c = redisConnectWithTimeout(host.c_str(), port, timeout);
    
    if (c == nullptr) {
        last_error_ = "Failed to allocate redis context";
        return false;
    }
    
    if (c->err) {
        last_error_ = c->errstr;
        redisFree(c);
        return false;
    }
    
    ctx_ = c;
    connected_ = true;
    return true;
}

void RedisClient::disconnect() {
    if (ctx_) {
        redisFree(static_cast<redisContext*>(ctx_));
        ctx_ = nullptr;
    }
    connected_ = false;
}

bool RedisClient::ping() {
    if (!connected_) return false;
    
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(static_cast<redisContext*>(ctx_), "PING")
    );
    
    if (!reply) {
        last_error_ = "No reply from server";
        return false;
    }
    
    bool ok = (reply->type == REDIS_REPLY_STATUS && 
               std::string(reply->str) == "PONG");
    
    freeReplyObject(reply);
    return ok;
}

bool RedisClient::set(const std::string& key, const std::string& value) {
    if (!connected_) {
        last_error_ = "Not connected";
        return false;
    }
    
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(static_cast<redisContext*>(ctx_), 
                    "SET %s %s", key.c_str(), value.c_str())
    );
    
    if (!reply) {
        last_error_ = "No reply from server";
        return false;
    }
    
    bool ok = (reply->type == REDIS_REPLY_STATUS);
    freeReplyObject(reply);
    return ok;
}

bool RedisClient::set(const std::string& key, const std::string& value, int ttl_seconds) {
    if (!connected_) {
        last_error_ = "Not connected";
        return false;
    }
    
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(static_cast<redisContext*>(ctx_), 
                    "SET %s %s EX %d", key.c_str(), value.c_str(), ttl_seconds)
    );
    
    if (!reply) {
        last_error_ = "No reply from server";
        return false;
    }
    
    bool ok = (reply->type == REDIS_REPLY_STATUS);
    freeReplyObject(reply);
    return ok;
}

std::optional<std::string> RedisClient::get(const std::string& key) {
    if (!connected_) {
        last_error_ = "Not connected";
        return std::nullopt;
    }
    
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(static_cast<redisContext*>(ctx_), 
                    "GET %s", key.c_str())
    );
    
    if (!reply) {
        last_error_ = "No reply from server";
        return std::nullopt;
    }
    
    std::optional<std::string> result;
    
    if (reply->type == REDIS_REPLY_STRING) {
        result = std::string(reply->str, reply->len);
    }
    
    freeReplyObject(reply);
    return result;
}

bool RedisClient::del(const std::string& key) {
    if (!connected_) {
        last_error_ = "Not connected";
        return false;
    }
    
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(static_cast<redisContext*>(ctx_), 
                    "DEL %s", key.c_str())
    );
    
    if (!reply) {
        last_error_ = "No reply from server";
        return false;
    }
    
    bool ok = (reply->type == REDIS_REPLY_INTEGER && reply->integer >= 0);
    freeReplyObject(reply);
    return ok;
}

bool RedisClient::exists(const std::string& key) {
    if (!connected_) return false;
    
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(static_cast<redisContext*>(ctx_), 
                    "EXISTS %s", key.c_str())
    );
    
    if (!reply) return false;
    
    bool exists = (reply->type == REDIS_REPLY_INTEGER && reply->integer > 0);
    freeReplyObject(reply);
    return exists;
}

bool RedisClient::expire(const std::string& key, int ttl_seconds) {
    if (!connected_) {
        last_error_ = "Not connected";
        return false;
    }
    
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(static_cast<redisContext*>(ctx_), 
                    "EXPIRE %s %d", key.c_str(), ttl_seconds)
    );
    
    if (!reply) {
        last_error_ = "No reply from server";
        return false;
    }
    
    bool ok = (reply->type == REDIS_REPLY_INTEGER && reply->integer == 1);
    freeReplyObject(reply);
    return ok;
}

int RedisClient::ttl(const std::string& key) {
    if (!connected_) return -2;
    
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(static_cast<redisContext*>(ctx_), 
                    "TTL %s", key.c_str())
    );
    
    if (!reply) return -2;
    
    int result = (reply->type == REDIS_REPLY_INTEGER) ? 
                 static_cast<int>(reply->integer) : -2;
    freeReplyObject(reply);
    return result;
}

bool RedisClient::mset(const std::vector<std::pair<std::string, std::string>>& pairs) {
    if (!connected_ || pairs.empty()) {
        last_error_ = pairs.empty() ? "Empty pairs" : "Not connected";
        return false;
    }
    
    std::vector<const char*> argv;
    std::vector<size_t> argvlen;
    
    std::string cmd = "MSET";
    argv.push_back(cmd.c_str());
    argvlen.push_back(cmd.length());
    
    for (const auto& [key, value] : pairs) {
        argv.push_back(key.c_str());
        argvlen.push_back(key.length());
        argv.push_back(value.c_str());
        argvlen.push_back(value.length());
    }
    
    redisReply* reply = static_cast<redisReply*>(
        redisCommandArgv(static_cast<redisContext*>(ctx_),
                        static_cast<int>(argv.size()),
                        argv.data(), argvlen.data())
    );
    
    if (!reply) {
        last_error_ = "No reply from server";
        return false;
    }
    
    bool ok = (reply->type == REDIS_REPLY_STATUS);
    freeReplyObject(reply);
    return ok;
}

std::vector<std::optional<std::string>> RedisClient::mget(const std::vector<std::string>& keys) {
    std::vector<std::optional<std::string>> results;
    
    if (!connected_ || keys.empty()) {
        return results;
    }
    
    std::vector<const char*> argv;
    std::vector<size_t> argvlen;
    
    std::string cmd = "MGET";
    argv.push_back(cmd.c_str());
    argvlen.push_back(cmd.length());
    
    for (const auto& key : keys) {
        argv.push_back(key.c_str());
        argvlen.push_back(key.length());
    }
    
    redisReply* reply = static_cast<redisReply*>(
        redisCommandArgv(static_cast<redisContext*>(ctx_),
                        static_cast<int>(argv.size()),
                        argv.data(), argvlen.data())
    );
    
    if (!reply) {
        return results;
    }
    
    if (reply->type == REDIS_REPLY_ARRAY) {
        for (size_t i = 0; i < reply->elements; ++i) {
            if (reply->element[i]->type == REDIS_REPLY_STRING) {
                results.emplace_back(
                    std::string(reply->element[i]->str, reply->element[i]->len)
                );
            } else {
                results.emplace_back(std::nullopt);
            }
        }
    }
    
    freeReplyObject(reply);
    return results;
}

int RedisClient::del(const std::vector<std::string>& keys) {
    if (!connected_ || keys.empty()) return 0;
    
    std::vector<const char*> argv;
    std::vector<size_t> argvlen;
    
    std::string cmd = "DEL";
    argv.push_back(cmd.c_str());
    argvlen.push_back(cmd.length());
    
    for (const auto& key : keys) {
        argv.push_back(key.c_str());
        argvlen.push_back(key.length());
    }
    
    redisReply* reply = static_cast<redisReply*>(
        redisCommandArgv(static_cast<redisContext*>(ctx_),
                        static_cast<int>(argv.size()),
                        argv.data(), argvlen.data())
    );
    
    if (!reply) return 0;
    
    int deleted = (reply->type == REDIS_REPLY_INTEGER) ? 
                  static_cast<int>(reply->integer) : 0;
    freeReplyObject(reply);
    return deleted;
}

int64_t RedisClient::incr(const std::string& key) {
    if (!connected_) return -1;
    
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(static_cast<redisContext*>(ctx_), 
                    "INCR %s", key.c_str())
    );
    
    if (!reply) return -1;
    
    int64_t result = (reply->type == REDIS_REPLY_INTEGER) ? reply->integer : -1;
    freeReplyObject(reply);
    return result;
}

int64_t RedisClient::incrby(const std::string& key, int64_t amount) {
    if (!connected_) return -1;
    
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(static_cast<redisContext*>(ctx_), 
                    "INCRBY %s %lld", key.c_str(), amount)
    );
    
    if (!reply) return -1;
    
    int64_t result = (reply->type == REDIS_REPLY_INTEGER) ? reply->integer : -1;
    freeReplyObject(reply);
    return result;
}

std::vector<std::string> RedisClient::keys(const std::string& pattern) {
    std::vector<std::string> result;
    
    if (!connected_) return result;
    
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(static_cast<redisContext*>(ctx_), 
                    "KEYS %s", pattern.c_str())
    );
    
    if (!reply) return result;
    
    if (reply->type == REDIS_REPLY_ARRAY) {
        for (size_t i = 0; i < reply->elements; ++i) {
            if (reply->element[i]->type == REDIS_REPLY_STRING) {
                result.emplace_back(reply->element[i]->str, reply->element[i]->len);
            }
        }
    }
    
    freeReplyObject(reply);
    return result;
}

int RedisClient::del_pattern(const std::string& pattern) {
    auto matching_keys = keys(pattern);
    if (matching_keys.empty()) return 0;
    return del(matching_keys);
}

bool RedisClient::flushdb() {
    if (!connected_) return false;
    
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(static_cast<redisContext*>(ctx_), "FLUSHDB")
    );
    
    if (!reply) return false;
    
    bool ok = (reply->type == REDIS_REPLY_STATUS);
    freeReplyObject(reply);
    return ok;
}

int64_t RedisClient::dbsize() {
    if (!connected_) return -1;
    
    redisReply* reply = static_cast<redisReply*>(
        redisCommand(static_cast<redisContext*>(ctx_), "DBSIZE")
    );
    
    if (!reply) return -1;
    
    int64_t size = (reply->type == REDIS_REPLY_INTEGER) ? reply->integer : -1;
    freeReplyObject(reply);
    return size;
}

void* RedisClient::execute(const char* format, ...) {
    if (!connected_) return nullptr;
    
    va_list args;
    va_start(args, format);
    void* reply = redisvCommand(static_cast<redisContext*>(ctx_), format, args);
    va_end(args);
    
    return reply;
}

void RedisClient::freeReply(void* reply) {
    if (reply) {
        freeReplyObject(reply);
    }
}

} // namespace search