MMC_READER_VERSION = 1.0
MMC_READER_SITE = $(BR2_EXTERNAL_RPI4_LAB_PATH)/package/mmc-reader
MMC_READER_SITE_METHOD = local

define MMC_READER_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) \
		-o $(@D)/mmc_reader $(@D)/mmc_reader.c
endef

define MMC_READER_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/mmc_reader $(TARGET_DIR)/usr/bin/mmc_reader
endef

$(eval $(generic-package))
