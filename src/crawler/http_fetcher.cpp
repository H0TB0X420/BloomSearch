#include "crawler/http_fetcher.h"
#include "common/logger.h"
#include "common/config.h"
#include <curl/curl.h>
#include <string>
#include <memory>
#include <thread>
#include <chrono>
#include <cmath>
#include <sstream>

namespace search {

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t totalSize = size * nmemb;
    if (userp) {
        userp->append(static_cast<char*>(contents), totalSize);
    }
    return totalSize;
}

bool HTTPFetcher::fetch(const std::string& url, std::string& content) {
    Logger::info("Starting fetch for: " + url);

    // Get configuration from singleton
    auto& config = Config::instance();
    std::string userAgent = config.user_agent();
    
    const int max_retries = 3;
    
    for (int attempt = 0; attempt <= max_retries; ++attempt) {
        content.clear(); 

        auto curlDeleter = [](CURL* c) { if (c) curl_easy_cleanup(c); };
        std::unique_ptr<CURL, decltype(curlDeleter)> curl(curl_easy_init(), curlDeleter);

        if (!curl) {
            Logger::error("Failed to initialize libcurl");
            return false;
        }

        auto slistDeleter = [](struct curl_slist* s) { if (s) curl_slist_free_all(s); };
        std::unique_ptr<struct curl_slist, decltype(slistDeleter)> header_ptr(nullptr, slistDeleter);

        curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &content);
        
        curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 2L);

        curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_MAXREDIRS, 3L);
        curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 30L);

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, ("User-Agent: " + userAgent).c_str());
        headers = curl_slist_append(headers, "Accept: text/html,application/xhtml+xml");
        headers = curl_slist_append(headers, "Accept-Language: en-US,en");
        curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers);
        header_ptr.reset(headers);

        CURLcode res = curl_easy_perform(curl.get());

        if (res != CURLE_OK) {
            std::string errorMsg = curl_easy_strerror(res);
            
            // Specific SSL Error Logging
            if (res == CURLE_PEER_FAILED_VERIFICATION) {
                Logger::error("SSL Verification Failed: " + errorMsg);
                return false; 
            } else if (res == CURLE_SSL_CONNECT_ERROR) {
                Logger::error("SSL Handshake Failed: " + errorMsg);
            } else if (res == CURLE_OPERATION_TIMEDOUT) {
                Logger::warn("Timeout error: " + errorMsg);
            } else {
                Logger::warn("Network error: " + errorMsg);
            }

            if (res != CURLE_PEER_FAILED_VERIFICATION && attempt < max_retries) {
                int sleep_time = static_cast<int>(std::pow(2, attempt)); 
                std::stringstream ss;
                ss << "Retrying in " << sleep_time << " seconds (Attempt " 
                   << (attempt + 1) << "/" << max_retries << ")";
                Logger::info(ss.str());
                std::this_thread::sleep_for(std::chrono::seconds(sleep_time));
                continue;
            } else {
                Logger::error("All retries exhausted or fatal SSL error");
                return false;
            }
        }

        long response_code;
        curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &response_code);
        
        long redirect_count;
        curl_easy_getinfo(curl.get(), CURLINFO_REDIRECT_COUNT, &redirect_count);
        if (redirect_count > 0) {
            char* final_url = nullptr;
            curl_easy_getinfo(curl.get(), CURLINFO_EFFECTIVE_URL, &final_url);
            std::stringstream ss;
            ss << "Redirected " << redirect_count << " times. Final URL: " 
               << (final_url ? final_url : "unknown");
            Logger::info(ss.str());
        }

        std::stringstream ss;
        ss << "HTTP Status Code: " << response_code;
        Logger::info(ss.str());

        if (response_code >= 200 && response_code < 300) {
            Logger::info("Fetch successful");
            return true;
        } else if (response_code >= 400 && response_code < 500) {
            Logger::warn("Client error (4xx) - not retrying");
            return false;
        } else if (response_code >= 500 && response_code < 600) {
            if (attempt < max_retries) {
                int sleep_time = static_cast<int>(std::pow(2, attempt));
                std::stringstream retry_ss;
                retry_ss << "Server error (" << response_code << "), retrying in " 
                         << sleep_time << " seconds (Attempt " 
                         << (attempt + 1) << "/" << max_retries << ")";
                Logger::warn(retry_ss.str());
                std::this_thread::sleep_for(std::chrono::seconds(sleep_time));
                continue;
            } else {
                Logger::error("Server error - all retries exhausted");
                return false;
            }
        } else {
            Logger::error("Unexpected status code: " + std::to_string(response_code));
            return false;
        }
    }

    return false;
}

} // namespace search