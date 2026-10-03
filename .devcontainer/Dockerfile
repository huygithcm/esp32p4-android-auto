FROM mcr.microsoft.com/devcontainers/base:ubuntu-24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    python3 python3-pip python3-venv git cmake ninja-build \
    flex bison gperf ccache libffi-dev libssl-dev libusb-1.0-0 \
    && rm -rf /var/lib/apt/lists/*
