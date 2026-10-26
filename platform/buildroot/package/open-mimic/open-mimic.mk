OPEN_MIMIC_VERSION = 0.2.0
OPEN_MIMIC_SITE = $(BR2_EXTERNAL_OPEN_MIMIC_PATH)/../../software
OPEN_MIMIC_SITE_METHOD = local
OPEN_MIMIC_LICENSE = AGPL-3.0-only
OPEN_MIMIC_LICENSE_FILES = LICENSE
OPEN_MIMIC_DEPENDENCIES = juce-framework freetype fontconfig xlib_libX11 xlib_libXcomposite xlib_libXext xlib_libXrender xlib_libXi
# JUCE bootstraps juceaide natively and restores PATH_ORIG while doing so.
# Keep Buildroot's target pkg-config wrapper out of that host-only build.
OPEN_MIMIC_CONF_ENV = PATH_ORIG=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
OPEN_MIMIC_CONF_OPTS = -DJUCE_PATH=$(STAGING_DIR)/usr/share/juce -DBUILD_TESTING=OFF

# Catch a missing/incompatible input UAPI patch before building the application.
define OPEN_MIMIC_CHECK_INPUT_ABI
	printf '#include <linux/input.h>\n_Static_assert(sizeof(struct input_event) == 16, "evdev ABI mismatch");\n' | \
		$(TARGET_CC) -x c -c -o $(@D)/input-abi-check.o -
endef
OPEN_MIMIC_PRE_CONFIGURE_HOOKS += OPEN_MIMIC_CHECK_INPUT_ABI

$(eval $(cmake-package))
