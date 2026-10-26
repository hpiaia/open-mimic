# Ghidra analysis environment

REA 6.8.0 is configured to use Ghidra 12.1.4 as the default native analysis
provider on the development Mac. This version belongs to REA's accepted
12.1.x release line. The Homebrew 12.2 formula falls outside that line.

The official release archive is `ghidra_12.1.4_PUBLIC_20260921.zip`, SHA-256:
`ddac49f903da9d5bac833e5cc79395098b9c33cfd3279be5f31bd00387d2d4db`.
Its digest was checked against the release asset metadata.

Installation: `~/Applications/ghidra_12.1.4_PUBLIC`.
JDK: Homebrew `openjdk@21`, with `JAVA_HOME` pointing to
`/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home`.
The GUI launch properties use the same JDK. `~/.local/bin/ghidra` links to
`ghidraRun` in that installation.

The archive did not include a macOS native decompiler. It was built from the
release's bundled sources using the existing Apple toolchain:

```sh
cd ~/Applications/ghidra_12.1.4_PUBLIC/Ghidra/Features/Decompiler/src/decompile/cpp
make -j4 ghidra_opt ARCH_TYPE='-arch arm64' OSDIR=mac_arm_64
```

The executable was installed as `os/mac_arm_64/decompile` beneath the
Decompiler feature. REA's scoped doctor check validated Ghidra version,
headless launcher, Mach-O arm64 decompiler, Java 21, and Codex registration.
An end-to-end REA decompile of the original Mimic ARM executable succeeded;
see [audio-output-validation.md](audio-output-validation.md) for its evidence.

Codex's user configuration sets `REA_ANALYSIS_PROVIDER=ghidra`,
`GHIDRA_INSTALL_DIR`, and `JAVA_HOME` for the REA MCP server. A backup was made
before modifying those environment entries. Existing MCP processes retain
their old launch environment and need reconnection to load the configuration.
Explicit CLI use is available immediately:

```sh
GHIDRA_INSTALL_DIR="$HOME/Applications/ghidra_12.1.4_PUBLIC" \
JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home \
rea decompile /path/to/mimic_pro 0x45a344 --provider ghidra --json
```

References: [official release](https://github.com/NationalSecurityAgency/ghidra/releases/tag/Ghidra_12.1.4_build),
[REA installation](https://github.com/morluto/rea/blob/main/docs/installation.md#ghidra).
