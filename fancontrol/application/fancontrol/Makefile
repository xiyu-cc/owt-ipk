include $(TOPDIR)/rules.mk

PKG_NAME:=fancontrol
PKG_VERSION:=3.6.2
PKG_RELEASE:=1

PKG_MAINTAINER:=owt-ipk
PKG_LICENSE:=GPL-2.0-or-later
PKG_CPE_ID:=cpe:/a:lm_sensors:lm_sensors

include $(INCLUDE_DIR)/package.mk

define Package/fancontrol
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Temperature-driven fan speed control daemon (C++)
  URL:=https://hwmon.wiki.kernel.org/lm_sensors
  DEPENDS:=+libstdcpp +libubus +libubox
endef

define Package/fancontrol/description
 fancontrol daemon implemented in C++. It monitors hwmon temperatures
 and writes PWM values to keep fans at target speeds.
endef

define Build/Compile
	$(TARGET_CXX) $(TARGET_CXXFLAGS) $(TARGET_CPPFLAGS) -std=gnu++17 -Wall -Wextra -o \
		$(PKG_BUILD_DIR)/fancontrol \
		-I$(PKG_BUILD_DIR) \
		$(PKG_BUILD_DIR)/app/main.cpp \
		$(PKG_BUILD_DIR)/libcore/board_config.cpp \
		$(PKG_BUILD_DIR)/libcore/demand_policy.cpp \
		$(PKG_BUILD_DIR)/libcore/fancontrol_core.cpp \
		$(PKG_BUILD_DIR)/libcore/pwm_controller.cpp \
		$(PKG_BUILD_DIR)/libcore/safety_guard.cpp \
		$(PKG_BUILD_DIR)/libcore/temp_source.cpp \
		$(TARGET_LDFLAGS) -lubus -lubox
endef

define Package/fancontrol/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/fancontrol $(1)/usr/sbin/fancontrol
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/fancontrol.init $(1)/etc/init.d/fancontrol
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/fancontrol.config $(1)/etc/config/fancontrol
	$(INSTALL_DIR) $(1)/etc
	$(INSTALL_DATA) ./files/fancontrol.r3mini $(1)/etc/fancontrol.r3mini
	$(INSTALL_DIR) $(1)/usr/share/doc/fancontrol
	$(INSTALL_DATA) ./files/README.openwrt $(1)/usr/share/doc/fancontrol/README.openwrt
endef

define Package/fancontrol/postinst
#!/bin/sh
[ -n "$${IPKG_INSTROOT}" ] && exit 0

if ! uci -q get fancontrol.main >/dev/null 2>&1; then
	uci -q batch <<'EOF'
set fancontrol.main=fancontrol
set fancontrol.main.enabled='1'
set fancontrol.main.config_file='/etc/fancontrol.r3mini'
set fancontrol.main.debug='0'
set fancontrol.main.respawn_threshold='3600'
set fancontrol.main.respawn_timeout='5'
set fancontrol.main.respawn_retry='5'
commit fancontrol
EOF
fi

enabled="$$(uci -q get fancontrol.main.enabled)"
[ "$$enabled" = "1" ] || exit 0

/etc/init.d/fancontrol enable >/dev/null 2>&1 || true
/etc/init.d/fancontrol start >/dev/null 2>&1 || true
exit 0
endef

$(eval $(call BuildPackage,fancontrol))
