#include "storage/s3_client.h"
#include <curl/curl.h>
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <zstd.h>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <functional>
#include <stdexcept>

namespace search {

static size_t write_callback(void* contents, size_t size, size_t nmemb, std::string* output) {
    size_t total_size = size * nmemb;
    output->append(static_cast<char*>(contents), total_size);
    return total_size;
}

bool S3Client::connect() {
    const char* endpoint = std::getenv("MINIO_ENDPOINT");
    const char* access_key = std::getenv("MINIO_ACCESS_KEY");
    const char* secret_key = std::getenv("MINIO_SECRET_KEY");
    const char* bucket = std::getenv("MINIO_BUCKET");
    
    if (!endpoint) {
        last_error_ = "Missing MINIO_ENDPOINT";
        return false;
    }
    
    return connect(endpoint, 
                   access_key ? access_key : "", 
                   secret_key ? secret_key : "", 
                   bucket ? bucket : "bloom-content");
}

bool S3Client::connect(const std::string& endpoint,
                       const std::string& access_key,
                       const std::string& secret_key,
                       const std::string& bucket) {
    endpoint_ = endpoint;
    access_key_ = access_key;
    secret_key_ = secret_key;
    bucket_ = bucket;
    
    // Remove trailing slash from endpoint
    if (!endpoint_.empty() && endpoint_.back() == '/') {
        endpoint_.pop_back();
    }
    
    // Add http:// if no scheme
    if (endpoint_.find("://") == std::string::npos) {
        endpoint_ = "http://" + endpoint_;
    }
    
    connected_ = true;
    return true;
}

bool S3Client::ensure_bucket_exists() {
    if (!connected_) {
        last_error_ = "Not connected";
        return false;
    }
    
    CURL* curl = curl_easy_init();
    if (!curl) return false;
    
    std::string url = endpoint_ + "/" + bucket_ + "/";
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    
    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    curl_easy_cleanup(curl);
    
    if (res == CURLE_OK && (http_code == 200 || http_code == 404)) {
        // 404 means bucket doesn't exist but MinIO is reachable
        // We'll let put() create it implicitly or fail with clear error
        return true;
    }
    
    last_error_ = "Cannot reach MinIO at " + url;
    return false;
}

bool S3Client::put(const std::string& key, const std::string& content, bool compress_flag) {
    if (!connected_) {
        last_error_ = "Not connected";
        return false;
    }
    
    std::string data = compress_flag ? compress(content) : content;
    std::string url = endpoint_ + "/" + bucket_ + "/" + key;
    std::string content_type = compress_flag ? "application/zstd" : "text/html";
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        last_error_ = "Failed to init curl";
        return false;
    }
    
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, ("Content-Type: " + content_type).c_str());
    
    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(data.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    
    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        last_error_ = curl_easy_strerror(res);
        return false;
    }
    
    if (http_code >= 200 && http_code < 300) {
        return true;
    }
    
    last_error_ = "HTTP " + std::to_string(http_code) + ": " + response;
    return false;
}

std::optional<std::string> S3Client::get(const std::string& key) {
    if (!connected_) {
        last_error_ = "Not connected";
        return std::nullopt;
    }
    
    std::string url = endpoint_ + "/" + bucket_ + "/" + key;
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        last_error_ = "Failed to init curl";
        return std::nullopt;
    }
    
    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    
    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        last_error_ = curl_easy_strerror(res);
        return std::nullopt;
    }
    
    if (http_code == 200) {
        // Check if compressed (Zstd magic number: 0x28 0xB5 0x2F 0xFD)
        if (response.size() >= 4 && 
            static_cast<uint8_t>(response[0]) == 0x28 &&
            static_cast<uint8_t>(response[1]) == 0xB5 &&
            static_cast<uint8_t>(response[2]) == 0x2F &&
            static_cast<uint8_t>(response[3]) == 0xFD) {
            return decompress(response);
        }
        return response;
    }
    
    last_error_ = "HTTP " + std::to_string(http_code);
    return std::nullopt;
}

bool S3Client::exists(const std::string& key) {
    if (!connected_) return false;
    
    std::string url = endpoint_ + "/" + bucket_ + "/" + key;
    
    CURL* curl = curl_easy_init();
    if (!curl) return false;
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    
    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    curl_easy_cleanup(curl);
    
    return (res == CURLE_OK && http_code == 200);
}

bool S3Client::remove(const std::string& key) {
    if (!connected_) return false;
    
    std::string url = endpoint_ + "/" + bucket_ + "/" + key;
    
    CURL* curl = curl_easy_init();
    if (!curl) return false;
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    
    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    curl_easy_cleanup(curl);
    
    return (res == CURLE_OK && http_code >= 200 && http_code < 300);
}

std::string S3Client::compress(const std::string& data) {
    if (data.empty()) {
        return data;
    }
    
    size_t max_compressed_size = ZSTD_compressBound(data.size());
    std::string compressed(max_compressed_size, '\0');
    
    size_t compressed_size = ZSTD_compress(
        compressed.data(), compressed.size(),
        data.data(), data.size(),
        3  // compression level
    );
    
    if (ZSTD_isError(compressed_size)) {
        return data;
    }
    
    compressed.resize(compressed_size);
    return compressed;
}

std::string S3Client::decompress(const std::string& compressed) {
    if (compressed.empty()) {
        return compressed;
    }
    
    if (compressed.size() < 4 ||
        static_cast<uint8_t>(compressed[0]) != 0x28 ||
        static_cast<uint8_t>(compressed[1]) != 0xB5 ||
        static_cast<uint8_t>(compressed[2]) != 0x2F ||
        static_cast<uint8_t>(compressed[3]) != 0xFD) {
        return compressed;
    }
    
    unsigned long long decompressed_size = ZSTD_getFrameContentSize(
        compressed.data(), compressed.size()
    );
    
    if (decompressed_size == ZSTD_CONTENTSIZE_ERROR ||
        decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN) {
        decompressed_size = compressed.size() * 10;  // Assume 10x expansion max
    }
    
    std::string decompressed(decompressed_size, '\0');
    
    size_t result = ZSTD_decompress(
        decompressed.data(), decompressed.size(),
        compressed.data(), compressed.size()
    );
    
    if (ZSTD_isError(result)) {
        return compressed;
    }
    
    decompressed.resize(result);
    return decompressed;
}

std::string S3Client::url_to_key(const std::string& url) {
    std::hash<std::string> hasher;
    size_t hash = hasher(url);
    
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << hash << ".html";
    return oss.str();
}

std::string S3Client::sha256_hash(const std::string& data) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(data.c_str()), data.length(), hash);
    return to_hex(std::vector<uint8_t>(hash, hash + SHA256_DIGEST_LENGTH));
}

std::string S3Client::hmac_sha256(const std::string& key, const std::string& data) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    unsigned int len = SHA256_DIGEST_LENGTH;
    
    HMAC(EVP_sha256(), 
         key.c_str(), static_cast<int>(key.length()),
         reinterpret_cast<const unsigned char*>(data.c_str()), data.length(),
         hash, &len);
    
    return std::string(reinterpret_cast<char*>(hash), len);
}

std::string S3Client::to_hex(const std::vector<uint8_t>& data) {
    std::ostringstream oss;
    for (uint8_t byte : data) {
        oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(byte);
    }
    return oss.str();
}

std::string S3Client::sign_request(const std::string& /*method*/,
                                   const std::string& /*path*/,
                                   const std::string& /*payload_hash*/,
                                   const std::string& /*content_type*/) const {
    return "";
}

} // namespace search