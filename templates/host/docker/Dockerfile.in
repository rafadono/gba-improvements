FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends build-essential cmake ninja-build pkg-config libsdl2-dev python3 git ca-certificates && rm -rf /var/lib/apt/lists/*
