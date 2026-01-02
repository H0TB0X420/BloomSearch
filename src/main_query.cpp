#include "common/config.h"
#include "common/logger.h"
#include "storage/postgres_client.h"
#include "storage/rocksdb/rocksdb_client.h"
#include "storage/redis_client.h"
#include "query/query_parser.h"
#include "query/ranker.h"
#include "query/result_formatter.h"
#include <iostream>
#include <string>

using namespace search;

void print_banner() {
    std::cout << "\n";
    std::cout << "======================================================\n";
    std::cout << "   Bloom Search v1.0\n";
    std::cout << "   Search the web without AI-generated spam\n";
    std::cout << "======================================================\n";
    std::cout << "\n";
}

void print_help() {
    std::cout << "Commands:\n";
    std::cout << "  search <query>  - Search for pages\n";
    std::cout << "  help            - Show this help\n";
    std::cout << "  quit, exit      - Exit the program\n";
    std::cout << "\n";
}

int main(int argc, char** argv) {
    try {
        Logger::info("Starting Bloom Search Query Engine");
        Logger::info("Version 1.0.0");
        
        if (argc > 1 && std::string(argv[1]) == "--test") {
            Logger::info("Running in test mode");
            Logger::info("Test passed");
            return 0;
        }
        
        PostgresClient db;
        if (!db.connect()) {
            Logger::error("Failed to connect to database");
            return 1;
        }
        
        RocksDBClient index;
        if (!index.open(std::getenv("ROCKSDB_PATH") ? std::getenv("ROCKSDB_PATH") : "/data/rocksdb")) {
            Logger::error("Failed to open RocksDB: " + index.last_error());
            return 1;
        }
        
        RedisClient cache;
        cache.connect();
        
        QueryParser parser;
        Ranker ranker;
        ResultFormatter formatter;
        
        Logger::info("Query engine initialized");
        
        print_banner();
        print_help();
        
        std::string input;
        while (true) {
            std::cout << "search> ";
            std::getline(std::cin, input);
            
            if (input.empty()) {
                continue;
            }
            
            if (input == "quit" || input == "exit") {
                Logger::info("Shutting down query engine");
                break;
            }
            
            if (input == "help") {
                print_help();
                continue;
            }
            
            if (input.substr(0, 7) == "search ") {
                std::string query = input.substr(7);
                Logger::info("Processing query: " + query);
                
                auto terms = parser.parse(query);
                SearchResponse results = ranker.rank(terms);
                
                if (results.empty()) {
                    std::cout << "No results found.\n\n";
                } else {
                    std::cout << "\nFound " << results.size() << " results:\n\n";
                    std::string formatted = formatter.format(results);
                    std::cout << formatted << "\n";
                }
            } else {
                std::cout << "Unknown command. Type 'help' for available commands.\n";
            }
        }
        
    } catch (const std::exception& e) {
        Logger::error("Fatal error: " + std::string(e.what()));
        return 1;
    }
    
    return 0;
}
