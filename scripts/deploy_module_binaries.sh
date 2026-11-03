#!/usr/bin/env bash
# Install a freshly cross-compiled daemon and/or UI on the module with backups.
#
#   scripts/deploy_module_binaries.sh [--host root@192.168.31.215] \
#       [--mimicd build/mimicd-stage2-arm] [--ui build/mimic-ui-stage2-arm] [--tag stage2]
#
# Backups are kept as /usr/bin/mimicd.before-TAG and /usr/bin/mimic.before-TAG.
# Roll back with: ssh HOST 'cp /usr/bin/mimicd.before-TAG /usr/bin/mimicd && /etc/init.d/S57mimic-pads restart'
set -euo pipefail

host=root@192.168.31.215
mimicd=""
ui=""
tag=stage2
while [ $# -gt 0 ]; do
  case "$1" in
    --host) host="$2"; shift 2 ;;
    --mimicd) mimicd="$2"; shift 2 ;;
    --ui) ui="$2"; shift 2 ;;
    --tag) tag="$2"; shift 2 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done
if [ -z "$mimicd" ] && [ -z "$ui" ]; then
  echo "nothing to deploy: pass --mimicd and/or --ui" >&2
  exit 2
fi
ssh_opts=(-o ConnectTimeout=8 -o BatchMode=yes)

if [ -n "$mimicd" ]; then
  echo "== daemon: $(shasum -a 256 "$mimicd" | cut -c1-16)…"
  scp -O "${ssh_opts[@]}" "$mimicd" "$host:/usr/bin/mimicd.$tag"
  ssh "${ssh_opts[@]}" "$host" "set -e
    chmod 755 /usr/bin/mimicd.$tag
    /usr/bin/mimicd.$tag pad-command >/dev/null 2>&1 || true
    [ -f /usr/bin/mimicd.before-$tag ] || cp /usr/bin/mimicd /usr/bin/mimicd.before-$tag
    /etc/init.d/S57mimic-pads stop || true
    cp /usr/bin/mimicd.$tag /usr/bin/mimicd && sync
    /etc/init.d/S57mimic-pads start
    sleep 4
    sha256sum /usr/bin/mimicd
    python3 - <<'EOF' 2>/dev/null || cat /run/mimic-pads/status.json | head -c 400
import json
s = json.load(open('/run/mimic-pads/status.json'))
print('running', s['running'], 'state', s['state'], 'engine', s['engine']['enabled'], s['engine']['active'], 'error', s['error'] or '-', 'live', s['engine']['error'] or '-')
for p in s['pads']:
    t = p['trigger']
    print(p['id'], 'cal', p['calibration']['state'], 'rule', t['measured']['calibrated'], 'model', p['model']['state'], 'mask', t['maskMs'], 'retrig', t['retriggerCancel'], 'mode', t['zoneMode'])
EOF"
fi

if [ -n "$ui" ]; then
  echo "== ui: $(shasum -a 256 "$ui" | cut -c1-16)…"
  scp -O "${ssh_opts[@]}" "$ui" "$host:/usr/bin/mimic.$tag"
  ssh "${ssh_opts[@]}" "$host" "set -e
    chmod 755 /usr/bin/mimic.$tag
    [ -f /usr/bin/mimic.before-$tag ] || cp /usr/bin/mimic /usr/bin/mimic.before-$tag
    cp /usr/bin/mimic.$tag /usr/bin/mimic && sync
    # tty1 respawns open-mimic-console, which restarts Xorg and the UI. When the
    # UI exits, the console execs a login shell on tty1; end that shell too.
    kill \$(pidof mimic) 2>/dev/null || true
    sleep 2
    for p in /proc/[0-9]*; do
      if [ \"\$(readlink \$p/fd/0 2>/dev/null)\" = /dev/tty1 ]; then kill -9 \$(basename \$p) 2>/dev/null || true; fi
    done
    sleep 10
    ps | grep -E '[ /]mimic$|Xorg' | grep -v grep || echo 'UI not visible in ps yet'
    sha256sum /usr/bin/mimic"
fi
echo "done"
