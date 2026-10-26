FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    bash bc binutils bzip2 ca-certificates cpio diffutils file findutils \
    g++ gcc git gzip cmake pkg-config libfreetype6-dev libfontconfig1-dev \
    libx11-dev libxi-dev libxext-dev libxrandr-dev libxinerama-dev libxcursor-dev libxcomposite-dev libxrender-dev libasound2-dev \
    make patch perl python3 rsync sed tar unzip wget which xz-utils \
    dosfstools mtools e2fsprogs && \
    rm -rf /var/lib/apt/lists/*
