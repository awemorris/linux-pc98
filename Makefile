.PHONY: all configure kernel rootfs images update-kernel run clean

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

update-kernel:
	./update-kernel.sh

run:
	./run-qemu.sh

clean:
	$(MAKE) -C linux-6.12 O=$(CURDIR)/build/kernel clean
