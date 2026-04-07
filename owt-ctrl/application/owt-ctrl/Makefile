include $(TOPDIR)/rules.mk

PKG_NAME:=owt-ctrl
PKG_VERSION:=0.1.0
PKG_RELEASE:=1

PKG_MAINTAINER:=owt-ipk
PKG_LICENSE:=MIT

include $(INCLUDE_DIR)/package.mk

define Package/$(PKG_NAME)
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=OWT LAN control daemon
  DEPENDS:=+libstdcpp +libssh2
endef

define Package/$(PKG_NAME)/description
 A resident OpenWrt LAN control service with HTTP API.
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	if [ ! -d ../../frontend/dist ]; then \
		echo "ERROR: missing frontend/dist, run: cd ../../frontend && npm install && npm run build"; \
		false; \
	fi
	$(CP) ../../CMakeLists.txt $(PKG_BUILD_DIR)/
	$(CP) ../../include $(PKG_BUILD_DIR)/
	$(CP) ../../src $(PKG_BUILD_DIR)/
	$(CP) ../../frontend/dist $(PKG_BUILD_DIR)/frontend-dist
	mkdir -p $(PKG_BUILD_DIR)/third_party/spdlog
	$(CP) ../../third_party/boost $(PKG_BUILD_DIR)/third_party/
	$(CP) ../../third_party/spdlog/include $(PKG_BUILD_DIR)/third_party/spdlog/
	$(CP) ../../third_party/nlohmann $(PKG_BUILD_DIR)/third_party/
endef

define Build/Compile
	$(TARGET_CXX) $(TARGET_CXXFLAGS) $(TARGET_CPPFLAGS) -std=gnu++17 -Wall -Wextra -DBOOST_ERROR_CODE_HEADER_ONLY -DOWT_CTRL_ENABLE_LIBSSH2=1 \
		-I$(PKG_BUILD_DIR)/include \
		-I$(PKG_BUILD_DIR)/third_party/boost \
		-I$(PKG_BUILD_DIR)/third_party/spdlog/include \
		-I$(PKG_BUILD_DIR)/third_party \
		-I$(STAGING_DIR)/usr/include \
		-o $(PKG_BUILD_DIR)/owt-ctrl \
				$(PKG_BUILD_DIR)/src/core/main.cpp \
				$(PKG_BUILD_DIR)/src/core/log.cpp \
				$(PKG_BUILD_DIR)/src/core/config.cpp \
				$(PKG_BUILD_DIR)/src/core/utils.cpp \
				$(PKG_BUILD_DIR)/src/core/host_probe_agent.cpp \
				$(PKG_BUILD_DIR)/src/core/params_store.cpp \
				$(PKG_BUILD_DIR)/src/core/wakeonlan_sender.cpp \
				$(PKG_BUILD_DIR)/src/core/ssh_executor.cpp \
				$(PKG_BUILD_DIR)/src/api/wol_wake_api.cpp \
				$(PKG_BUILD_DIR)/src/api/host_control_api.cpp \
				$(PKG_BUILD_DIR)/src/api/host_probe_api.cpp \
				$(PKG_BUILD_DIR)/src/api/monitoring_api.cpp \
				$(PKG_BUILD_DIR)/src/api/params_api.cpp \
				$(TARGET_LDFLAGS) -pthread -lssh2
endef

define Package/$(PKG_NAME)/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/owt-ctrl $(1)/usr/sbin/owt-ctrl
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/owt-ctrl.init $(1)/etc/init.d/owt-ctrl
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/owt-ctrl.config $(1)/etc/config/owt-ctrl
		$(INSTALL_DIR) $(1)/etc/owt-ctrl
		$(INSTALL_CONF) ./files/config.ini $(1)/etc/owt-ctrl/config.ini
		$(INSTALL_CONF) ./files/params.ini $(1)/etc/owt-ctrl/params.ini
		$(INSTALL_DIR) $(1)/www/owt-ctrl
	$(CP) $(PKG_BUILD_DIR)/frontend-dist/. $(1)/www/owt-ctrl/
	$(INSTALL_DIR) $(1)/usr/share/doc/owt-ctrl
	$(INSTALL_DATA) ./files/README.openwrt $(1)/usr/share/doc/owt-ctrl/README.openwrt
endef

define Package/$(PKG_NAME)/postinst
#!/bin/sh
[ -n "$${IPKG_INSTROOT}" ] && exit 0
/etc/init.d/owt-ctrl enable >/dev/null 2>&1 || true
/etc/init.d/owt-ctrl start >/dev/null 2>&1 || true
exit 0
endef

$(eval $(call BuildPackage,$(PKG_NAME)))
