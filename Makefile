.PHONY: help build up down restart logs shell test clean rebuild prod-build prod-deploy

# Default target
help:
	@echo "Search Engine Docker Commands"
	@echo "=============================="
	@echo "  make build        - Build all Docker images"
	@echo "  make up           - Start all services"
	@echo "  make down         - Stop all services"
	@echo "  make restart      - Restart all services"
	@echo "  make logs         - Follow logs from all services"
	@echo "  make shell        - Enter development container"
	@echo "  make test         - Run tests in dev container"
	@echo "  make clean        - Remove containers and volumes"
	@echo "  make rebuild      - Clean rebuild from scratch"
	@echo ""
	@echo "Production:"
	@echo "  make prod-build   - Build production images"
	@echo "  make prod-deploy  - Deploy to production"

# Development commands
build:
	docker-compose build

up:
	docker-compose up -d
	@echo "Services started. Access:"
	@echo "  - PostgreSQL: localhost:5432"
	@echo "  - MinIO UI: http://localhost:9001"
	@echo "  - Redis: localhost:6379"
	@echo "  - Query Engine: localhost:8080"

down:
	docker-compose down

restart:
	docker-compose restart

logs:
	docker-compose logs -f

shell:
	docker-compose exec dev bash

# Build C++ code inside container
compile:
	docker-compose exec dev bash -c "cmake -B build -G Ninja && cmake --build build --parallel"

test:
	docker-compose exec dev bash -c "./build/crawler --test && ./build/indexer --test && ./build/query_engine --test"

clean:
	docker-compose down -v
	rm -rf build/

rebuild: clean build up

# Production commands
prod-build:
	docker-compose -f docker-compose.prod.yml build

prod-deploy:
	docker-compose -f docker-compose.prod.yml --env-file .env.prod up -d

prod-logs:
	docker-compose -f docker-compose.prod.yml logs -f

prod-stop:
	docker-compose -f docker-compose.prod.yml down
