#pragma once

#include <string>
#include <optional>
#include <vector>
#include <cstdint>

namespace search {

//=============================================================================
// S3/MinIO Client for storing raw HTML content
//=============================================================================
class S3Client {
public:
    S3Client() = default;
    ~S3Client() = default;
    
    // Prevent copying
    S3Client(const S3Client&) = delete;
    S3Client& operator=(const S3Client&) = delete;
    
    // Allow moving
    S3Client(S3Client&&) = default;
    S3Client& operator=(S3Client&&) = default;
    
    //=========================================================================
    // Connection (1.4.1)
    //=========================================================================
    
    // Connect using environment variables:
    // MINIO_ENDPOINT, MINIO_ACCESS_KEY, MINIO_SECRET_KEY, MINIO_BUCKET
    bool connect();
    
    // Connect with explicit parameters
    bool connect(const std::string& endpoint, 
                 const std::string& access_key,
                 const std::string& secret_key,
                 const std::string& bucket);
    
    bool is_connected() const { return connected_; }
    
    // Create bucket if it doesn't exist
    bool ensure_bucket_exists();
    
    //=========================================================================
    // Storage Operations (1.4.3)
    //=========================================================================
    
    // Store content (compresses automatically if compress=true)
    bool put(const std::string& key, const std::string& content, bool compress = true);
    
    // Retrieve content (decompresses automatically if was compressed)
    std::optional<std::string> get(const std::string& key);
    
    // Check if key exists
    bool exists(const std::string& key);
    
    // Delete object
    bool remove(const std::string& key);
    
    //=========================================================================
    // Compression (1.4.2)
    //=========================================================================
    
    // Compress using Zstandard
    static std::string compress(const std::string& data);
    
    // Decompress Zstandard data
    static std::string decompress(const std::string& compressed);
    
    //=========================================================================
    // Utility
    //=========================================================================
    
    // Generate a key from URL (hash-based)
    static std::string url_to_key(const std::string& url);
    
    // Get last error message
    const std::string& last_error() const { return last_error_; }

private:
    std::string endpoint_;
    std::string access_key_;
    std::string secret_key_;
    std::string bucket_;
    bool connected_ = false;
    mutable std::string last_error_;
    
    // AWS Signature V4 helpers
    std::string sign_request(const std::string& method,
                            const std::string& path,
                            const std::string& payload_hash,
                            const std::string& content_type = "") const;
    
    static std::string sha256_hash(const std::string& data);
    static std::string hmac_sha256(const std::string& key, const std::string& data);
    static std::string to_hex(const std::vector<uint8_t>& data);
    
    // HTTP helpers
    bool http_put(const std::string& path, const std::string& data, 
                  const std::string& content_type = "application/octet-stream");
    std::optional<std::string> http_get(const std::string& path);
    bool http_head(const std::string& path);
    bool http_delete(const std::string& path);
};

} // namespace search