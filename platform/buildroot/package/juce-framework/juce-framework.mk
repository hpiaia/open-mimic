JUCE_FRAMEWORK_VERSION = 9.0.4
JUCE_FRAMEWORK_SITE = $(call github,juce-framework,JUCE,$(JUCE_FRAMEWORK_VERSION))
JUCE_FRAMEWORK_LICENSE = AGPL-3.0-only (modules), various (bundled dependencies)
JUCE_FRAMEWORK_LICENSE_FILES = LICENSE.md
JUCE_FRAMEWORK_INSTALL_STAGING = YES
JUCE_FRAMEWORK_INSTALL_TARGET = NO

# Upstream resets host tool variables but leaves Buildroot's pkg-config sysroot.
define JUCE_FRAMEWORK_FIX_HOST_TOOLS
	python3 $(BR2_EXTERNAL_OPEN_MIMIC_PATH)/package/juce-framework/fix-host-tools.py $(@D)
endef
JUCE_FRAMEWORK_POST_PATCH_HOOKS += JUCE_FRAMEWORK_FIX_HOST_TOOLS

define JUCE_FRAMEWORK_INSTALL_STAGING_CMDS
	mkdir -p $(STAGING_DIR)/usr/share/juce
	cp -a $(@D)/modules $(@D)/extras $(@D)/CMakeLists.txt $(STAGING_DIR)/usr/share/juce/
endef

$(eval $(generic-package))
