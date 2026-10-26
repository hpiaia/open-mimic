# Mimic UI

C++/JUCE application for the Mimic Pro's 1024 × 600 LCD. It displays Wi-Fi
association, SSID and IPv4 address, with placeholder calibration, trigger-monitor
and audio-test buttons. Touches show a marker, count and coordinates.

The application uses JUCE components, labels, buttons, a LookAndFeel theme and
a Grid layout. It runs on Xorg's framebuffer backend with the existing Goodix
kernel driver and evdev input. No desktop environment, browser or proprietary
GPU userspace is required. The same sources build a desktop preview.

Network queries run on a worker thread with process timeouts; the message
thread refreshes widgets through a Timer. Workers never access GUI components.
The interface does not open ADC or McASP devices.

The console wrapper starts `open-mimic-display`, which owns a local X session
with TCP connections disabled and a private authorization cookie. Place a
`NOUI` file on the SD FAT partition to start the local shell instead. Display
session failures return to the shell. SSH is independent of the display.

Logs: `/tmp/mimic-ui.log`, `/tmp/mimic-xorg.log`, `/tmp/mimic-display.log`.
The calibration, trigger-monitor and audio buttons remain placeholders.

A subdued header indicator shows total system CPU usage and used/total RAM in
MiB. A separate worker samples `/proc/stat` and `/proc/meminfo` once per second.
CPU is normalized to 0–100% across the system; RAM usage is `MemTotal` minus
`MemAvailable`, accounting for reclaimable cache. Missing readings appear as
`--`, including CPU until two samples are available. The UI thread only reads
status snapshots. Counter definitions follow the [Linux proc documentation](https://docs.kernel.org/filesystems/proc.html).
