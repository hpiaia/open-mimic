JUCE_FRAMEWORK_VERSION = 9.0.4
JUCE_FRAMEWORK_SITE = $(call github,juce-framework,JUCE,$(JUCE_FRAMEWORK_VERSION))
JUCE_FRAMEWORK_LICENSE = AGPL-3.0-only (modules), various (bundled dependencies)
JUCE_FRAMEWORK_LICENSE_FILES = LICENSE.md
JUCE_FRAMEWORK_INSTALL_STAGING = YES
JUCE_FRAMEWORK_INSTALL_TARGET = NO

define JUCE_FRAMEWORK_INSTALL_STAGING_CMDS
	mkdir -p $(STAGING_DIR)/usr/share/juce
	cp -a $(@D)/modules $(@D)/extras $(@D)/CMakeLists.txt $(STAGING_DIR)/usr/share/juce/
endef

$(eval $(generic-package))
