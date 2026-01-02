#include "storage/rocksdb/rocksdb_client.h"
#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/write_batch.h>
#include <rocksdb/iterator.h>
#include <rocksdb/slice.h>

namespace search {

RocksDBClient::RocksDBClient() = default;

RocksDBClient::~RocksDBClient() {
    close();
}

RocksDBClient::RocksDBClient(RocksDBClient&& other) noexcept
    : db_(other.db_)
    , batch_(std::move(other.batch_))
    , path_(std::move(other.path_))
    , last_error_(std::move(other.last_error_)) {
    other.db_ = nullptr;
}

RocksDBClient& RocksDBClient::operator=(RocksDBClient&& other) noexcept {
    if (this != &other) {
        close();
        db_ = other.db_;
        batch_ = std::move(other.batch_);
        path_ = std::move(other.path_);
        last_error_ = std::move(other.last_error_);
        other.db_ = nullptr;
    }
    return *this;
}

bool RocksDBClient::open(const std::string& path) {
    return open(path, true, true);
}

bool RocksDBClient::open(const std::string& path, bool create_if_missing, 
                         bool compression_enabled) {
    if (db_) {
        close();
    }
    
    rocksdb::Options options;
    options.create_if_missing = create_if_missing;
    
    options.write_buffer_size = 64 * 1024 * 1024;  // 64MB write buffer
    options.max_write_buffer_number = 3;
    options.target_file_size_base = 64 * 1024 * 1024;  // 64MB SST files
    
    if (compression_enabled) {
        options.compression = rocksdb::kLZ4Compression;
    } else {
        options.compression = rocksdb::kNoCompression;
    }
    
    rocksdb::Status status = rocksdb::DB::Open(options, path, &db_);
    
    if (!status.ok()) {
        last_error_ = status.ToString();
        db_ = nullptr;
        return false;
    }
    
    path_ = path;
    return true;
}

void RocksDBClient::close() {
    if (batch_) {
        batch_.reset();
    }
    if (db_) {
        delete db_;
        db_ = nullptr;
    }
    path_.clear();
}

bool RocksDBClient::put(const std::string& key, const std::string& value) {
    if (!db_) {
        last_error_ = "Database not open";
        return false;
    }
    
    rocksdb::WriteOptions write_options;
    rocksdb::Status status = db_->Put(write_options, key, value);
    
    if (!status.ok()) {
        last_error_ = status.ToString();
        return false;
    }
    
    return true;
}

std::optional<std::string> RocksDBClient::get(const std::string& key) {
    if (!db_) {
        last_error_ = "Database not open";
        return std::nullopt;
    }
    
    std::string value;
    rocksdb::ReadOptions read_options;
    rocksdb::Status status = db_->Get(read_options, key, &value);
    
    if (status.IsNotFound()) {
        return std::nullopt;
    }
    
    if (!status.ok()) {
        last_error_ = status.ToString();
        return std::nullopt;
    }
    
    return value;
}

bool RocksDBClient::remove(const std::string& key) {
    if (!db_) {
        last_error_ = "Database not open";
        return false;
    }
    
    rocksdb::WriteOptions write_options;
    rocksdb::Status status = db_->Delete(write_options, key);
    
    if (!status.ok()) {
        last_error_ = status.ToString();
        return false;
    }
    
    return true;
}

bool RocksDBClient::exists(const std::string& key) {
    if (!db_) {
        return false;
    }
    
    std::string value;
    rocksdb::ReadOptions read_options;
    rocksdb::Status status = db_->Get(read_options, key, &value);
    
    return status.ok();
}

void RocksDBClient::begin_batch() {
    batch_ = std::make_unique<rocksdb::WriteBatch>();
}

void RocksDBClient::batch_put(const std::string& key, const std::string& value) {
    if (batch_) {
        batch_->Put(key, value);
    }
}

void RocksDBClient::batch_delete(const std::string& key) {
    if (batch_) {
        batch_->Delete(key);
    }
}

bool RocksDBClient::commit_batch() {
    if (!db_ || !batch_) {
        last_error_ = "No batch to commit";
        return false;
    }
    
    rocksdb::WriteOptions write_options;
    rocksdb::Status status = db_->Write(write_options, batch_.get());
    
    batch_.reset();
    
    if (!status.ok()) {
        last_error_ = status.ToString();
        return false;
    }
    
    return true;
}

void RocksDBClient::rollback_batch() {
    batch_.reset();
}

void RocksDBClient::iterate_prefix(const std::string& prefix,
                                   std::function<bool(const std::string&, const std::string&)> callback) {
    if (!db_) {
        return;
    }
    
    rocksdb::ReadOptions read_options;
    std::unique_ptr<rocksdb::Iterator> it(db_->NewIterator(read_options));
    
    for (it->Seek(prefix); it->Valid(); it->Next()) {
        std::string key = it->key().ToString();
        
        if (key.compare(0, prefix.size(), prefix) != 0) {
            break;
        }
        
        std::string value = it->value().ToString();        
        if (!callback(key, value)) {
            break;
        }
    }
}

std::vector<std::string> RocksDBClient::get_keys_with_prefix(const std::string& prefix) {
    std::vector<std::string> keys;
    
    iterate_prefix(prefix, [&keys](const std::string& key, const std::string&) {
        keys.push_back(key);
        return true;
    });
    
    return keys;
}

std::vector<std::pair<std::string, std::string>> RocksDBClient::get_all_with_prefix(const std::string& prefix) {
    std::vector<std::pair<std::string, std::string>> results;
    
    iterate_prefix(prefix, [&results](const std::string& key, const std::string& value) {
        results.emplace_back(key, value);
        return true;
    });
    
    return results;
}

size_t RocksDBClient::count_prefix(const std::string& prefix) {
    size_t count = 0;
    
    iterate_prefix(prefix, [&count](const std::string&, const std::string&) {
        ++count;
        return true;
    });
    
    return count;
}

void RocksDBClient::compact() {
    if (db_) {
        db_->CompactRange(nullptr, nullptr);
    }
}

uint64_t RocksDBClient::approximate_size(const std::string& start_key, const std::string& end_key) {
    if (!db_) {
        return 0;
    }
    
    rocksdb::Range range(start_key, end_key);
    uint64_t size = 0;
    db_->GetApproximateSizes(&range, 1, &size);
    
    return size;
}

} // namespace search