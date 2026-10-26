include $(sort $(wildcard $(BR2_EXTERNAL_OPEN_MIMIC_PATH)/package/*/*.mk))

# Keep all evdev consumers on the Pearl ARM32 kernel event ABI with musl time64.
LIBEVDEV_DEPENDENCIES += mimic-input-abi
MTDEV_DEPENDENCIES += mimic-input-abi
XDRIVER_XF86_INPUT_EVDEV_DEPENDENCIES += mimic-input-abi
XSERVER_XORG_SERVER_DEPENDENCIES += mimic-input-abi
