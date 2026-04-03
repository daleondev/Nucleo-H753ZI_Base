# Use the official devcontainer base image so the expected vscode user exists.
FROM mcr.microsoft.com/devcontainers/base:ubuntu-24.04

# Prevent interactive prompts (like timezone selections) from blocking the build
ENV DEBIAN_FRONTEND=noninteractive

# Update the package index, install the required packages, and clean up
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    cmake \
    g++ \
    gcc-arm-none-eabi \
    git \
    libstdc++-arm-none-eabi-newlib \
    openocd \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# (Optional) Set the default working directory
WORKDIR /workspace

# Set the default command to run when the container starts
CMD ["/bin/bash"]