FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    bash bc binutils bzip2 ca-certificates cpio diffutils file findutils \
    g++ gcc git gzip \
    make patch perl python3 rsync sed tar unzip wget which xz-utils \
    dosfstools mtools e2fsprogs && \
    rm -rf /var/lib/apt/lists/*
