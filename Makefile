KERNEL_VERSION ?= 6.12
KERNEL_BUILD ?= $(if $(filter 6.12,$(KERNEL_VERSION)),$(CURDIR)/build/kernel,$(CURDIR)/build/kernel-$(KERNEL_VERSION))
export KERNEL_VERSION KERNEL_BUILD

.PHONY: all configure kernel rootfs images dist update-kernel run clean

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

update-kernel:
	./update-kernel.sh

run:
	./run-qemu.sh

clean:
	$(MAKE) -C linux-$(KERNEL_VERSION) O=$(KERNEL_BUILD) clean
