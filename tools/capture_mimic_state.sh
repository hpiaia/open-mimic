#!/bin/sh
# Non-destructive runtime capture for a Pearl Mimic Pro shell.
#
# Usage on the device:
#   sh capture_mimic_state.sh /mnt/usb-flash/open-mimic-capture
#
# The script only reads system state and writes files under the output directory.

set -u

OUT="${1:-./open-mimic-capture}"
mkdir -p "$OUT" "$OUT/proc" "$OUT/sys" "$OUT/dev" "$OUT/logs" "$OUT/commands" "$OUT/files"

run() {
  name="$1"
  shift
  {
    echo "$ $*"
    "$@" 2>&1
  } > "$OUT/commands/$name.txt"
}

run_sh() {
  name="$1"
  shift
  {
    echo "$ $*"
    sh -c "$*" 2>&1
  } > "$OUT/commands/$name.txt"
}

copy_if_exists() {
  src="$1"
  dst="$2"
  if [ -e "$src" ]; then
    cp "$src" "$dst" 2>/dev/null || true
  fi
}

copy_tree_listing() {
  name="$1"
  dir="$2"
  if [ -e "$dir" ]; then
    find "$dir" -maxdepth 5 -print > "$OUT/$name.find.txt" 2>&1 || true
    ls -laR "$dir" > "$OUT/$name.ls.txt" 2>&1 || true
  fi
}

date > "$OUT/capture_date.txt" 2>&1 || true
uname -a > "$OUT/uname.txt" 2>&1 || true

run mount mount
run df df -h
run ps ps
run_sh cmdline "cat /proc/cmdline"
run_sh cpuinfo "cat /proc/cpuinfo"
run_sh meminfo "cat /proc/meminfo"
run_sh modules "cat /proc/modules"
run_sh partitions "cat /proc/partitions"
run_sh filesystems "cat /proc/filesystems"
run_sh interrupts "cat /proc/interrupts"
run_sh devices "cat /proc/devices"
run_sh mtd "cat /proc/mtd"
run_sh config_gz "zcat /proc/config.gz"
run_sh dmesg "dmesg"
run_sh dmesg_filtered "dmesg | grep -Ei 'ads|adc|iio|mcasp|asoc|alsa|sound|ttyS|serial|uart|8250|gpio|spi|mmc|sda|usb|mtd|ubi|root|kexec|mimic'"

run_sh dev_nodes "ls -la /dev /dev/ads7953.* /dev/mcasp.* /dev/regulators /dev/lcd-conf /dev/ttyS* 2>/dev/null"
run_sh major_minor "ls -l /dev/ads7953.* /dev/mcasp.* /dev/regulators /dev/lcd-conf /dev/ttyS9 2>/dev/null"
run_sh block_nodes "ls -la /dev/sd* /dev/mmcblk* /dev/mtd* /dev/ubi* 2>/dev/null"
run_sh fdisk "fdisk -l"
run_sh blkid "blkid"
run_sh lsblk "lsblk -a"

run_sh alsa_cards "cat /proc/asound/cards"
run_sh alsa_pcm "cat /proc/asound/pcm"
run_sh aplay "aplay -l"
run_sh arecord "arecord -l"

run_sh spi_devices "find /sys/bus/spi/devices -maxdepth 4 -type f -print -exec sh -c 'echo === $1; cat $1 2>/dev/null' sh {} \\;"
run_sh iio_devices "find /sys/bus/iio/devices -maxdepth 4 -type f -print -exec sh -c 'echo === $1; cat $1 2>/dev/null' sh {} \\;"
run_sh gpio_class "find /sys/class/gpio -maxdepth 3 -type f -print -exec sh -c 'echo === $1; cat $1 2>/dev/null' sh {} \\;"
run_sh tty_class "find /sys/class/tty -maxdepth 3 -type f -print -exec sh -c 'echo === $1; cat $1 2>/dev/null' sh {} \\;"
run_sh thermal "find /sys/class/thermal -maxdepth 3 -type f -print -exec sh -c 'echo === $1; cat $1 2>/dev/null' sh {} \\;"

copy_if_exists /etc/issue "$OUT/files/etc_issue"
copy_if_exists /etc/os-release "$OUT/files/etc_os-release"
copy_if_exists /etc/inittab "$OUT/files/etc_inittab"
copy_if_exists /etc/fstab "$OUT/files/etc_fstab"
copy_if_exists /etc/mtab "$OUT/files/etc_mtab"
copy_if_exists /var/log/messages "$OUT/logs/var_log_messages"
copy_if_exists /root/etc/edrums-version.txt "$OUT/files/root_etc_edrums-version.txt"
copy_if_exists /root/etc/edrums-build.txt "$OUT/files/root_etc_edrums-build.txt"
copy_if_exists /root/mimic_log.txt "$OUT/logs/root_mimic_log.txt"
copy_if_exists /root/hw_test_log.txt "$OUT/logs/root_hw_test_log.txt"

copy_tree_listing proc_device_tree /proc/device-tree
copy_tree_listing root_etc /root/etc
copy_tree_listing mnt_settings /mnt/settings
copy_tree_listing mnt_user_top /mnt/user

echo "Capture written to $OUT"
