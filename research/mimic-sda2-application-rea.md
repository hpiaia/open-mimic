# Mimic Pro: REA analysis of the original application partition

Date: 2026-10-25. Static analysis only; no Mimic code was executed on the
Mac. Files came from verified copies of `/dev/sda1` and `/dev/sda2` on the
original SSD. Raw images remain in
`../../open-mimic-artifacts/original-ssd-2026-10-25/software-partition-images/`.

## Identity and contents

`sda2` contains `mimic_pro`, `mimic_pro.mup`, `start.sh`, `prod.sh`, `dev.sh`,
`load.sh`, `factory.sh`, and the symlink `edrums.sh -> /root/prod.sh`. The
executable is a 32-bit ARM ELF dynamically linked to X11, Xext, Xinerama,
FreeType, libc, and libstdc++. It has C++ symbols and debug information. Its
SHA-256 is
`b12895171c9cb8fd7db067c816ba28b4ae4817869a34654175f3e6dfb683fa8c`,
**identical** to the ELF previously extracted from the official 1.4.18
package. REA/Hopper reported 21,648 procedures and 3,535 typed string
objects (`ev_b959c9f8ae0cddb38bf121a6d1e456a140b6af8b5102128f8fefdfca6b030c2d`).

The `mimic_pro.mup` on the SSD differs from the downloaded 1.4.18 package:
SHA-256 `3720fb9ea6b9438f604b45fd1adf662313927eb0cc2ed72721e897d16f1b6a43`.
The local parser confirmed the `mimic_software_update` magic, format 1, an
uncompressed payload of 9,019,128 bytes, and CRC32C `b578f87d`. The ELF
extracted from this `.mup` has SHA-256
`d3f08b56bb797e31ebcfea6612170a6494c438d62e4f2e7a8ed6273132f53d96`.
There is not enough evidence to assign an exact commercial version to this
file. JUCE `getApplicationVersion()` returns `1.0.0` even in the official
1.4.18 executable, so that value does not identify the firmware version
(`ev_3f4d08162805741df52ecd0c163590892279d309795d5c075eb9023edeef6d68`).

## Startup

`start.sh` on `sda2` runs `/root/mimic_pro`. On `sda1`, Buildroot 2015.11.1
starts X via `/etc/init.d/S60graphic`; its `xinitrc` launches `xterm` with
`/root/edrums.sh`, which points to `prod.sh`, then `start.sh`. During JUCE
startup, the application creates `CEdrumsApp`, computation, disk, playback,
and decompression threads, and a full-screen window containing
`MainContentComponent` (`ev_ac1d2b18a8cce905837826d48b7dd6977fe8a4c2261401a1483674087920792d`).
These are statically observed calls, not measurements of execution on the
module.

### Safe mode and `dev.sh`

The original `edrums.sh -> /root/prod.sh` symlink is fixed on `sda2`.
`xinitrc` starts it without testing `safe_mode`; `prod.sh` always calls
`start.sh`. Within `start.sh`, the `safe_mode=...` parameter is extracted
from `/proc/cmdline` and exported to the application. The
`CEdrumsApp::CEdrumsApp` function reads `getenv("safe_mode")`, compares it
with `"1"`, and sets `mimic_pro_global_is_safe_mode` to 1 on a match
(`ev_8bbefbd88f94efe563c6460f9af1665d529d471cac01e97ffe62abe6ffde7653`).
The binary also contains `You Are In Safe Mode Now.`
(`ev_143b7ae5cf34671d5c4b1343dae1c14a425cf303ba383c87fdbd86bdc834d843`).
Thus, the observed safe mode reaches `mimic_pro`; the analyzed scripts do
not use it to select `dev.sh`. We still lack the original SPI U-Boot and an
observation of `/proc/cmdline` during normal boot with the wheel pressed, so
the stage that creates the kernel parameter remains unknown.

## Subsystems found in the binary

| Area | Evidence | Practical implication |
| --- | --- | --- |
| Updates | `CUpdaterDataStorage::processOSUpdate` reads the `mimic_software_update` container, checks CRC32C, and passes chunks to `readChunk` (`ev_84a2556b046a641565ad886bef650b3e249b8de4e092d7ae9e211fa9bfb63c95`). Earlier analysis traced `executeChunk` to `system@PLT` (`ev_1442a6996756355070ee2e3c046933859ddb710cd6bdd8b0621890b95e085f81`). | The format can carry update commands; changes require careful review on a copy. |
| Velocity calibration | `CCaptureTopVelocityPane` has `Show`, `Hide`, `timerCallback`, and `buttonClicked`; `CTrigPiezoTab::buttonClicked` opens capture (`ev_ebd694750b95bb966f58659ebdfede2990da39876400a76b0325d39424c54882`, `ev_64510e6251365e2a7785f060bb120c30536c317ad1da12b7df7c2eb2a1777716`). | An entry point exists for studying current calibration before proposing machine learning. |
| Crosstalk | `CXtalkManager::StartCaptureSession` prepares capture state; `ProcessAdcData` examines ADC samples and 12-bit values (`ev_9407a9cc45710a4ab8f68db156638884106f900e1db37e7bb7372d7f4ff8343f`, `ev_ba538a40f571a5ea23985602609da89971f3d2a6e8570e955ecbc9b91371ad4f`). | Signal input and crosstalk suppression have separate routines. Their full mathematics still requires instruction and data analysis. |
| Pad presets | `CTrigPadPresetStorage` includes Roland, Yamaha, ATV, Lemon, and TruTrack presets (`ev_48c1746ca7b355903393ce0b3844bba5aa670397a0af023393a5fe4f864c8c10`). `CTrigPadStorage::SaveCurrState_TPS` serializes descriptors through `CDataStorage` (`ev_0089e93abd105ded41bc8817e172dd4fd85d1dee88f6ed5e06340a3445f982e5`). | There are concrete formats and parameters to map for an external editor or calibrator. |
| USB | `CDirectoryManager::ReplyUSBDir` returns `/mnt/usb-flash/`; `CheckIfUSBFlashMounted` checks that path (`ev_d5fc3c82a35e77afbee2cf0a7eec3adbdd123e2127e59c96020e23dd09aa01b4`, `ev_678a221fe12fa78b21052bba10e681814c0c10171430665bf3c30c87df640eca`). | The application expects Linux-mounted media; this does not demonstrate a direct USB protocol with a PC. |
| Hardware | `CExtPortControl::Enable_12volt` writes to `/sys/class/gpio/...` (`ev_ff09fdec16563fad7c9e256831a073a649b19ab0c43ba980919c0d2f1d68e4f3`). | The application controls at least one output through sysfs GPIO. Its number and physical connection require a DTB/schematic or hardware test. |

The executable also contains `/dev/ads7953.0`, `/dev/ads7953.1`,
`/dev/mcasp.0`, `/dev/mcasp.1`, `/dev/lcd-conf`, and `/dev/regulators`.
These indicate interfaces used by the program; code references are needed
before asserting how each device is opened.

`CEDrumEngine2::fillAdcBuffEmulateInputLoad` is called directly by `Render`
(`ev_282af0c8e1c6f026e8beb5800fa1d7c8000ad7e3afee69b57038a6ae86342c77`,
`ev_d44dce4a5599455055dcaf69cede90a1be0bfd1cfa15ebe6b94e1230066f87b9`).
Pseudocode shows synthetic values being generated in the ADC buffer
(`ev_8c731582cbcd9561852ef553b5b5e01f48e70db2f1ccac5a80362a3549ff0e4e`),
but the condition enabling this path and its practical use are not yet
established. `StartAdcEmu` has an empty body and no resolved direct caller
(`ev_f38e15aced96bd5a8e25b847cbfacd014dca524066e4550801f5e6969f2a476f`,
`ev_fd10b5ab4fa1bf6565be1eecb31b777a29f387dc8e00d91be14ed519d0918bd0`).

The binary returns `/mnt/settings/module_settings/` as its settings
directory (`ev_e744f661ea49cfbbb53d2f02350056b3a30a1c3d7250c7cf1095ef0c94508fe2`).
Pad state uses `curr_pad_stor2.dat`
(`ev_b2be950d3fb026e8ad8798dd2bfdfd80749e7741177593683af90b598ad066ad`),
and crosstalk state uses `xtalk_settings.dat`
(`ev_f2cc4e57a77aebc15bc739bccbfc1c108ffedf3e0f3a62261a91daf84a854b1b`).
`CTrigPadDescriptor::SaveState` writes booleans, integers, floats, and
strings through `CDataStorage::AddValue`, and calls serialization routines
in `CTrigData` and `CTrigZoneData`
(`ev_03e3ab0d29fea11faad66a3ea160136ce02fa34dd411e75bd570d309c7628868`).
`CTrigData::SaveState` also includes `CPadInputRegSettings::SaveState`
(`ev_3ba332e4763d749e5c13a5c2157cf1e52ed35fbfa883944500d1d491997cb467`).
This confirms a layered structure for pad, zone, and input parameters;
the names and semantics of all fields remain to be reconstructed. These
files reside on the `sda3` settings partition, which was not cloned at the
owner's request. Their formats can first be studied through the ELF read
and write functions; real samples will require selective export.

## Limits and artifacts

Hopper provides reconstructed pseudocode rather than C++ source. Its public
API does not report complete function body ranges. Name searches cover
identified procedures; an absent name does not prove an absent function.
The older `.mup` was decompressed and validated locally but was not opened
in Hopper in this step. No runtime behavior was observed.

- REA evidence: `../../open-mimic-artifacts/original-ssd-2026-10-25/analysis/rea-sda2-application-evidence.json`.
- REA snapshot for continuation: `../../open-mimic-artifacts/original-ssd-2026-10-25/analysis/rea-sda2-application-snapshot.json`.
- Next investigation: trace ADC input to pad events, fields serialized in `curr_pad_stor2.dat`, and the velocity-capture flow. This provides a concrete basis for a machine-learning calibrator.
