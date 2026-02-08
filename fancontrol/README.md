# fancontrol feed layout

This feed is organized by function:

- `application/fancontrol/`: C++ `fancontrol` daemon and C++ `pwmconfig`
- `luci/luci-app-fancontrol/`: LuCI wizard for generating `/etc/fancontrol`

## OpenWrt SDK build

After `./scripts/feeds update fancontrol` and `./scripts/feeds install -f -p fancontrol -a`,
compile by package name:

- `make package/feeds/fancontrol/fancontrol/compile V=s`
- `make package/feeds/fancontrol/pwmconfig/compile V=s`
- `make package/feeds/fancontrol/luci-app-fancontrol/compile V=s`
