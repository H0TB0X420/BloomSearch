#pragma once

#include <string>
#include <cstdlib>
#include <optional>

namespace search {

class Config {
public:
    static Config& instance();

    std::string postgres_host() const;
    int postgres_port() const;
    std::string postgres_db() const;
    std::string postgres_user() const;
    std::string postgres_password() const;

    std::string minio_endpoint() const;
    std::string minio_access_key() const;
    std::string minio_secret_key() const;

    std::string redis_host() const;
    int redis_port() const;

    std::string rocksdb_path() const;

    int crawl_rate_limit() const;
    int max_depth() const;
    std::string user_agent() const;

private:
    Config();
    
    std::string get_env(const char* name, const std::string& default_val = "") const;
    int get_env_int(const char* name, int default_val) const;
};

} // namespace search
