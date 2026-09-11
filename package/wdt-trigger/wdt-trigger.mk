WDT_TRIGGER_VERSION = 1.0
WDT_TRIGGER_SITE = $(BR2_EXTERNAL_RPI4_LAB_PATH)/package/wdt-trigger
WDT_TRIGGER_SITE_METHOD = local

define WDT_TRIGGER_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) \
		-o $(@D)/wdt-trigger $(@D)/wdt-trigger.c
endef

define WDT_TRIGGER_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/wdt-trigger $(TARGET_DIR)/usr/bin/
endef

$(eval $(generic-package))
