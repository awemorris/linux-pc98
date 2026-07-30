KERNEL_VERSION ?= 6.12
KERNEL_BUILD ?= $(if $(filter 6.12,$(KERNEL_VERSION)),$(CURDIR)/build/kernel,$(CURDIR)/build/kernel-$(KERNEL_VERSION))
export KERNEL_VERSION KERNEL_BUILD

.PHONY: all configure kernel rootfs images dist busybox-i486 busybox-i686 update-kernel run clean

all:
	./build-debian.sh

configure:
	./configure-kernel.sh

kernel:
	./build-kernel.sh

rootfs:
	./build-debian-rootfs.sh

images:
	./build-images.sh

dist: images
	./build-dist.sh

busybox-i486:
	KERNEL_VERSION=7.1 CPU_FAMILY=486 \
		KERNEL_BUILD=$(CURDIR)/build/kernel-7.1-i486 \
		LOCALVERSION=-i486 DEVICE_PROFILE=pc98 INSTALL_MODULES=0 \
		./build-kernel.sh
	CPU_FAMILY=486 ./build-i486-rootfs.sh
	CPU_FAMILY=486 ./build-i486-image.sh

busybox-i686:
	KERNEL_VERSION=7.1 CPU_FAMILY=686 \
		KERNEL_BUILD=$(CURDIR)/build/kernel-7.1 \
		DEVICE_PROFILE=pc98 INSTALL_MODULES=0 \
		./build-kernel.sh
	CPU_FAMILY=686 ./build-i486-rootfs.sh
	CPU_FAMILY=686 ./build-i486-image.sh

update-kernel:
	./update-kernel.sh

run:
	./run-qemu.sh

clean:
	$(MAKE) -C linux-$(KERNEL_VERSION) O=$(KERNEL_BUILD) clean
