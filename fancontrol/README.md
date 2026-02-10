# fancontrol feed layout

This feed is organized by function:

- `application/fancontrol/`: C++ `fancontrol` daemon (board-mode only)
- `luci/luci-app-fancontrol/`: LuCI board editor for generating `/etc/fancontrol.conf`

## OpenWrt SDK build

After `./scripts/feeds update fancontrol` and `./scripts/feeds install -f -p fancontrol -a`,
compile by package name:

- `make package/feeds/fancontrol/fancontrol/compile V=s`
- `make package/feeds/fancontrol/luci-app-fancontrol/compile V=s`
