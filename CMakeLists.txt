cmake_minimum_required(VERSION 3.20)
project(BloomSearch VERSION 1.0.0 LANGUAGES CXX)

# C++23 standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler warnings
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic")

# Build type specific flags
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# Find required packages
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
find_package(Boost REQUIRED COMPONENTS system filesystem)

# PostgreSQL
find_package(PostgreSQL REQUIRED)
pkg_check_modules(PQXX REQUIRED IMPORTED_TARGET libpqxx)

# CURL
find_package(CURL REQUIRED)

find_package(OpenSSL REQUIRED)

# RocksDB
set(ROCKSDB_LIBRARIES rocksdb)

# Gumbo HTML parser
find_library(GUMBO_LIBRARY gumbo REQUIRED)
find_path(GUMBO_INCLUDE_DIR gumbo.h REQUIRED)

# Hiredis (Redis client)
find_library(HIREDIS_LIBRARY hiredis REQUIRED)
find_path(HIREDIS_INCLUDE_DIR hiredis/hiredis.h REQUIRED)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${PostgreSQL_INCLUDE_DIRS}
    ${PQXX_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
    ${Boost_INCLUDE_DIRS}
)

# Common library (shared code)
add_library(common STATIC
    src/common/config.cpp
    src/common/logger.cpp
    src/common/text_processor.cpp
    src/common/search_types.cpp
)
target_link_libraries(common
    ${Boost_LIBRARIES}
)

# Storage library (database clients)
add_library(storage STATIC
    src/storage/postgres_client.cpp
    src/storage/rocksdb/rocksdb_client.cpp
    src/storage/rocksdb/rocksdb_index_reader.cpp
    src/storage/s3_client.cpp
    src/storage/redis_client.cpp
)
target_link_libraries(storage
    common
    indexer_lib
    ${PostgreSQL_LIBRARIES}
    PkgConfig::PQXX
    ${ROCKSDB_LIBRARIES}
    ${CURL_LIBRARIES}
    ${HIREDIS_LIBRARY}
    OpenSSL::SSL
    OpenSSL::Crypto
    zstd
    Threads::Threads
)
# target_link_libraries(storage
#     common
#     ${PQXX_LIBRARIES}
#     ${ZSTD_LIBRARY}
#     ${ROCKSDB_LIBRARY}
#     ${HIREDIS_LIBRARY}
#     pthread
#     dl
# )

# Crawler library
add_library(crawler_lib STATIC
    src/crawler/url_frontier.cpp
    src/crawler/http_fetcher.cpp
    src/crawler/robots_parser.cpp
    src/crawler/url_frontier_mt.cpp
)
target_link_libraries(crawler_lib
    common
    storage
    ${CURL_LIBRARIES}
    ${Boost_LIBRARIES}
    pthread
)

# Indexer library
add_library(indexer_lib STATIC
    src/indexer/html_parser.cpp
    src/indexer/tokenizer.cpp
    src/indexer/index_builder.cpp
    src/indexer/ai_detector.cpp
)
target_link_libraries(indexer_lib
    common
    storage
    ${GUMBO_LIBRARY}
    ${ROCKSDB_LIBRARIES}
)
# Query library
add_library(query_lib STATIC
    src/query/query_parser.cpp
    src/query/ranker.cpp
    src/query/result_formatter.cpp
)
target_link_libraries(query_lib
    common
    storage
    ${ROCKSDB_LIBRARIES}
)

# Crawler executable
add_executable(crawler src/main_crawler.cpp)
target_link_libraries(crawler
    crawler_lib
    common
    storage
)

add_executable(crawler_mt src/main_crawler_mt.cpp)
target_link_libraries(crawler_mt
    crawler_lib
    indexer_lib
    storage
    common
    pthread
)

# Indexer executable
add_executable(indexer src/main_indexer.cpp)
target_link_libraries(indexer
    indexer_lib
    common
    storage
)

# Query engine executable
add_executable(query_engine src/main_query.cpp)
target_link_libraries(query_engine
    query_lib
    common
    storage
)

# Installation
install(TARGETS crawler indexer query_engine
    RUNTIME DESTINATION bin
)

option(BUILD_TESTS "Build test executables" ON)
if(BUILD_TESTS)
    add_executable(test_http_fetcher tests/test_http_fetcher.cpp)
    target_link_libraries(test_http_fetcher
        crawler_lib
        common
        storage
    )
    add_executable(test_robots_parser1 tests/robots_parser/test_robots_parser1.cpp)
    target_link_libraries(test_robots_parser1
        crawler_lib
        common
        storage
    )
    add_executable(test_robots_parser2 tests/robots_parser/test_robots_parser2.cpp)
    target_link_libraries(test_robots_parser2
        crawler_lib
        common
        storage
    )
    add_executable(test_robots_parser3 tests/robots_parser/test_robots_parser3.cpp)
    target_link_libraries(test_robots_parser3
        crawler_lib
        common
        storage
    )
    add_executable(test_robots_parser4 tests/robots_parser/test_robots_parser4.cpp)
    target_link_libraries(test_robots_parser4
        crawler_lib
        common
        storage
    )
    add_executable(test_robots_parser5 tests/robots_parser/test_robots_parser5.cpp)
    target_link_libraries(test_robots_parser5
        crawler_lib
        common
        storage
    )
    add_executable(test_robots_parser6 tests/robots_parser/test_robots_parser6.cpp)
    target_link_libraries(test_robots_parser6
        crawler_lib
        common
        storage
    )
    add_executable(test_url_frontier1 tests/url_frontier/test_url_frontier1.cpp)
    target_link_libraries(test_url_frontier1
        crawler_lib
        common
        storage
    )
    add_executable(test_url_frontier2 tests/url_frontier/test_url_frontier2.cpp)
    target_link_libraries(test_url_frontier2
        crawler_lib
        common
        storage
    )
    add_executable(test_url_frontier3 tests/url_frontier/test_url_frontier3.cpp)
    target_link_libraries(test_url_frontier3
        crawler_lib
        common
        storage
    )
    add_executable(test_url_frontier4 tests/url_frontier/test_url_frontier4.cpp)
    target_link_libraries(test_url_frontier4
        crawler_lib
        common
        storage
    )
    add_executable(test_url_frontier5 tests/url_frontier/test_url_frontier5.cpp)
    target_link_libraries(test_url_frontier5
        crawler_lib
        common
        storage
    )
    add_executable(test_url_frontier6 tests/url_frontier/test_url_frontier6.cpp)
    target_link_libraries(test_url_frontier6
        crawler_lib
        common
        storage
    )
    add_executable(test_crawler_integration tests/integration/test_crawler_integration.cpp)
    target_link_libraries(test_crawler_integration
        crawler_lib
        common
        storage
    )
    
    # PostgreSQL tests
    add_executable(test_postgres_client tests/storage/test_postgres_client.cpp)
    target_link_libraries(test_postgres_client
        storage
        common
    )
    add_executable(test_postgres_additional tests/storage/test_postgres_additional.cpp)
    target_link_libraries(test_postgres_additional
        storage
        common
    )
    add_executable(test_s3_client tests/storage/test_s3_client.cpp)
    target_link_libraries(test_s3_client
        storage
        common
    )
    add_executable(test_html_parser tests/indexer/test_html_parser.cpp)
    target_link_libraries(test_html_parser
        indexer_lib
        common
    )
    add_executable(test_tokenizer tests/indexer/test_tokenizer.cpp)
    target_link_libraries(test_tokenizer
        indexer_lib
        common
    )
    add_executable(test_rocksdb_client tests/storage/test_rocksdb_client.cpp)
    target_link_libraries(test_rocksdb_client
        indexer_lib
        common
    )
    add_executable(test_index_builder tests/indexer/test_index_builder.cpp)
    target_link_libraries(test_index_builder
        indexer_lib
        common
    )
    add_executable(test_redis_client tests/storage/test_redis_client.cpp)
    target_link_libraries(test_redis_client
        storage
        common
    )
    add_executable(test_crawler_mt tests/integration/test_crawler_mt.cpp)
    target_link_libraries(test_crawler_mt
        crawler_lib
        indexer_lib
        common
        pthread
    )
    add_executable(test_text_processor tests/common/test_text_processor.cpp)
    target_link_libraries(test_text_processor
        common
    )
    add_executable(test_query_parser tests/query/test_query_parser.cpp)
    target_link_libraries(test_query_parser
        query_lib 
        common
    )
    add_executable(test_ranker tests/query/test_ranker.cpp)
    target_link_libraries(test_ranker
        query_lib
        common
    )
    add_executable(test_rocksdb_index_reader tests/storage/test_rocksdb_index_reader.cpp)
    target_link_libraries(test_rocksdb_index_reader
        storage
        indexer_lib
        query_lib
        common
    )
    add_executable(test_url_filter tests/robots_parser/test_url_filter.cpp)
    target_link_libraries(test_url_filter crawler_lib common)
    add_executable(test_date_utils tests/common/test_date_utils.cpp)
    target_link_libraries(test_date_utils common)
    add_executable(test_content_date tests/common/test_content_date.cpp)
    target_link_libraries(test_content_date common)
endif()