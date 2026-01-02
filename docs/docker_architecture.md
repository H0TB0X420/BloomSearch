
```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          DEVELOPMENT ENVIRONMENT                             │
│                     (Everything runs in Docker)                              │
│                                                                              │
│  Your Machine                                                                │
│  ├── /project/src          (bind mounted → hot reload)                      │
│  ├── /project/include      (bind mounted → hot reload)                      │
│  ├── docker-compose.yml                                                      │
│  └── docker-compose.prod.yml                                                 │
│                                                                              │
└──────────────────────────────────────────────────────────────────────────────┘
                                   │
                                   │ docker-compose up
                                   ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                        DOCKER COMPOSE STACK (DEV)                            │
│                                                                              │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │  query-engine  (Container)                                          │   │
│  │  ┌──────────────────────────────────────────────────────────────┐  │   │
│  │  │  C++20 Binary (built with g++-11 or clang-15)                │  │   │
│  │  │  • Listens on localhost:8080                                 │  │   │
│  │  │  • Reads from RocksDB volume                                 │  │   │
│  │  │  • Queries PostgreSQL for metadata                           │  │   │
│  │  │  • Redis for caching                                         │  │   │
│  │  └──────────────────────────────────────────────────────────────┘  │   │
│  │  Volumes:                                                           │   │
│  │    • ./src:/app/src:ro           (read-only, live code)           │   │
│  │    • ./include:/app/include:ro   (read-only, live code)           │   │
│  │    • rocksdb-data:/data/rocksdb  (persistent index)               │   │
│  │    • ./build:/app/build          (compiled artifacts)             │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                   │                                         │
│                                   │ Connects to                             │
│                                   ▼                                         │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │  crawler  (Container)                                               │   │
│  │  ┌──────────────────────────────────────────────────────────────┐  │   │
│  │  │  C++20 Binary (separate executable)                          │  │   │
│  │  │  • Fetches URLs with libcurl                                 │  │   │
│  │  │  • Respects robots.txt                                       │  │   │
│  │  │  • Rate limiting per domain                                  │  │   │
│  │  │  • Saves to S3/MinIO                                         │  │   │
│  │  └──────────────────────────────────────────────────────────────┘  │   │
│  │  Volumes:                                                           │   │
│  │    • ./src:/app/src:ro                                             │   │
│  │    • ./include:/app/include:ro                                     │   │
│  │    • crawler-state:/data/state   (checkpoint data)                │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                   │                                         │
│                                   │ Feeds to                                │
│                                   ▼                                         │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │  indexer  (Container)                                               │   │
│  │  ┌──────────────────────────────────────────────────────────────┐  │   │
│  │  │  C++20 Binary (batch processing)                             │  │   │
│  │  │  • Reads from PostgreSQL crawl queue                         │  │   │
│  │  │  • Fetches HTML from S3                                      │  │   │
│  │  │  • Parses and tokenizes                                      │  │   │
│  │  │  • AI detection scoring                                      │  │   │
│  │  │  • Writes to RocksDB index                                   │  │   │
│  │  └──────────────────────────────────────────────────────────────┘  │   │
│  │  Volumes:                                                           │   │
│  │    • ./src:/app/src:ro                                             │   │
│  │    • rocksdb-data:/data/rocksdb  (same volume as query-engine)    │   │
│  │    • ai-models:/data/models      (ML model files)                 │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                              │
│       All containers connect to shared network: search-engine-net           │
│                                                                              │
└──────────────────────────────────┬───────────────────────────────────────────┘
                                   │
                   ┌───────────────┼────────────────┬──────────────┐
                   │               │                │              │
                   ▼               ▼                ▼              ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                       SUPPORTING SERVICES (Docker)                           │
│                                                                              │
│  ┌──────────────────┐  ┌──────────────────┐  ┌─────────────┐  ┌─────────┐ │
│  │   postgres:15    │  │   minio/minio    │  │ redis:7     │  │ adminer │ │
│  │                  │  │                  │  │             │  │ (DB UI) │ │
│  │  Port: 5432      │  │  Port: 9000/9001 │  │ Port: 6379  │  │:8081    │ │
│  │                  │  │                  │  │             │  │         │ │
│  │  Volume:         │  │  Volume:         │  │  Volume:    │  │         │ │
│  │   postgres-data  │  │   minio-data     │  │   redis-data│  │         │ │
│  └──────────────────┘  └──────────────────┘  └─────────────┘  └─────────┘ │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
