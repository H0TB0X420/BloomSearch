#pragma once

#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <functional>

namespace rocksdb {
    class DB;
    class WriteBatch;
    class Iterator;
    class Options;
    class WriteOptions;
    class ReadOptions;
}

namespace search {

class RocksDBClient {
public:
    RocksDBClient();
    ~RocksDBClient();
    
    RocksDBClient(const RocksDBClient&) = delete;
    RocksDBClient& operator=(const RocksDBClient&) = delete;
    
    RocksDBClient(RocksDBClient&&) noexcept;
    RocksDBClient& operator=(RocksDBClient&&) noexcept;
    
    bool open(const std::string& path);
    
    bool open(const std::string& path, bool create_if_missing, 
              bool compression_enabled = true);
    
    void close();
    
    bool is_open() const { return db_ != nullptr; }
    
    const std::string& path() const { return path_; }
    
    bool put(const std::string& key, const std::string& value);
    
    std::optional<std::string> get(const std::string& key);
    
    bool remove(const std::string& key);
    
    bool exists(const std::string& key);
    
    void begin_batch();
    
    void batch_put(const std::string& key, const std::string& value);
    void batch_delete(const std::string& key);
    
    bool commit_batch();
    
    void rollback_batch();
    
    bool has_pending_batch() const { return batch_ != nullptr; }
    
    void iterate_prefix(const std::string& prefix,
                       std::function<bool(const std::string&, const std::string&)> callback);
    
    std::vector<std::string> get_keys_with_prefix(const std::string& prefix);
    
    std::vector<std::pair<std::string, std::string>> get_all_with_prefix(const std::string& prefix);
    
    size_t count_prefix(const std::string& prefix);
    
    void compact();
    
    uint64_t approximate_size(const std::string& start_key, const std::string& end_key);
    
    const std::string& last_error() const { return last_error_; }

private:
    rocksdb::DB* db_ = nullptr;
    std::unique_ptr<rocksdb::WriteBatch> batch_;
    std::string path_;
    std::string last_error_;
};

} // namespace search