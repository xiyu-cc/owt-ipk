include $(TOPDIR)/rules.mk

PKG_NAME:=fancontrol
PKG_VERSION:=3.6.2
PKG_RELEASE:=7

PKG_MAINTAINER:=owt-ipk
PKG_LICENSE:=GPL-2.0-or-later
PKG_CPE_ID:=cpe:/a:lm_sensors:lm_sensors

include $(INCLUDE_DIR)/package.mk

define Package/fancontrol
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Temperature-driven fan speed control daemon (C++)
  URL:=https://hwmon.wiki.kernel.org/lm_sensors
  DEPENDS:=+libstdcpp
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
		$(PKG_BUILD_DIR)/libcore/fancontrol_core.cpp \
		$(PKG_BUILD_DIR)/libcore/temp_source.cpp \
		$(TARGET_LDFLAGS)
endef

define Package/fancontrol/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/fancontrol $(1)/usr/sbin/fancontrol
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/fancontrol.init $(1)/etc/init.d/fancontrol
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/fancontrol.config $(1)/etc/config/fancontrol
	$(INSTALL_DIR) $(1)/usr/share/doc/fancontrol
	$(INSTALL_DATA) ./files/README.openwrt $(1)/usr/share/doc/fancontrol/README.openwrt
	$(INSTALL_DATA) ./files/fancontrol.r3mini.example $(1)/usr/share/doc/fancontrol/fancontrol.r3mini.example
endef

define Package/fancontrol/conffiles
/etc/config/fancontrol
endef

$(eval $(call BuildPackage,fancontrol))
