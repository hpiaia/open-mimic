MIMIC_INPUT_ABI_VERSION = 1
MIMIC_INPUT_ABI_SITE = $(BR2_EXTERNAL_OPEN_MIMIC_PATH)/../compat/input-abi
MIMIC_INPUT_ABI_SITE_METHOD = local
MIMIC_INPUT_ABI_LICENSE = GPL-2.0-only
MIMIC_INPUT_ABI_INSTALL_STAGING = YES
MIMIC_INPUT_ABI_INSTALL_TARGET = NO

define MIMIC_INPUT_ABI_INSTALL_STAGING_CMDS
	python3 $(@D)/fix_input_header.py $(STAGING_DIR)/usr/include/linux/input.h
	printf '#include <linux/input.h>\n_Static_assert(sizeof(struct input_event) == 16, "evdev ABI mismatch");\n' | \
		$(TARGET_CC) -x c -c -o $(@D)/input-abi-check.o -
endef

$(eval $(generic-package))
