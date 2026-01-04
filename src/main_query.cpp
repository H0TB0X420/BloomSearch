#include "common/logger.h"
#include "storage/rocksdb/rocksdb_index_reader.h"
#include "query/query_parser.h"
#include "query/ranker.h"
#include "query/result_formatter.h"
#include <iostream>
#include <string>
#include <memory>
#include <cstdlib>

using namespace search;

struct SearchOptions {
    int limit = 10;
    int page = 1;
    bool show_help = false;
    bool show_version = false;
    bool test_mode = false;
    std::string query;
};

void print_banner() {
    std::cout << "\n";
    std::cout << "══════════════════════════════════════════════════════════\n";
    std::cout << "   🌸 BloomSearch v1.0\n";
    std::cout << "   Search the web, filter AI-generated content\n";
    std::cout << "══════════════════════════════════════════════════════════\n";
    std::cout << "\n";
}

void print_usage() {
    std::cout << "Usage: query_engine [OPTIONS] [QUERY]\n\n";
    std::cout << "Options:\n";
    std::cout << "  -n, --limit N     Number of results per page (default: 10)\n";
    std::cout << "  -p, --page N      Page number (default: 1)\n";
    std::cout << "  -h, --help        Show this help message\n";
    std::cout << "  -v, --version     Show version information\n";
    std::cout << "\n";
    std::cout << "Examples:\n";
    std::cout << "  query_engine bitcoin                   # Search for bitcoin\n";
    std::cout << "  query_engine -n 5 \"machine learning\"   # Top 5 results\n";
    std::cout << "  query_engine -p 2 -n 10 rust           # Page 2, 10 per page\n";
    std::cout << "  query_engine                           # Interactive mode\n";
    std::cout << "\n";
}

void print_repl_help() {
    std::cout << "\nCommands:\n";
    std::cout << "  <query>           Search for pages\n";
    std::cout << "  limit N           Set results per page\n";
    std::cout << "  page N            Go to page N of last results\n";
    std::cout << "  next / n          Next page\n";
    std::cout << "  prev / p          Previous page\n";
    std::cout << "  stats             Show index statistics\n";
    std::cout << "  help / ?          Show this help\n";
    std::cout << "  quit / q          Exit\n";
    std::cout << "\nQuery syntax:\n";
    std::cout << "  bitcoin                Simple search\n";
    std::cout << "  \"exact phrase\"         Phrase match\n";
    std::cout << "  bitcoin -scam          Exclude term\n";
    std::cout << "  bitcoin era:pre-ai     Only pre-AI content\n";
    std::cout << "  bitcoin era:post-ai    Only post-AI content\n";
    std::cout << "  bitcoin site:example.com   Filter by site\n";
    std::cout << "\n";
}

void print_stats(const RocksDBIndexReader& index) {
    std::cout << "\nIndex Statistics:\n";
    std::cout << "  Documents: " << index.get_total_docs() << "\n";
    std::cout << "  Avg length: " << static_cast<int>(index.get_avg_doc_length()) << " tokens\n";
    std::cout << "\n";
}

SearchOptions parse_args(int argc, char** argv) {
    SearchOptions opts;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            opts.show_help = true;
        } else if (arg == "-v" || arg == "--version") {
            opts.show_version = true;
        } else if (arg == "--test") {
            opts.test_mode = true;
        } else if ((arg == "-n" || arg == "--limit") && i + 1 < argc) {
            opts.limit = std::atoi(argv[++i]);
            if (opts.limit < 1) opts.limit = 10;
            if (opts.limit > 100) opts.limit = 100;
        } else if ((arg == "-p" || arg == "--page") && i + 1 < argc) {
            opts.page = std::atoi(argv[++i]);
            if (opts.page < 1) opts.page = 1;
        } else if (arg[0] != '-') {
            // Part of query
            if (!opts.query.empty()) opts.query += " ";
            opts.query += arg;
        }
    }
    
    return opts;
}

std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t");
    return s.substr(start, end - start + 1);
}

void execute_search(const std::string& query, Ranker& ranker, QueryParser& parser,
                   ResultFormatter& formatter, int limit, int page) {
    auto parsed = parser.parse(query);
    
    if (parsed.empty()) {
        std::cout << "Invalid query - no search terms found.\n\n";
        return;
    }
    
    // Get more results than needed for pagination
    int total_needed = limit * page;
    SearchResponse results = ranker.rank(parsed, total_needed);
    
    if (results.empty()) {
        std::cout << "No results found.\n\n";
        return;
    }
    
    // Calculate pagination
    int start_idx = (page - 1) * limit;
    int end_idx = std::min(start_idx + limit, static_cast<int>(results.results.size()));
    int total_pages = (results.total_matches + limit - 1) / limit;
    
    if (start_idx >= static_cast<int>(results.results.size())) {
        std::cout << "Page " << page << " is empty. Only " << total_pages << " pages available.\n\n";
        return;
    }
    
    // Slice results for current page
    std::vector<SearchResult> page_results(
        results.results.begin() + start_idx,
        results.results.begin() + end_idx
    );
    
    // Display header
    std::cout << "\nFound " << results.total_matches << " results";
    std::cout << " (" << results.search_time_ms << " ms)";
    if (total_pages > 1) {
        std::cout << "  [Page " << page << "/" << total_pages << "]";
    }
    std::cout << "\n────────────────────────────────────────\n\n";
    
    // Format and display results
    SearchResponse page_response;
    page_response.results = page_results;
    page_response.total_matches = results.total_matches;
    page_response.search_time_ms = results.search_time_ms;
    
    std::string formatted = formatter.format(page_response, start_idx + 1);
    std::cout << formatted;
    
    // Show pagination hint
    if (total_pages > 1) {
        std::cout << "────────────────────────────────────────\n";
        if (page < total_pages) {
            std::cout << "Type 'next' or 'n' for more results\n";
        }
        if (page > 1) {
            std::cout << "Type 'prev' or 'p' for previous page\n";
        }
        std::cout << "\n";
    }
}

int main(int argc, char** argv) {
    try {
        SearchOptions opts = parse_args(argc, argv);
        
        if (opts.show_help) {
            print_banner();
            print_usage();
            return 0;
        }
        
        if (opts.show_version) {
            std::cout << "BloomSearch v1.0\n";
            return 0;
        }
        
        if (opts.test_mode) {
            Logger::info("Test mode - exiting");
            return 0;
        }
        
        // Open index
        const char* rocksdb_env = std::getenv("ROCKSDB_PATH");
        std::string rocksdb_path = rocksdb_env ? rocksdb_env : "/data/rocksdb";
        
        auto index = std::make_shared<RocksDBIndexReader>();
        if (!index->open(rocksdb_path)) {
            std::cerr << "Error: Could not open search index at " << rocksdb_path << "\n";
            std::cerr << "Make sure you've run the indexer first.\n";
            return 1;
        }
        
        Logger::info("Index opened: " + std::to_string(index->get_total_docs()) + " documents");
        
        QueryParser parser;
        Ranker ranker(index);
        ResultFormatter formatter;
        
        // Direct query mode
        if (!opts.query.empty()) {
            execute_search(opts.query, ranker, parser, formatter, opts.limit, opts.page);
            return 0;
        }
        
        // Interactive REPL mode
        print_banner();
        print_repl_help();
        
        std::string last_query;
        int current_limit = opts.limit;
        int current_page = 1;
        
        std::string input;
        while (true) {
            std::cout << "search> ";
            
            if (!std::getline(std::cin, input)) {
                break;
            }
            
            input = trim(input);
            if (input.empty()) continue;
            
            // Commands
            if (input == "quit" || input == "exit" || input == "q") {
                break;
            }
            
            if (input == "help" || input == "?") {
                print_repl_help();
                continue;
            }
            
            if (input == "stats") {
                print_stats(*index);
                continue;
            }
            
            if (input == "next" || input == "n") {
                if (last_query.empty()) {
                    std::cout << "No previous search. Enter a query first.\n\n";
                    continue;
                }
                current_page++;
                execute_search(last_query, ranker, parser, formatter, current_limit, current_page);
                continue;
            }
            
            if (input == "prev" || input == "p") {
                if (last_query.empty()) {
                    std::cout << "No previous search. Enter a query first.\n\n";
                    continue;
                }
                if (current_page > 1) {
                    current_page--;
                    execute_search(last_query, ranker, parser, formatter, current_limit, current_page);
                } else {
                    std::cout << "Already on first page.\n\n";
                }
                continue;
            }
            
            if (input.substr(0, 6) == "limit ") {
                int new_limit = std::atoi(input.substr(6).c_str());
                if (new_limit >= 1 && new_limit <= 100) {
                    current_limit = new_limit;
                    std::cout << "Results per page set to " << current_limit << "\n\n";
                } else {
                    std::cout << "Limit must be between 1 and 100\n\n";
                }
                continue;
            }
            
            if (input.substr(0, 5) == "page ") {
                int new_page = std::atoi(input.substr(5).c_str());
                if (new_page >= 1 && !last_query.empty()) {
                    current_page = new_page;
                    execute_search(last_query, ranker, parser, formatter, current_limit, current_page);
                } else if (last_query.empty()) {
                    std::cout << "No previous search. Enter a query first.\n\n";
                } else {
                    std::cout << "Page must be >= 1\n\n";
                }
                continue;
            }
            
            // Strip optional "search " prefix
            std::string query = input;
            if (input.substr(0, 7) == "search ") {
                query = input.substr(7);
            }
            
            // Execute search
            last_query = query;
            current_page = 1;
            execute_search(query, ranker, parser, formatter, current_limit, current_page);
        }
        
        index->close();
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}