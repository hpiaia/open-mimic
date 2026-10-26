#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
version=2025.02.19
archive="$repo_dir/build/buildroot-src/buildroot-$version.tar.xz"
expected=e5e58bff1da91f022f60cefdfe280140c08023cd0aeec1e5f6f9da0ea866d69c
volume=open-mimic-buildroot

mkdir -p "$repo_dir/build/buildroot-src"
if [ ! -f "$archive" ]; then
  curl --fail --location --output "$archive" "https://buildroot.org/downloads/buildroot-$version.tar.xz"
fi
actual="$(shasum -a 256 "$archive" | awk '{print $1}')"
if [ "$actual" != "$expected" ]; then
  echo "Buildroot archive hash mismatch: $actual" >&2
  exit 1
fi
docker build --platform linux/arm64 -t open-mimic:buildroot-host "$repo_dir/platform/buildroot"
docker volume create "$volume" >/dev/null
docker run --rm --platform linux/arm64 \
  --env FORCE_UNSAFE_CONFIGURE=1 \
  --mount "type=bind,src=$repo_dir,dst=/project" \
  --mount "type=volume,src=$volume,dst=/work" \
  -w /work \
  open-mimic:buildroot-host sh -ec '
    if [ ! -f /work/buildroot-2025.02.19/Makefile ]; then
      tar -xf /project/build/buildroot-src/buildroot-2025.02.19.tar.xz -C /work
    fi
    if [ -d /project/build/buildroot-src/buildroot-2025.02.19/dl ]; then
      mkdir -p /work/buildroot-2025.02.19/dl
      cp -an /project/build/buildroot-src/buildroot-2025.02.19/dl/. \
        /work/buildroot-2025.02.19/dl/
    fi
    cd /work/buildroot-2025.02.19
    make O=/work/output \
      BR2_EXTERNAL=/project/platform/buildroot open_mimic_defconfig
    # Enabling C++ in an existing C-only toolchain needs a GCC rebuild.
    if [ -x /work/output/host/bin/arm-buildroot-linux-musleabihf-gcc ] &&
       [ ! -x /work/output/host/bin/arm-buildroot-linux-musleabihf-g++ ]; then
      make O=/work/output host-gcc-final-dirclean
      make O=/work/output BR2_JLEVEL=4 host-gcc-final
    fi
    if [ -x /work/output/host/bin/arm-buildroot-linux-musleabihf-g++ ] &&
       [ ! -e /work/output/target/usr/lib/libstdc++.so.6 ]; then
      make O=/work/output gcc-final-reinstall
    fi
    # Old evdev objects must be rebuilt when adding the time64 UAPI backport.
    if [ ! -e /work/output/build/mimic-input-abi-1/.stamp_staging_installed ] &&
       find /work/output/build -maxdepth 1 -type d -name "libevdev-*" -print -quit | grep -q .; then
      make O=/work/output libevdev-dirclean mtdev-dirclean \
        xdriver_xf86-input-evdev-dirclean xserver_xorg-server-dirclean
    fi
    make O=/work/output BR2_JLEVEL=4
    mkdir -p /project/build/buildroot-output/images
    cp /work/output/images/rootfs.ext4 /project/build/buildroot-output/images/
    cp /work/output/.config /project/build/buildroot-output/.config
  '
echo "Buildroot rootfs: $repo_dir/build/buildroot-output/images/rootfs.ext4"
python3 "$repo_dir/scripts/build_buildroot_boot.py"
python3 "$repo_dir/scripts/build_sd_image.py"
