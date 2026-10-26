# Open Mimic

Open Mimic is an independent, open-source project to extend the Pearl Mimic
Pro electronic drum module. Its goal is to build a maintainable operating
system and application for the existing hardware, with flexible pad support,
audio processing, MIDI, connectivity, and assisted calibration.

The repository brings together SD boot components, an ARM Linux system,
hardware drivers, diagnostic tools, and the workspace for the new
application. Hardware and firmware research lives in [research](research/).
The [platform](platform/) directory contains boot, kernel, driver, and
Buildroot userspace components; [software](software/) contains the C++/JUCE instrument application.

This project is independent of Pearl and Steven Slate Drums. Pearl firmware
and hardware binaries are retained for interoperability research without
claiming authorship or licensing rights. Platform source code is licensed under [GPL-2.0-only](LICENSE). The
C++/JUCE applications use [AGPL-3.0-only](software/LICENSE); third-party
components retain their own licenses.
