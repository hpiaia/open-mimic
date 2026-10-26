# Open Mimic applications

Open Mimic uses C++17 and JUCE 9.0.4 for the instrument application. Python
is used for offline capture inspection and model-training tools.

- `mimic/`: JUCE components, touchscreen navigation and network status.
- `mimicd/`: ADC capture, JUCE analog output tests, and analog Aux control.
- `common/`: hardware access and the versioned capture format.
- `tests/`: capture-format and Aux gain regression checks independent of the hardware.

The interface and hardware engine have separate process ownership. Runtime control files publish headphone and Aux levels, and a Unix domain
socket cancels manual audio tests. The Aux controller runs continuously;
continuous PCM instrument playback and trigger inference remain to be implemented. See [architecture](../research/application-architecture.md) and
[JUCE integration decisions](../research/juce-integration.md).

## Build

Use `scripts/build_buildroot.sh` to cross-compile applications and the SD image.
The Buildroot package pins and verifies the JUCE source archive. No Rust
toolchain is required by the application.

For desktop development, obtain JUCE 9.0.4 and pass its source directory:

```sh
cmake -S software -B build/desktop -DJUCE_PATH=/path/to/JUCE-9.0.4
cmake --build build/desktop --parallel 2
ctest --test-dir build/desktop --output-on-failure
```

The same GUI runs in a desktop window. `OPEN_MIMIC_FULLSCREEN=1` enables the
instrument display mode. ADC access requires the Pearl Linux devices.

To test portable capture serialization and Aux gain controls without JUCE:

```sh
cmake -S software -B build/capture-tests -DMIMIC_BUILD_UI=OFF -DMIMIC_USE_JUCE=OFF
cmake --build build/capture-tests
ctest --test-dir build/capture-tests --output-on-failure
```

The new application sources in this directory use AGPL-3.0-only, matching the
open-source licensing route for the pinned JUCE modules. The original kernel
and third-party components retain their respective licences; this does not
relicense them. See `LICENSE` here and the upstream JUCE licence.

## Coding conventions

See [CODING_STYLE.md](CODING_STYLE.md) for C++ conventions, ownership and
threading rules, formatting commands and primary references.
