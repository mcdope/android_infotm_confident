
ANDROID_KERNEL_PATH := bootable/linux-3.0.8
ANDROID_INSTALL_PATH := device/infotm/imapx800/modules

obj-y				+= ubi/
obj-y				+= ubifs/
obj-y				+= uftl/

default:
	$(MAKE) -C $(ANDROID_BUILD_TOP)/$(ANDROID_KERNEL_PATH) M=$(shell pwd) modules

install:
	cp -f ubi/ubi.ko   $(ANDROID_BUILD_TOP)/$(ANDROID_INSTALL_PATH)/ubi.ko
	cp -f ubifs/ubifs.ko $(ANDROID_BUILD_TOP)/$(ANDROID_INSTALL_PATH)/ubifs.ko
	cp -f uftl/uftl.ko $(ANDROID_BUILD_TOP)/$(ANDROID_INSTALL_PATH)/uftl.ko

RMMODS := *.o *.ko *.~ *.mod.c *.order .*.cmd *.symvers .tmp_versions

clean:
	rm -rf $(RMMODS)
	make clean -C ubi
	make clean -C ubifs
	make clean -C uftl
