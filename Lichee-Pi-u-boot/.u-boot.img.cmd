cmd_u-boot.img := ./tools/mkimage -A arm -T firmware -C none -O u-boot -a 0x81700000 -e 0 -n "U-Boot 2018.01"" for sunxi board" -d u-boot.bin u-boot.img  >/dev/null
