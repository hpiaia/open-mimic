OPEN_MIMIC_VERSION = 0.2.0
OPEN_MIMIC_SITE = $(BR2_EXTERNAL_OPEN_MIMIC_PATH)/../../software
OPEN_MIMIC_SITE_METHOD = local
OPEN_MIMIC_LICENSE = AGPL-3.0-only
OPEN_MIMIC_LICENSE_FILES = LICENSE
OPEN_MIMIC_DEPENDENCIES = mimic-input-abi juce-framework freetype fontconfig xlib_libX11 xlib_libXcomposite xlib_libXext xlib_libXrender xlib_libXi
# JUCE bootstraps juceaide natively and restores PATH_ORIG while doing so.
# Keep Buildroot's target pkg-config wrapper out of that host-only build.
OPEN_MIMIC_CONF_ENV = PATH_ORIG=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
OPEN_MIMIC_CONF_OPTS = -DJUCE_PATH=$(STAGING_DIR)/usr/share/juce -DBUILD_TESTING=OFF

$(eval $(cmake-package))
