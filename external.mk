.PHONY: sync

sync:
	$(BR2_EXTERNAL_RPI4_LAB_PATH)/sync-target.sh $(TOPDIR)

# GCC 15 compatibility: several older host tools fail with -Werror on warnings GCC 15 newly enables by default
HOST_PAHOLE_CONF_OPTS += -DCMAKE_C_FLAGS="-Wno-error=unused-but-set-variable -Wno-error=discarded-qualifiers"
LINUX_MAKE_FLAGS += HOSTCFLAGS="-Wno-error=discarded-qualifiers"

include $(sort $(wildcard $(BR2_EXTERNAL_RPI4_LAB_PATH)/package/*/*.mk))