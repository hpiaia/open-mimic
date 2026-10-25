# Mimic kernel

`pearl-4.1.13/` contains files extracted from the original SSD system
partition: `zImage`, `am5718-edrums-v3.dtb`, and `lib/modules`. The kernel
configuration came from `/proc/config.gz` on the Mimic. `Module.symvers` was
reconstructed from Pearl modules and a build of the TI tree so that ZT9101
could be built with `CONFIG_MODVERSIONS=y`.

The **exact Pearl kernel source** is not available in this project. The
versioned binary supports experimental boot and comparison, but cannot be
rebuilt bit-for-bit from this repository. The ADC, McASP, LCD, and regulator
drivers appear to be built into this kernel; the replacement userspace uses
the original kernel while hardware support is reconstructed.

TI research baseline: https://git.ti.com/git/processor-sdk/processor-sdk-linux.git,
ref `processor-sdk-linux-02.00.01`, commit
`8dc66170d9c7e365e723dd3f515f0446b2b9c7bd`. This baseline does not
automatically include Pearl's changes.

Binary hashes are in [platform/prebuilt.sha256](../prebuilt.sha256). The
validated rescue SD used the same kernel and DTB hashes.
