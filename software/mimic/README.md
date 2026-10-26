# Mimic

C++/JUCE application for the Mimic Pro's 1024 × 600 LCD. It displays Wi-Fi
association, SSID and IPv4 address, alongside CPU/RAM usage, headphone gain, manual audio tests and Aux input
controls. Calibration and trigger monitoring remain placeholders. Touches
show a marker, count and coordinates.

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

Logs: `/tmp/mimic.log`, `/tmp/mimic-xorg.log`, `/tmp/mimic-display.log`.
The wheel adjusts headphone volume and toggles mute. Full-screen touch lists
select physical output routes, frequency and test level. Aux input has an
independent level and an optional link to headphone gain. Controls are applied
by `mimicd`, outside the display process.

A subdued header indicator shows total system CPU usage and used/total RAM in
MiB. A separate worker samples `/proc/stat` and `/proc/meminfo` once per second.
CPU is normalized to 0–100% across the system; RAM usage is `MemTotal` minus
`MemAvailable`, accounting for reclaimable cache. Missing readings appear as
`--`, including CPU until two samples are available. The UI thread only reads
status snapshots. Counter definitions follow the [Linux proc documentation](https://docs.kernel.org/filesystems/proc.html).
