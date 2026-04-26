include $(TOPDIR)/rules.mk

PKG_NAME:=owt-agent
PKG_VERSION:=0.2.0
PKG_RELEASE:=1

PKG_MAINTAINER:=owt-ipk
PKG_LICENSE:=MIT
PKG_BUILD_DEPENDS:=libssh2 libopenssl zlib libuuid cmake/host ninja/host

include $(INCLUDE_DIR)/package.mk

OWT_AGENT_INCLUDE_DIR:=$(if $(wildcard ../../include/include),../../include/include,../../include)
OWT_AGENT_SRC_DIR:=$(if $(wildcard ../../src/src),../../src/src,../../src)
OWT_AGENT_TP_SOURCE:=$(strip $(OWT_AGENT_TP_SOURCE))

ifeq ($(OWT_AGENT_TP_SOURCE),)
$(error OWT_AGENT_TP_SOURCE is required and must point to the owt third_party directory)
endif

define Package/$(PKG_NAME)
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=OWT gateway execution agent
  DEPENDS:=+libstdcpp +libssh2 +libopenssl +libuuid
endef

define Package/$(PKG_NAME)/description
 OWT gateway-side agent for receiving and executing control commands.
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	rm -rf $(PKG_BUILD_DIR)/include $(PKG_BUILD_DIR)/src $(PKG_BUILD_DIR)/third_party $(PKG_BUILD_DIR)/owt-protocol $(PKG_BUILD_DIR)/cmake
	$(CP) $(OWT_AGENT_INCLUDE_DIR) $(PKG_BUILD_DIR)/include
	$(CP) $(OWT_AGENT_SRC_DIR) $(PKG_BUILD_DIR)/src
	$(CP) ./CMakeLists.txt $(PKG_BUILD_DIR)/
	$(CP) ./cmake $(PKG_BUILD_DIR)/
	mkdir -p $(PKG_BUILD_DIR)/third_party $(PKG_BUILD_DIR)/owt-protocol
	$(CP) $(OWT_AGENT_TP_SOURCE)/spdlog $(PKG_BUILD_DIR)/third_party/
	$(CP) $(OWT_AGENT_TP_SOURCE)/nlohmann $(PKG_BUILD_DIR)/third_party/
	$(CP) $(OWT_AGENT_TP_SOURCE)/jsoncpp $(PKG_BUILD_DIR)/third_party/
	$(CP) $(OWT_AGENT_TP_SOURCE)/drogon $(PKG_BUILD_DIR)/third_party/
	$(CP) ../../../owt-protocol/include $(PKG_BUILD_DIR)/owt-protocol/
endef

define Build/Compile
	rm -rf $(PKG_BUILD_DIR)/build
	(cd $(PKG_BUILD_DIR) && \
		cmake -S . -B build -G Ninja \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
			-DCMAKE_C_COMPILER="$(TARGET_CC)" \
			-DCMAKE_CXX_COMPILER="$(TARGET_CXX)" \
			-DCMAKE_C_FLAGS="$(TARGET_CFLAGS) $(TARGET_CPPFLAGS)" \
			-DCMAKE_CXX_FLAGS="$(TARGET_CXXFLAGS) $(TARGET_CPPFLAGS)" \
			-DCMAKE_EXE_LINKER_FLAGS="$(TARGET_LDFLAGS)" \
				-DCMAKE_SHARED_LINKER_FLAGS="$(TARGET_LDFLAGS)" \
				-DCMAKE_MODULE_LINKER_FLAGS="$(TARGET_LDFLAGS)" \
				-DOWT_AGENT_REQUIRE_LIBSSH2=ON \
				-DOWT_AGENT_ALLOW_SSH_STUB=OFF \
				-DLIBSSH2_INCLUDE_DIR="$(STAGING_DIR)/usr/include" \
				-DLIBSSH2_LIBRARY="$(STAGING_DIR)/usr/lib/libssh2.so" \
				-DOWT_AGENT_TP_ROOT="$(PKG_BUILD_DIR)/third_party" \
				-DUUID_DIR="$(STAGING_DIR)/usr" \
				-DUUID_INCLUDE_DIR="$(STAGING_DIR)/usr/include/uuid" \
			-DUUID_LIBRARY="$(STAGING_DIR)/usr/lib/libuuid.so" \
			-DUUID_INCLUDE_DIRS="$(STAGING_DIR)/usr/include/uuid" \
			-DUUID_LIBRARIES="$(STAGING_DIR)/usr/lib/libuuid.so")
	cmake --build $(PKG_BUILD_DIR)/build --target owt_agent
	cp -f $(PKG_BUILD_DIR)/build/owt-agent $(PKG_BUILD_DIR)/owt-agent
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
endef

define Package/$(PKG_NAME)/postinst
#!/bin/sh
[ -n "$${IPKG_INSTROOT}" ] && exit 0
/etc/init.d/owt-agent enable >/dev/null 2>&1 || true
/etc/init.d/owt-agent start >/dev/null 2>&1 || true
exit 0
endef

$(eval $(call BuildPackage,$(PKG_NAME)))
