# syntax=docker/dockerfile:1.4

# ==============================================================================
# Development - Full tooling for building and testing
# ==============================================================================
FROM ubuntu:22.04 AS development

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    g++-12 \
    cmake \
    ninja-build \
    gdb \
    curl \
    libcurl4-openssl-dev \
    libpq-dev \
    libpqxx-dev \
    librocksdb-dev \
    libboost-system-dev \
    libboost-filesystem-dev \
    libzstd-dev \
    libgumbo-dev \
libhiredis-dev \    
ca-certificates \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*

# Set g++-12 as default
RUN update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-12 100 && \
    update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-12 100

WORKDIR /app

ENV CXX=g++-12
ENV CC=gcc-12

CMD ["/bin/bash"]
