#!/bin/sh
set -eu

target="$1"
project="${BR2_EXTERNAL_OPEN_MIMIC_PATH}/../.."
modules="${project}/platform/kernel/pearl-4.1.13/modules"

mkdir -p "${target}/lib/modules" "${target}/mnt/sd" \
  "${target}/mnt/pearl/os" "${target}/mnt/pearl/app" \
  "${target}/mnt/pearl/settings" "${target}/mnt/pearl/user" \
  "${target}/root/.ssh"
cp -a "${modules}/." "${target}/lib/modules/"
chmod 700 "${target}/root/.ssh"

# SSH is started only after loading a public key from the SD card.
rm -f "${target}/etc/init.d/S50dropbear"

# Start an already unlocked local shell after rcS rather than running getty.
# Its wrapper restores the framebuffer console before showing the prompt.
sed -i '/^tty1::respawn:\/usr\/sbin\/open-mimic-console$/d' "${target}/etc/inittab"
if grep -q '^tty1::respawn:' "${target}/etc/inittab"; then
  echo 'Unexpected tty1 getty entry in Buildroot inittab' >&2
  exit 1
fi
printf '\ntty1::respawn:/usr/sbin/open-mimic-console\n' >> "${target}/etc/inittab"
