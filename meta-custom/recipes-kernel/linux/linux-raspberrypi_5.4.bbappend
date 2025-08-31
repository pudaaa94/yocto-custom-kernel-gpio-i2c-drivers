FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI += " \
	file://0001-dts-disable-default-enable-custom-gpio.patch \
	file://0002-enable-custom-gpio-driver-build.patch \
	file://0003-dts-enable-custom-client-i2c.patch \
	file://0004-enable-custom-i2c-client-driver-build.patch \
	file://0005-disable-efi.patch \
	file://drivers/pinctrl/custom-bcm2835-gpio-driver.c \
	file://drivers/i2c/custom-bcm2835-i2c-client-driver.c \
"

do_configure_append() {
	echo "do_configure called for custom"
	cp -v ${WORKDIR}/drivers/pinctrl/custom-bcm2835-gpio-driver.c ${S}/drivers/pinctrl/bcm/custom-bcm2835-gpio-driver.c
	cp -v ${WORKDIR}/drivers/i2c/custom-bcm2835-i2c-client-driver.c ${S}/drivers/i2c/custom-bcm2835-i2c-client-driver.c
}