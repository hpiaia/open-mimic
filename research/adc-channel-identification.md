# Stable ADS7953 channel identification

The application originally treated the position of a conversion word inside a
32-byte DMA frame as its channel number. This was incorrect. Words retain the
ADS7953 physical channel address in bits 15:12 and the conversion in bits 11:0.
The [Texas Instruments datasheet](https://www.ti.com/lit/ds/symlink/ads7953.pdf)
describes this output format when channel-address output is enabled.

On the running Mimic, both raw rings contained tag order `15,0,1,...,14`.
Verified owner captures show other boots with order `0,1,...,15`, and a session
with ADC0 rotated while ADC1 remained aligned. This explains apparent changes
in pad wiring without any change to the physical input. An additional mapping
error wrapped the last ADC1 slot to global channel 0, which is ADC0.

`AdcFrame.h` now decodes all sixteen conversion words by tag, rejecting duplicate
or missing IDs. Live reads and calibration meters share this decoder. Raw
OMADC002 recordings retain the original bytes. New trial metadata explicitly
sets `channel_addressing: ads7953-tag-v1`; training reconstructs tagged channels
for these trials. Older metadata without that field continues to use its
historical DMA-position assignments. Existing verified recordings and learned
weights are preserved; their intended waveform extraction is unchanged.

Pad configuration version 2 uses physical channel IDs. Version 1 is rejected
with an explicit error rather than silently interpreting an old position map
as a physical map. The owner's old file is backed up outside Git before the
one-time migration. The measured physical map is restored while retaining the
current MIDI channel, note and zone settings. Tom 3 uses ADC1 channels 14/15;
Tom 4 uses ADC0 channels 0/1. The old Tom 4 DMA-position correction 1/2 remains
correct for its rotated historical recordings.

Different active sensors for one dual-zone pad must still be on the same ADC:
synchronization between converters has not been established. The configuration
command now rejects a cross-ADC pair before saving it, with the pad ID and the
global-index ranges in the message. Single-zone pads retain their secondary
physical assignment without interpreting that channel.

Tests cover all sixteen frame rotations, duplicate-tag rejection, ring wrap,
unchanged raw snapshots, physical-channel meters and rejected cross-ADC edits.
A service restart checks persistence and acquisition on the real device; a
cold-boot/physical-pad check by the owner remains the final hardware test.
