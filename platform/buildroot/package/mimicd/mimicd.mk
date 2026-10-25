MIMICD_VERSION = 0.1.0
MIMICD_SITE = $(BR2_EXTERNAL_OPEN_MIMIC_PATH)/../../software/mimicd
MIMICD_SITE_METHOD = local
MIMICD_LICENSE = GPL-2.0-only

$(eval $(cargo-package))
