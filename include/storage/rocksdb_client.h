#pragma once

#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <functional>

// Forward declare RocksDB types
namespace rocksdb {
    class DB;
    class WriteBatch;
    class Iterator;
    class Options;
    class WriteOptions;
    class ReadOptions;
}

namespace search {

//=============================================================================
// RocksDB Client - persistent key-value store for inverted index
//=============================================================================
class RocksDBClient {
public:
    RocksDBClient();
    ~RocksDBClient();
    
    // Prevent copying
    RocksDBClient(const RocksDBClient&) = delete;
    RocksDBClient& operator=(const RocksDBClient&) = delete;
    
    // Allow moving
    RocksDBClient(RocksDBClient&&) noexcept;
    RocksDBClient& operator=(RocksDBClient&&) noexcept;
    
    //=========================================================================
    // Database setup (2.3.1)
    //=========================================================================
    
    // Open database at path (creates if doesn't exist)
    bool open(const std::string& path);
    
    // Open with custom options
    bool open(const std::string& path, bool create_if_missing, 
              bool compression_enabled = true);
    
    // Close database
    void close();
    
    // Check if open
    bool is_open() const { return db_ != nullptr; }
    
    // Get database path
    const std::string& path() const { return path_; }
    
    //=========================================================================
    // Basic operations (2.3.2)
    //=========================================================================
    
    // Put a key-value pair
    bool put(const std::string& key, const std::string& value);
    
    // Get value for key (returns nullopt if not found)
    std::optional<std::string> get(const std::string& key);
    
    // Delete a key
    bool remove(const std::string& key);
    
    // Check if key exists
    bool exists(const std::string& key);
    
    //=========================================================================
    // Batch operations (2.3.3)
    //=========================================================================
    
    // Begin a batch (for efficient bulk writes)
    void begin_batch();
    
    // Add to current batch
    void batch_put(const std::string& key, const std::string& value);
    void batch_delete(const std::string& key);
    
    // Write the batch
    bool commit_batch();
    
    // Discard the batch
    void rollback_batch();
    
    // Check if batch is active
    bool has_pending_batch() const { return batch_ != nullptr; }
    
    //=========================================================================
    // Iteration (2.3.4)
    //=========================================================================
    
    // Iterate all keys with given prefix
    // Callback receives (key, value), return false to stop iteration
    void iterate_prefix(const std::string& prefix,
                       std::function<bool(const std::string&, const std::string&)> callback);
    
    // Get all keys with prefix
    std::vector<std::string> get_keys_with_prefix(const std::string& prefix);
    
    // Get all key-value pairs with prefix
    std::vector<std::pair<std::string, std::string>> get_all_with_prefix(const std::string& prefix);
    
    // Count keys with prefix
    size_t count_prefix(const std::string& prefix);
    
    //=========================================================================
    // Utility
    //=========================================================================
    
    // Compact the database (reclaim space)
    void compact();
    
    // Get approximate size of key range
    uint64_t approximate_size(const std::string& start_key, const std::string& end_key);
    
    // Get last error message
    const std::string& last_error() const { return last_error_; }

private:
    rocksdb::DB* db_ = nullptr;
    std::unique_ptr<rocksdb::WriteBatch> batch_;
    std::string path_;
    std::string last_error_;
};

} // namespace search