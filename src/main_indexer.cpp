//=============================================================================
// Bloom Search - Indexer Main Loop
// Processes crawled pages from storage into searchable inverted index
//=============================================================================

#include "common/config.h"
#include "common/logger.h"
#include "storage/postgres_client.h"
#include "storage/rocksdb_client.h"
#include "storage/s3_client.h"
#include "storage/redis_client.h"
#include "indexer/html_parser.h"
#include "indexer/tokenizer.h"
#include "indexer/ai_detector.h"
#include "indexer/index_builder.h"

#include <iostream>
#include <string>
#include <csignal>
#include <chrono>
#include <thread>
#include <atomic>
#include <cstdlib>

using namespace search;

//=============================================================================
// Global state for graceful shutdown
//=============================================================================
std::atomic<bool> g_shutdown_requested{false};

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        Logger::info("Shutdown requested...");
        g_shutdown_requested = true;
    }
}

//=============================================================================
// Statistics tracking
//=============================================================================
struct IndexerStats {
    uint64_t pages_indexed = 0;
    uint64_t pages_failed = 0;
    uint64_t bytes_processed = 0;
    std::chrono::steady_clock::time_point start_time;
    
    void start() {
        start_time = std::chrono::steady_clock::now();
    }
    
    void log_progress() const {
        auto elapsed = std::chrono::steady_clock::now() - start_time;
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
        
        double rate = seconds > 0 ? 
            static_cast<double>(pages_indexed) / seconds * 60.0 : 0;
        
        Logger::info("========================================");
        Logger::info("[PROGRESS] Pages indexed: " + std::to_string(pages_indexed));
        Logger::info("[PROGRESS] Pages failed:  " + std::to_string(pages_failed));
        Logger::info("[PROGRESS] Bytes processed: " + std::to_string(bytes_processed / 1024) + " KB");
        Logger::info("[PROGRESS] Elapsed: " + std::to_string(seconds) + "s");
        Logger::info("[PROGRESS] Rate: " + std::to_string(rate) + " pages/min");
        Logger::info("========================================");
    }
};

//=============================================================================
// Usage
//=============================================================================
void print_usage(const char* program) {
    std::cout << "Usage: " << program << " [options]\n"
              << "\nOptions:\n"
              << "  -n, --max-pages N   Index at most N pages (default: unlimited)\n"
              << "  -b, --batch-size N  Process N pages per batch (default: 100)\n"
              << "  --test              Run in test mode (verify connections)\n"
              << "  -h, --help          Show this help message\n"
              << "\nEnvironment variables:\n"
              << "  POSTGRES_CONNECTION  PostgreSQL connection string\n"
              << "  S3_ENDPOINT          MinIO/S3 endpoint URL\n"
              << "  S3_ACCESS_KEY        MinIO/S3 access key\n"
              << "  S3_SECRET_KEY        MinIO/S3 secret key\n"
              << "  S3_BUCKET            S3 bucket name\n"
              << "  ROCKSDB_PATH         Path to RocksDB database\n"
              << "  REDIS_HOST           Redis host (optional)\n"
              << std::endl;
}

//=============================================================================
// Main
//=============================================================================
int main(int argc, char** argv) {
    // Parse command line arguments
    int64_t max_pages = -1;
    int batch_size = 100;
    int progress_interval = 10;
    bool test_mode = false;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--test") {
            test_mode = true;
        } else if ((arg == "-n" || arg == "--max-pages") && i + 1 < argc) {
            max_pages = std::stoll(argv[++i]);
        } else if ((arg == "-b" || arg == "--batch-size") && i + 1 < argc) {
            batch_size = std::stoi(argv[++i]);
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            print_usage(argv[0]);
            return 1;
        }
    }
    
    // Setup signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "    Bloom Search - Indexer                                  \n";
    std::cout << "============================================================\n\n";
    
    try {
        Logger::info("Starting Bloom Search Indexer");
        Logger::info("Version 1.0.0");
        
        //=====================================================================
        // Test mode - verify connections and exit
        //=====================================================================
        if (test_mode) {
            Logger::info("Running in test mode");
            Logger::info("Configuration:");
            auto& cfg = Config::instance();
            Logger::info("  RocksDB path: " + cfg.rocksdb_path());
            Logger::info("Test passed");
            return 0;
        }
        
        //=====================================================================
        // Initialize components
        //=====================================================================
        Logger::info("Initializing components...");
        
        // PostgreSQL
        PostgresClient db;
        if (!db.connect()) {
            Logger::error("Failed to connect to PostgreSQL: " + db.last_error());
            return 1;
        }
        Logger::info("Connected to PostgreSQL");
        
        // Schema migration - add indexed column if missing
        db.execute("ALTER TABLE pages ADD COLUMN IF NOT EXISTS indexed BOOLEAN DEFAULT FALSE");
        db.execute("CREATE INDEX IF NOT EXISTS idx_pages_indexed ON pages(indexed) WHERE indexed = FALSE");
        
        // MinIO/S3
        S3Client s3;
        const char* s3_endpoint = std::getenv("S3_ENDPOINT");
        const char* s3_access_key = std::getenv("S3_ACCESS_KEY");
        const char* s3_secret_key = std::getenv("S3_SECRET_KEY");
        const char* s3_bucket = std::getenv("S3_BUCKET");
        
        if (!s3.connect(
            s3_endpoint ? s3_endpoint : "http://minio:9000",
            s3_access_key ? s3_access_key : "minioadmin",
            s3_secret_key ? s3_secret_key : "minioadmin",
            s3_bucket ? s3_bucket : "bloom-content"
        )) {
            Logger::error("Failed to connect to S3/MinIO: " + s3.last_error());
            return 1;
        }
        Logger::info("Connected to MinIO/S3");
        
        // RocksDB Index
        const char* rocksdb_path = std::getenv("ROCKSDB_PATH");
        IndexBuilder index_builder;
        if (!index_builder.initialize(rocksdb_path ? rocksdb_path : "/data/rocksdb")) {
            Logger::error("Failed to initialize index: " + index_builder.last_error());
            return 1;
        }
        Logger::info("Initialized RocksDB index (docs: " + 
                     std::to_string(index_builder.document_count()) + ", terms: " +
                     std::to_string(index_builder.term_count()) + ")");
        
        // Redis (optional)
        RedisClient redis;
        if (redis.connect()) {
            Logger::info("Connected to Redis cache");
        } else {
            Logger::info("Redis not available, running without cache");
        }
        
        Logger::info("Indexer components initialized");
        
        //=====================================================================
        // Main indexing loop
        //=====================================================================
        IndexerStats stats;
        stats.start();
        
        int64_t total_processed = 0;
        
        Logger::info("Starting indexer (max_pages: " + 
                     (max_pages > 0 ? std::to_string(max_pages) : "unlimited") + 
                     ", batch_size: " + std::to_string(batch_size) + ")");
        
        while (!g_shutdown_requested) {
            // Check if we've hit the limit
            if (max_pages > 0 && total_processed >= max_pages) {
                Logger::info("Reached max pages limit (" + std::to_string(max_pages) + ")");
                break;
            }
            
            // Get batch of pending pages
            int batch_limit = batch_size;
            if (max_pages > 0) {
                batch_limit = std::min(batch_limit, static_cast<int>(max_pages - total_processed));
            }
            
            auto pending = db.get_unindexed_pages(batch_limit);
            
            if (pending.empty()) {
                Logger::info("No pending pages, waiting 5 seconds...");
                std::this_thread::sleep_for(std::chrono::seconds(5));
                continue;
            }
            
            Logger::info("Processing batch of " + std::to_string(pending.size()) + " pages");
            
            // Process each page in batch
            for (const auto& page : pending) {
                if (g_shutdown_requested) break;
                
                // Fetch HTML from S3
                auto html_opt = s3.get(page.content_hash);
                if (!html_opt) {
                    Logger::info("[FAIL] " + page.url + " - S3 fetch failed: " + s3.last_error());
                    stats.pages_failed++;
                    total_processed++;
                    continue;
                }
                
                std::string html = *html_opt;
                stats.bytes_processed += html.size();
                
                // Index the document
                if (!index_builder.index_document(static_cast<uint64_t>(page.id), page.url, html)) {
                    Logger::info("[FAIL] " + page.url + " - Index failed: " + index_builder.last_error());
                    stats.pages_failed++;
                    total_processed++;
                    continue;
                }
                
                // Mark as indexed in PostgreSQL
                if (!db.mark_page_indexed(page.id)) {
                    Logger::info("[FAIL] " + page.url + " - DB update failed: " + db.last_error());
                    stats.pages_failed++;
                    total_processed++;
                    continue;
                }
                
                stats.pages_indexed++;
                total_processed++;
                
                // Log individual page (debug level)
                // Logger::info("[OK] " + page.url + " (" + std::to_string(html.size()) + " bytes)");
                
                // Log progress periodically
                if (stats.pages_indexed % progress_interval == 0 && stats.pages_indexed > 0) {
                    stats.log_progress();
                }
            }
            
            // Flush index after each batch
            index_builder.flush();
        }
        
        //=====================================================================
        // Shutdown
        //=====================================================================
        Logger::info("Flushing index...");
        index_builder.flush();
        
        Logger::info("Indexer stopped");
        stats.log_progress();
        
        Logger::info("Final index stats: " + 
                     std::to_string(index_builder.document_count()) + " docs, " +
                     std::to_string(index_builder.term_count()) + " terms");
        
    } catch (const std::exception& e) {
        Logger::error("Fatal error: " + std::string(e.what()));
        return 1;
    }
    
    return 0;
}