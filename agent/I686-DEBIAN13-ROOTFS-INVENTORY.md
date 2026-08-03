# Debian-13-i686-PC-98-Release-0.3.0 inventory

This inventory was generated from the installed dpkg database and the
actual staging tree. It describes the Release root filesystem, not the
set of packages currently available from a Debian mirror.

## Build scope

- rootfs: `/home/awe/linux-pc98/build/release-v0.3.0/debian13-rootfs`
- debootstrap suite: `trixie`
- debootstrap variant: `minbase`
- architecture: `i386` userland built for the repository's i686 image
- installed packages: **112**
- package architectures: `all` 14, `i386` 98
- sum of dpkg Installed-Size fields: **164946 KiB**
- explicit `--include` packages present: `ca-certificates`, `dhcpcd-base`, `e2fsprogs`, `ifupdown`, `iproute2`, `kmod`, `sysvinit-core`, `udev`

The build subsequently deletes apt lists, manuals, info pages, and
locales. Therefore `Installed-Size` is package metadata and is larger
than the files retained in the slimmed staging tree.

## Installed packages

| Package | Version | Arch | Priority | Essential | KiB | Exec/ELF | Selection |
|---|---|---|---|---|---|---|---|
| adduser | 3.152 | all | important | no | 428 | 3 | minbase/dependency |
| apt | 3.0.3 | i386 | required | no | 4409 | 25 | minbase/dependency |
| base-files | 13.8+deb13u6 | i386 | required | yes | 348 | 1 | minbase/dependency |
| base-passwd | 3.6.7 | i386 | required | yes | 267 | 1 | minbase/dependency |
| bash | 5.2.37-2+b9 | i386 | required | yes | 7327 | 3 | minbase/dependency |
| bsdutils | 1:2.41-5 | i386 | required | yes | 411 | 6 | minbase/dependency |
| ca-certificates | 20250419 | all | standard | no | 390 | 2 | explicit --include |
| coreutils | 9.7-3 | i386 | required | yes | 18739 | 106 | minbase/dependency |
| dash | 0.5.12-12 | i386 | required | yes | 214 | 1 | minbase/dependency |
| debconf | 1.5.91 | all | required | no | 497 | 11 | minbase/dependency |
| debian-archive-keyring | 2025.1 | all | important | no | 302 | 0 | minbase/dependency |
| debianutils | 5.23.2 | i386 | required | yes | 218 | 9 | minbase/dependency |
| dhcpcd-base | 1:10.1.0-11+deb13u3 | i386 | important | no | 538 | 3 | explicit --include |
| diffutils | 1:3.10-4 | i386 | required | yes | 1786 | 4 | minbase/dependency |
| dpkg | 1.22.22 | i386 | required | yes | 6708 | 14 | minbase/dependency |
| e2fsprogs | 1.47.2-3+b11 | i386 | important | no | 1625 | 20 | explicit --include |
| findutils | 4.10.0-3 | i386 | required | yes | 2194 | 2 | minbase/dependency |
| gcc-14-base | 14.2.0-19 | i386 | optional | no | 112 | 0 | minbase/dependency |
| grep | 3.11-4 | i386 | required | yes | 1281 | 4 | minbase/dependency |
| gzip | 1.13-1 | i386 | required | yes | 259 | 13 | minbase/dependency |
| hostname | 3.25 | i386 | required | yes | 45 | 1 | minbase/dependency |
| ifupdown | 0.8.44+deb13u1 | i386 | important | no | 197 | 9 | explicit --include |
| init-system-helpers | 1.69~deb13u1 | all | required | yes | 133 | 5 | minbase/dependency |
| initscripts | 3.14-4 | all | optional | no | 203 | 33 | minbase/dependency |
| insserv | 1.26.0-1 | i386 | optional | no | 143 | 5 | minbase/dependency |
| iproute2 | 6.15.0-1 | i386 | important | no | 4076 | 16 | explicit --include |
| kmod | 34.2-2 | i386 | important | no | 274 | 2 | explicit --include |
| libacl1 | 2.3.2-2+b1 | i386 | optional | no | 78 | 1 | minbase/dependency |
| libapparmor1 | 4.1.0-1 | i386 | optional | no | 114 | 1 | minbase/dependency |
| libapt-pkg7.0 | 3.0.3 | i386 | optional | no | 3917 | 1 | minbase/dependency |
| libattr1 | 1:2.5.2-3 | i386 | optional | no | 56 | 1 | minbase/dependency |
| libaudit-common | 1:4.0.2-2 | all | optional | no | 25 | 0 | minbase/dependency |
| libaudit1 | 1:4.0.2-2+b2 | i386 | optional | no | 171 | 1 | minbase/dependency |
| libblkid1 | 2.41-5 | i386 | optional | no | 488 | 1 | minbase/dependency |
| libbpf1 | 1:1.5.0-3 | i386 | optional | no | 499 | 1 | minbase/dependency |
| libbsd0 | 0.12.2-2 | i386 | optional | no | 213 | 1 | minbase/dependency |
| libbz2-1.0 | 1.0.8-6 | i386 | optional | no | 100 | 1 | minbase/dependency |
| libc-bin | 2.41-12+deb13u3 | i386 | required | yes | 2148 | 12 | minbase/dependency |
| libc6 | 2.41-12+deb13u3 | i386 | optional | no | 12437 | 272 | minbase/dependency |
| libcap-ng0 | 0.8.5-4+b1 | i386 | optional | no | 62 | 2 | minbase/dependency |
| libcap2 | 1:2.75-10+deb13u1+b1 | i386 | optional | no | 90 | 2 | minbase/dependency |
| libcap2-bin | 1:2.75-10+deb13u1+b1 | i386 | important | no | 122 | 4 | minbase/dependency |
| libcom-err2 | 1.47.2-3+b11 | i386 | optional | no | 60 | 1 | minbase/dependency |
| libcrypt1 | 1:4.4.38-1 | i386 | optional | no | 254 | 1 | minbase/dependency |
| libdb5.3t64 | 5.3.28+dfsg2-9 | i386 | optional | no | 2080 | 1 | minbase/dependency |
| libdebconfclient0 | 0.280 | i386 | optional | no | 37 | 1 | minbase/dependency |
| libelf1t64 | 0.192-4 | i386 | optional | no | 1243 | 1 | minbase/dependency |
| libext2fs2t64 | 1.47.2-3+b11 | i386 | optional | no | 618 | 2 | minbase/dependency |
| libgcc-s1 | 14.2.0-19 | i386 | optional | no | 235 | 1 | minbase/dependency |
| libgmp10 | 2:6.3.0+dfsg-3 | i386 | optional | no | 912 | 1 | minbase/dependency |
| libgssapi-krb5-2 | 1.21.3-5+deb13u1 | i386 | optional | no | 467 | 1 | minbase/dependency |
| libhogweed6t64 | 3.10.1-1 | i386 | optional | no | 478 | 1 | minbase/dependency |
| libk5crypto3 | 1.21.3-5+deb13u1 | i386 | optional | no | 273 | 1 | minbase/dependency |
| libkeyutils1 | 1.6.3-6 | i386 | optional | no | 45 | 1 | minbase/dependency |
| libkmod2 | 34.2-2 | i386 | optional | no | 155 | 1 | minbase/dependency |
| libkrb5-3 | 1.21.3-5+deb13u1 | i386 | optional | no | 1096 | 2 | minbase/dependency |
| libkrb5support0 | 1.21.3-5+deb13u1 | i386 | optional | no | 136 | 1 | minbase/dependency |
| liblastlog2-2 | 2.41-5 | i386 | optional | no | 79 | 1 | minbase/dependency |
| liblz4-1 | 1.10.0-4 | i386 | optional | no | 172 | 1 | minbase/dependency |
| liblzma5 | 5.8.1-1+deb13u1 | i386 | optional | no | 458 | 1 | minbase/dependency |
| libmd0 | 1.1.0-2+b1 | i386 | optional | no | 101 | 1 | minbase/dependency |
| libmnl0 | 1.0.5-3 | i386 | optional | no | 45 | 1 | minbase/dependency |
| libmount1 | 2.41-5 | i386 | optional | no | 636 | 1 | minbase/dependency |
| libnettle8t64 | 3.10.1-1 | i386 | optional | no | 557 | 1 | minbase/dependency |
| libpam-modules | 1.7.0-5 | i386 | required | no | 952 | 45 | minbase/dependency |
| libpam-modules-bin | 1.7.0-5 | i386 | required | no | 192 | 7 | minbase/dependency |
| libpam-runtime | 1.7.0-5 | all | required | no | 1028 | 2 | minbase/dependency |
| libpam0g | 1.7.0-5 | i386 | optional | no | 196 | 3 | minbase/dependency |
| libpcre2-8-0 | 10.46-1~deb13u1 | i386 | optional | no | 789 | 1 | minbase/dependency |
| libseccomp2 | 2.6.0-2 | i386 | optional | no | 208 | 1 | minbase/dependency |
| libselinux1 | 3.8.1-1 | i386 | optional | no | 244 | 1 | minbase/dependency |
| libsemanage-common | 3.8.1-1 | all | optional | no | 21 | 0 | minbase/dependency |
| libsemanage2 | 3.8.1-1 | i386 | optional | no | 341 | 1 | minbase/dependency |
| libsepol2 | 3.8.1-1 | i386 | optional | no | 942 | 1 | minbase/dependency |
| libsmartcols1 | 2.41-5 | i386 | optional | no | 404 | 1 | minbase/dependency |
| libsqlite3-0 | 3.46.1-7+deb13u1 | i386 | optional | no | 2034 | 1 | minbase/dependency |
| libss2 | 1.47.2-3+b11 | i386 | optional | no | 76 | 1 | minbase/dependency |
| libssl3t64 | 3.5.6-1~deb13u2 | i386 | optional | no | 7106 | 5 | minbase/dependency |
| libstdc++6 | 14.2.0-19 | i386 | optional | no | 3027 | 1 | minbase/dependency |
| libsystemd-shared | 257.13-1~deb13u1 | i386 | optional | no | 7101 | 2 | minbase/dependency |
| libsystemd0 | 257.13-1~deb13u1 | i386 | optional | no | 1273 | 1 | minbase/dependency |
| libtinfo6 | 6.5+20250216-2 | i386 | optional | no | 543 | 2 | minbase/dependency |
| libtirpc-common | 1.3.6+ds-1 | all | optional | no | 32 | 0 | minbase/dependency |
| libtirpc3t64 | 1.3.6+ds-1 | i386 | optional | no | 259 | 1 | minbase/dependency |
| libudev1 | 257.13-1~deb13u1 | i386 | optional | no | 332 | 1 | minbase/dependency |
| libuuid1 | 2.41-5 | i386 | optional | no | 97 | 1 | minbase/dependency |
| libxtables12 | 1.8.11-2 | i386 | optional | no | 101 | 1 | minbase/dependency |
| libxxhash0 | 0.8.3-2 | i386 | optional | no | 132 | 1 | minbase/dependency |
| libzstd1 | 1.5.7+dfsg-1 | i386 | optional | no | 883 | 1 | minbase/dependency |
| login | 1:4.16.0-2+really2.41-5 | i386 | required | no | 273 | 3 | minbase/dependency |
| login.defs | 1:4.17.4-2 | all | required | no | 214 | 0 | minbase/dependency |
| logsave | 1.47.2-3+b11 | i386 | optional | no | 58 | 1 | minbase/dependency |
| mawk | 1.3.4.20250131-1 | i386 | required | no | 307 | 11 | minbase/dependency |
| mount | 2.41-5 | i386 | required | no | 503 | 5 | minbase/dependency |
| ncurses-base | 6.5+20250216-2 | all | required | yes | 389 | 0 | minbase/dependency |
| ncurses-bin | 6.5+20250216-2 | i386 | required | yes | 650 | 7 | minbase/dependency |
| openssl | 3.5.6-1~deb13u2 | i386 | optional | no | 2484 | 4 | minbase/dependency |
| openssl-provider-legacy | 3.5.6-1~deb13u2 | i386 | optional | no | 392 | 1 | minbase/dependency |
| passwd | 1:4.17.4-2 | i386 | required | no | 4790 | 24 | minbase/dependency |
| perl-base | 5.40.1-6 | i386 | required | yes | 8080 | 12 | minbase/dependency |
| sed | 4.9-2+deb13u1 | i386 | required | yes | 994 | 1 | minbase/dependency |
| sqv | 1.3.0-3+b2 | i386 | optional | no | 3517 | 1 | minbase/dependency |
| startpar | 0.66-1 | i386 | optional | no | 63 | 1 | minbase/dependency |
| systemd | 257.13-1~deb13u1 | i386 | important | no | 9662 | 98 | minbase/dependency |
| sysv-rc | 3.14-4 | all | optional | no | 91 | 2 | minbase/dependency |
| sysvinit-core | 3.14-4 | i386 | optional | no | 361 | 4 | explicit --include |
| sysvinit-utils | 3.14-4 | i386 | required | yes | 102 | 3 | minbase/dependency |
| tar | 1.35+dfsg-3.1 | i386 | required | yes | 3137 | 3 | minbase/dependency |
| tzdata | 2026b-0+deb13u1 | all | required | no | 1361 | 0 | minbase/dependency |
| udev | 257.13-1~deb13u1 | i386 | important | no | 10183 | 14 | explicit --include |
| util-linux | 2.41-5 | i386 | required | yes | 5081 | 63 | minbase/dependency |
| zlib1g | 1:1.3.dfsg+really1.3.1-1+b1 | i386 | optional | no | 160 | 1 | minbase/dependency |

The machine-readable equivalent is
`inventory/i686-debian13-packages.tsv`.

## ELF payloads not owned by a Debian package

Found **21** unowned ELF files. In this image these
are Linux 7.1 modules installed by `make modules_install`; they are
project build products rather than files from a `.deb` package.

| Path | Bytes | Type | SHA-256 |
|---|---|---|---|
| /usr/lib/modules/7.1.0/kernel/drivers/hid/hid-generic.ko | 5957 | ELF 32-bit LSB relocatable, Intel i386, version 1 (SYSV), BuildID[sha1]=cf9f5093d847734fc36967310a5db9bd1c5e256d, not stripped | bd7aa9b7048353d46c816d8d7d6155ad1746d5aa2c9630d75e647ede1c0a2054 |
| /usr/lib/modules/7.1.0/kernel/drivers/hid/hid.ko | 247533 | ELF 32-bit LSB relocatable, Intel i386, version 1 (SYSV), BuildID[sha1]=420025f0c0957110dca7090e09bc3aef4d6a9856, not stripped | 28fd7dfdccc8411d35b583e01cacc589c6032abc753225cd810cfad5a401f5a5 |
| /usr/lib/modules/7.1.0/kernel/drivers/hid/usbhid/usbhid.ko | 41073 | ELF 32-bit LSB relocatable, Intel i386, version 1 (SYSV), BuildID[sha1]=cea73429fced3e80c16f6e527f8328426a2eb5f1, not stripped | 66990899844145f7b65bacceee85511426d982ba69fe88473c94209c338ab1c0 |
| /usr/lib/modules/7.1.0/kernel/drivers/net/usb/cdc_ether.ko | 23329 | ELF 32-bit LSB relocatable, Intel i386, version 1 (SYSV), BuildID[sha1]=eb6fe172a948421f20b0a92f13b7f373aa7ea6a4, not stripped | a3ee558585764c13a9b9f0627e8718056b7b4f93dcf1e0f3d3c5e6153027d589 |
| /usr/lib/modules/7.1.0/kernel/drivers/net/usb/cdc_ncm.ko | 48949 | ELF 32-bit LSB relocatable, Intel i386, version 1 (SYSV), BuildID[sha1]=bfa9d6bcf9e738c4a256710bae4513e34d710cc4, not stripped | eda08b45fed720f110be4917465a13445df8c296ff33e77649e0d628b22e0714 |
| /usr/lib/modules/7.1.0/kernel/drivers/net/usb/r8153_ecm.ko | 7569 | ELF 32-bit LSB relocatable, Intel i386, version 1 (SYSV), BuildID[sha1]=e1841fe1bac49d04573d838dd1639b83b70de99b, not stripped | 74bd13c9839f651eda7556dd92da69bb2d3dc081ffbf08c2590c2f236d50605a |
| /usr/lib/modules/7.1.0/kernel/drivers/net/usb/usbnet.ko | 63449 | ELF 32-bit LSB relocatable, Intel i386, version 1 (SYSV), BuildID[sha1]=effc644bbdd20ea0ff6fc438686b49d2ddf32e37, not stripped | 28c5dcb90884ca10f3ef87e291b8e30f3cc659876c0969b476dafab959e14590 |
| /usr/lib/modules/7.1.0/kernel/drivers/usb/class/cdc-acm.ko | 55361 | ELF 32-bit LSB relocatable, Intel i386, version 1 (SYSV), BuildID[sha1]=e6a9821eacaeae47c17d97c1584d12bbdc575178, not stripped | d5194f70690e6752a2351af8454094b2238dab8d72c10b7b768702a1b52bc86a |
| /usr/lib/modules/7.1.0/kernel/drivers/usb/class/cdc-wdm.ko | 28577 | ELF 32-bit LSB relocatable, Intel i386, version 1 (SYSV), BuildID[sha1]=50a7ae3697a378d94d484906dc2055f0dc8a2945, not stripped | 7f1d69e35f9a150b86d672399a782d2f6dfb3d9b606249512423c4373eb24aba |
| /usr/lib/modules/7.1.0/kernel/drivers/usb/class/usblp.ko | 27849 | ELF 32-bit LSB relocatable, Intel i386, version 1 (SYSV), BuildID[sha1]=a442ec98b125fd162d37deced38c81847ff98e48, not stripped | d7456bc3e0cca09479d71bcde7a2fe2ae308e2bcb411e7e362e638c6844c4039 |
| /usr/lib/modules/7.1.0/kernel/drivers/usb/common/usb-common.ko | 18865 | ELF 32-bit LSB relocatable, Intel i386, version 1 (SYSV), BuildID[sha1]=7d65de85dbbf290afeb3e9bbeb1905f62d787098, not stripped | bfd883a4a1d481f0d7d2df5982c764130e5f3d7ca930470511b23d6b817d38bd |
| /usr/lib/modules/7.1.0/kernel/drivers/usb/core/usbcore.ko | 370125 | ELF 32-bit LSB relocatable, Intel i386, version 1 (SYSV), BuildID[sha1]=f0f7e3fdf5cadbf928ce921157d4ee95fd422c2b, not stripped | faa9c01ae500991e459d746d0c983e8abe6e9cce9a1f06933172df4aec7e5bac |
| /usr/lib/modules/7.1.0/kernel/drivers/usb/host/ehci-hcd.ko | 96341 | ELF 32-bit LSB relocatable, Intel i386, version 1 (SYSV), BuildID[sha1]=1eaaa0dde6194aa7858f73bba40a7fce2e921cba, not stripped | f27fc28ba8246a113416baafcd3a7c4967fe61c7d6f8f5ee2f0c77a06769925d |
| /usr/lib/modules/7.1.0/kernel/drivers/usb/host/ehci-pci.ko | 12225 | ELF 32-bit LSB relocatable, Intel i386, version 1 (SYSV), BuildID[sha1]=424c182b29f27fcfa58e7e4908af449a981abe59, not stripped | 039377a4450398ac0ddbd8cdf55790d66f7b96cd599826a01cf9d7d32c0e35f6 |
| /usr/lib/modules/7.1.0/kernel/drivers/usb/host/ohci-hcd.ko | 61761 | ELF 32-bit LSB relocatable, Intel i386, version 1 (SYSV), BuildID[sha1]=f7f22a4963e47c2fdd31e6b2062f128f02065e72, not stripped | 27005bd51e09ae02325816900fd36cf1e4b9c03acff04f1574ea64cc67f9add1 |
| /usr/lib/modules/7.1.0/kernel/drivers/usb/host/ohci-pci.ko | 12365 | ELF 32-bit LSB relocatable, Intel i386, version 1 (SYSV), BuildID[sha1]=7c7fe8743326fd5b054b4cb70c81b516a51f2333, not stripped | 9383a82aa024f8bd5c616b72652417f8dd57f06947ec1cccb559ba8a47835350 |
| /usr/lib/modules/7.1.0/kernel/drivers/usb/host/uhci-hcd.ko | 52713 | ELF 32-bit LSB relocatable, Intel i386, version 1 (SYSV), BuildID[sha1]=f58fdad74b8e989b786364107fd0a88b6eba3142, not stripped | 5311de065eba360e36c19e5ad99cc9e3ccdfd57fa7bf649b710863afa989c07a |
| /usr/lib/modules/7.1.0/kernel/drivers/usb/storage/usb-storage.ko | 115665 | ELF 32-bit LSB relocatable, Intel i386, version 1 (SYSV), BuildID[sha1]=69ac1b7ea98817a6025cf4b8e930672287f88c35, not stripped | bfdcf808dd8d63f6f34aedd6c1d244e444c5b5845d7187858103efe52a88edb8 |
| /usr/lib/modules/7.1.0/kernel/drivers/video/fbdev/pc98cirrusfb.ko | 9081 | ELF 32-bit LSB relocatable, Intel i386, version 1 (SYSV), BuildID[sha1]=805c83ef68f41911fcc8fa4238d185564036c9e7, not stripped | ce17e214ffb6628072d83f6846b3dd2c866dcc10bf9e4a655a06dc5f97ba9d9b |
| /usr/lib/modules/7.1.0/kernel/drivers/video/fbdev/pc98tridentfb.ko | 26073 | ELF 32-bit LSB relocatable, Intel i386, version 1 (SYSV), BuildID[sha1]=0e9d6abf726303456daebd54091e0ecfb5755ce6, not stripped | c8abfa09e651b7e3ee93d47d1b357d2d82de0fae8c024ff4300d1dd26aa458d1 |
| /usr/lib/modules/7.1.0/kernel/kernel/configs.ko | 33801 | ELF 32-bit LSB relocatable, Intel i386, version 1 (SYSV), BuildID[sha1]=f3a70046e24767ee33bf3e53a205554156d73e37, not stripped | 5901c1b2b01ba1cc900be84b595dff57be5c5ac2f7c7bec1bc4657ad753bd0d6 |

The machine-readable equivalent is
`inventory/i686-debian13-unowned-binaries.tsv`.

## Other unowned executable scripts

No unowned executable scripts were found in the rootfs.

## Generated binary metadata not owned by a package

These files are caches or indexes generated while constructing the
rootfs. They are not executable payloads.

| Path | Bytes | Type | SHA-256 |
|---|---|---|---|
| /etc/ld.so.cache | 4887 | data | b2cb012240e35a301e678094b5770633fc5af6f5cff7b3e5f1c87ae11b32489b |
| /usr/lib/modules/7.1.0/modules.alias.bin | 44583 | data | b1701076fea179dd47dd63074449df65f24519c7f8258ef3eaeff862634646a2 |
| /usr/lib/modules/7.1.0/modules.builtin.alias.bin | 56103 | data | 2c8b99672f067419ebea2a5b3135bdf3ddbe731f507807fca188624143283b53 |
| /usr/lib/modules/7.1.0/modules.builtin.bin | 6747 | data | 7c11c85610632f7af2750742e3f4a87e3aee966eb929ba02b68d56ec2e9b1cec |
| /usr/lib/modules/7.1.0/modules.dep.bin | 3020 | data | 66fb4c818c21116d2bc24ca2760d5b2b5a877f8738e87b5d8d6e70c3390c7ef2 |
| /usr/lib/modules/7.1.0/modules.symbols.bin | 16596 | data | a8925c78b3837ece755d2182e0692688bd734490f3cbeb977c7682baeef02c74 |
| /var/cache/ldconfig/aux-cache | 4736 | data | 911e481aab46b2666e6d7ee28091e6a34be600cac1c2d83c7acb3e706dce3c24 |

## Non-package boot-image inputs

These files are outside the debootstrap rootfs but are embedded in the
released CF-card image by the PC-98 image builder.

| Role | Build path | Bytes | Type | SHA-256 |
|---|---|---|---|---|
| disk-IPL | /home/awe/linux-pc98/loader/disk-ipl.bin | 512 | DOS/MBR boot sector | 3ad6ba32db79f192e75e90baebb861616206c4faf0e528fc827aeb2a8bf6c8ea |
| partition-PBR | /home/awe/linux-pc98/loader/partition-pbr.bin | 512 | DOS/MBR boot sector | f4f2920524b3c37a8fe3d2fc412412dad1ad8f490d516e8863e0547e09f4efef |
| FAT-loader | /home/awe/linux-pc98/loader/fat-loader.bin | 2557 | data | f327e5939e70319b0fc34b066eaca26998f8022f7abaf099be4640456d66de19 |
| Linux-7.1-i686-vmlinux | /home/awe/linux-pc98/build/release-v0.3.0/kernel-7.1-i686-debian/vmlinux.boot | 18602728 | ELF 32-bit LSB executable, Intel i386, version 1 (SYSV), statically linked, BuildID[sha1]=59a33e0698b97546ceaa91a5c2cd5f8424a2fb92, stripped | 11ac7f5ffdfa8cead9fa1d9a38d0bd62f99802e9d7293cc38bca5740ed861f0f |

## Interpretation

- Normal commands and shared libraries are owned by the packages in
  the first table.
- The unowned ELF files are the locally built Linux modules; no extra
  standalone userspace ELF executable was found.
- The uncompressed kernel and three loader binaries are deliberately
  outside dpkg because they live in the PC-98 boot path/boot partition.
- Locally written hostname, fstab, network, password, and optional
  console settings are configuration text, not additional binaries.
