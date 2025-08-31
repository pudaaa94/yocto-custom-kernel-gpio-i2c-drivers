Yocto-Based Embedded Linux Kernel with Custom GPIO/I2C drivers

MOTIVATION

Learning more about Yocto, Embedded Linux and drivers through practical oriented approach

SETUP

Host: Ubuntu virtual machine
Target: Raspberry Pi 3B (BCM2837)

I setup TFTP and NFS server on host machine, so target can fetch literally everything it needs for boot over network. Wireshark tool
was really helpful during the setup. Most critical part of this setup were commands for U-boot (most imporant ones bellow):

bootargs=root=/dev/nfs rw rootfstype=nfs ip=192.168.0.14 console=ttyAMA0,115200 nfsroot=192.168.0.15:/srv/nfs/rpi-rootfs,vers=3
bootcmd=tftpboot 0x00080000 Image; tftpboot 0x02600000 bcm2837-rpi-3-b.dtb; booti 0x00080000 - 0x02600000

Another critical thing (that was preventing me from successful booting) was CONFIG_EFI, which was enabled by default. After successful boot,
I used SSH (dropbear package in local.conf to enable it) to remotely connect to RPI. This way of development gave me a lot of flexibility and I would recommend it to everyone. 

DEVELOPMENT

Main idea was to introduce custom layer in which I will implement all functionalities I want. I focused on implementation of basic GPIO (direction, reading and writing) and basic I2C client driver for BMP280 temperature/pressure sensor. Here is organization and description of each file (folder hierarchy must be matched, so patches can be applied successfully):

yocto
├── meta-custom
    ├── recipes-kernel/linux
        ├── files
            ├── drivers
                ├── i2c
                    custom-bcm2835-i2c-client-driver.c          --- implementation of I2C driver
                ├── pinctrl
                    custom-bcm2835-gpio-driver.c                --- implementation of GPIO driver
            0001-dts-disable-default-enable-custom-gpio.patch   --- modifying GPIO dts node
	        0002-enable-custom-gpio-driver-build.patch          --- Kconfig and Makefile changes for GPIO driver
	        0003-dts-enable-custom-client-i2c.patch             --- adding I2C dts node   
	        0004-enable-custom-i2c-client-driver-build.patch    --- Kconfig and Makefile changes for I2C client driver
	        0005-disable-efi.patch                              --- setting CONFIG_EFI to n
        linux-raspberrypi_5.4.bbappend                          --- list of all patches and source files


As you may notice, I was exploiting patching mechanism as much as possible. This gives a lot of flexibility during work, you always have functional base on which you can return by simply removing problematic patch. 

Important remark: since custom GPIO driver provides minimum functionality, it cannot be used together with custom I2C driver. In other words, I2C driver won't work if custom GPIO driver is used. Custom GPIO driver doesn't provide ALT function and many more things needed for I2C driver to function properly. So, through menuconfig or by simply changing n to y in my patches, select which driver (GPIO or I2C) you want to try. 

I used bitbake core-image-minimal for creating of image.
