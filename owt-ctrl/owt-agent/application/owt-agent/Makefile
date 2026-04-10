include $(TOPDIR)/rules.mk

PKG_NAME:=owt-agent
PKG_VERSION:=0.1.0
PKG_RELEASE:=1

PKG_MAINTAINER:=owt-ipk
PKG_LICENSE:=MIT
PKG_BUILD_DEPENDS:=libssh2 libopenssl

include $(INCLUDE_DIR)/package.mk

OWT_AGENT_INCLUDE_DIR:=$(if $(wildcard ../../include/include),../../include/include,../../include)
OWT_AGENT_SRC_DIR:=$(if $(wildcard ../../src/src),../../src/src,../../src)

define Package/$(PKG_NAME)
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=OWT gateway execution agent
  DEPENDS:=+libstdcpp +libssh2 +libopenssl
endef

define Package/$(PKG_NAME)/description
 OWT gateway-side agent for receiving and executing control commands.
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	rm -rf $(PKG_BUILD_DIR)/include $(PKG_BUILD_DIR)/src
	$(CP) $(OWT_AGENT_INCLUDE_DIR) $(PKG_BUILD_DIR)/include
	$(CP) $(OWT_AGENT_SRC_DIR) $(PKG_BUILD_DIR)/src
	mkdir -p $(PKG_BUILD_DIR)/third_party/spdlog
	$(CP) ../../../owt-net/third_party/boost $(PKG_BUILD_DIR)/third_party/
	$(CP) ../../../owt-net/third_party/spdlog/include $(PKG_BUILD_DIR)/third_party/spdlog/
	$(CP) ../../../owt-net/third_party/nlohmann $(PKG_BUILD_DIR)/third_party/
endef

define Build/Compile
	$(TARGET_CXX) $(TARGET_CXXFLAGS) $(TARGET_CPPFLAGS) -std=gnu++17 -Wall -Wextra -DBOOST_ERROR_CODE_HEADER_ONLY \
		-I$(PKG_BUILD_DIR)/include \
		-I$(PKG_BUILD_DIR)/third_party/boost \
		-I$(PKG_BUILD_DIR)/third_party/spdlog/include \
		-I$(PKG_BUILD_DIR)/third_party \
		-I$(STAGING_DIR)/usr/include \
		-o $(PKG_BUILD_DIR)/owt-agent \
				$(PKG_BUILD_DIR)/src/core/main.cpp \
				$(PKG_BUILD_DIR)/src/core/log.cpp \
				$(PKG_BUILD_DIR)/src/core/config.cpp \
				$(PKG_BUILD_DIR)/src/core/host_probe_agent.cpp \
				$(PKG_BUILD_DIR)/src/core/params_store.cpp \
				$(PKG_BUILD_DIR)/src/core/wakeonlan_sender.cpp \
				$(PKG_BUILD_DIR)/src/core/ssh_executor.cpp \
				$(PKG_BUILD_DIR)/src/core/control/agent_runtime.cpp \
				$(PKG_BUILD_DIR)/src/core/control/control_protocol.cpp \
				$(PKG_BUILD_DIR)/src/core/control/control_json_codec.cpp \
				$(PKG_BUILD_DIR)/src/core/control/wss_control_channel.cpp \
				$(TARGET_LDFLAGS) -pthread -lssh2 -lssl -lcrypto
endef

define Package/$(PKG_NAME)/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/owt-agent $(1)/usr/sbin/owt-agent
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/owt-agent.init $(1)/etc/init.d/owt-agent
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/owt-agent.config $(1)/etc/config/owt-agent
	$(INSTALL_DIR) $(1)/etc/owt-agent
	$(INSTALL_CONF) ./files/config.ini $(1)/etc/owt-agent/config.ini
	$(INSTALL_DIR) $(1)/usr/share/doc/owt-agent
	$(INSTALL_DATA) ./files/README.openwrt $(1)/usr/share/doc/owt-agent/README.openwrt
endef

define Package/$(PKG_NAME)/postinst
#!/bin/sh
[ -n "$${IPKG_INSTROOT}" ] && exit 0
/etc/init.d/owt-agent enable >/dev/null 2>&1 || true
/etc/init.d/owt-agent start >/dev/null 2>&1 || true
exit 0
endef

$(eval $(call BuildPackage,$(PKG_NAME)))
