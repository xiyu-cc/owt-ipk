include $(TOPDIR)/rules.mk

PKG_NAME:=fancontrol
PKG_VERSION:=3.6.2
PKG_RELEASE:=5

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

define Package/pwmconfig
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=Interactive helper to generate /etc/fancontrol (C++)
  URL:=https://hwmon.wiki.kernel.org/lm_sensors
  DEPENDS:=+fancontrol +libstdcpp
endef

define Package/pwmconfig/description
 pwmconfig implemented in C++. It scans PWM/fan sensors and helps create
 a board-specific /etc/fancontrol configuration file.
endef

define Build/Compile
	$(TARGET_CXX) $(TARGET_CXXFLAGS) $(TARGET_CPPFLAGS) -std=gnu++17 -Wall -Wextra -o \
		$(PKG_BUILD_DIR)/fancontrol $(PKG_BUILD_DIR)/fancontrol.cpp $(TARGET_LDFLAGS)
	$(TARGET_CXX) $(TARGET_CXXFLAGS) $(TARGET_CPPFLAGS) -std=gnu++17 -Wall -Wextra -o \
		$(PKG_BUILD_DIR)/pwmconfig $(PKG_BUILD_DIR)/pwmconfig.cpp $(TARGET_LDFLAGS)
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
endef

define Package/fancontrol/conffiles
/etc/config/fancontrol
endef

define Package/pwmconfig/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/pwmconfig $(1)/usr/sbin/pwmconfig
endef

$(eval $(call BuildPackage,fancontrol))
$(eval $(call BuildPackage,pwmconfig))
