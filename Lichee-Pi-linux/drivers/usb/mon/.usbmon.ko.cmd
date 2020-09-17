cmd_drivers/usb/mon/usbmon.ko := arm-linux-gnueabi-ld -r  -EL -T ./scripts/module-common.lds  --build-id  -o drivers/usb/mon/usbmon.ko drivers/usb/mon/usbmon.o drivers/usb/mon/usbmon.mod.o ;  true
