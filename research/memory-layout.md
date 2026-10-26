# Observed memory layout

The running Pearl kernel and original `am5718-edrums-v3.dtb` describe one
1 GiB RAM bank at physical address `0x80000000`. `/proc/meminfo` reports
`MemTotal: 830776 kB`, or approximately 811.3 MiB available to the Linux memory
manager. This is the total used by the UI's RAM indicator, not installed RAM
or the current free-memory value.

The device tree reserves 192 MiB for `cmem_block_mem@a0000000` at
`0xa0000000`–`0xabffffff`. Kernel and other reservations account for roughly
another 20.7 MiB. `/proc/iomem` exposes System RAM at `0x80000000`–`0x9fffffff`
and `0xac000000`–`0xbfcfffff`.

DSP/IPU shared DMA pools are also described in reserved-memory. CMA/reusable
pools must not simply be subtracted again from MemTotal: their accounting
differs from the dedicated CMEM carveout.

The [CompuLab specification](https://www.compulab.com/products/computer-on-modules/cl-som-am57x-ti-am5728-am5718-system-on-module/)
lists RAM configurations from 512 MB to 4 GB. The current boot description
establishes the exposed address range; it does not independently identify the
physically populated DRAM. Bootloader detection or hardware identification is
still needed before asserting the installed capacity. No memory-map changes
were made during this investigation.
