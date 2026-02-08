# Copyright (C) 2026
# SPDX-License-Identifier: GPL-2.0-or-later

include $(TOPDIR)/rules.mk

PKG_VERSION:=1.0.0
PKG_RELEASE:=1

LUCI_TITLE:=LuCI Fancontrol Wizard
LUCI_DEPENDS:=+luci-base +fancontrol +rpcd
LUCI_PKGARCH:=all

PKG_LICENSE:=GPL-2.0-or-later
PKG_MAINTAINER:=owt-ipk

include $(TOPDIR)/feeds/luci/luci.mk

# call BuildPackage - OpenWrt buildroot signature
