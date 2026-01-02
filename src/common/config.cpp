#include "common/config.h"
#include <stdexcept>

namespace search {

Config& Config::instance() {
    static Config config;
    return config;
}

Config::Config() {
    // Constructor - validate critical env vars
    if (postgres_host().empty()) {
        throw std::runtime_error("POSTGRES_HOST environment variable not set");
    }
}

std::string Config::get_env(const char* name, const std::string& default_val) const {
    const char* val = std::getenv(name);
    return val ? std::string(val) : default_val;
}

int Config::get_env_int(const char* name, int default_val) const {
    const char* val = std::getenv(name);
    if (!val) return default_val;
    try {
        return std::stoi(val);
    } catch (...) {
        return default_val;
    }
}

// Database configuration
std::string Config::postgres_host() const {
    return get_env("POSTGRES_HOST", "localhost");
}

int Config::postgres_port() const {
    return get_env_int("POSTGRES_PORT", 5432);
}

std::string Config::postgres_db() const {
    return get_env("POSTGRES_DB", "searchengine");
}

std::string Config::postgres_user() const {
    return get_env("POSTGRES_USER", "searchuser");
}

std::string Config::postgres_password() const {
    return get_env("POSTGRES_PASSWORD", "");
}

// S3/MinIO configuration
std::string Config::minio_endpoint() const {
    return get_env("MINIO_ENDPOINT", "minio:9000");
}

std::string Config::minio_access_key() const {
    return get_env("MINIO_ACCESS_KEY", "minioadmin");
}

std::string Config::minio_secret_key() const {
    return get_env("MINIO_SECRET_KEY", "minioadmin");
}

// Redis configuration
std::string Config::redis_host() const {
    return get_env("REDIS_HOST", "redis");
}

int Config::redis_port() const {
    return get_env_int("REDIS_PORT", 6379);
}

// RocksDB configuration
std::string Config::rocksdb_path() const {
    return get_env("ROCKSDB_PATH", "/data/rocksdb");
}

// Crawler configuration
int Config::crawl_rate_limit() const {
    return get_env_int("CRAWL_RATE_LIMIT", 5);
}

int Config::max_depth() const {
    return get_env_int("MAX_DEPTH", 3);
}

std::string Config::user_agent() const {
    return get_env("USER_AGENT", "BloomSearchBot/1.0");
}

} // namespace search
