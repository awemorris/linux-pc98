#!/bin/sh
set -eu

repo=/home/awe/linux-pc98
source="$repo/linux-7.1"
build="$repo/build/i386-minimal/kernel"
config="$build/.config"
sc="$source/scripts/config"

mkdir -p "$build"
make -C "$source" O="$build" ARCH=i386 tinyconfig

# CPU and machine baseline.
"$sc" --file "$config" \
	--enable EXPERT \
	--enable EMBEDDED \
	--enable CC_OPTIMIZE_FOR_SIZE \
	--enable X86_EXTENDED_PLATFORM \
	--enable X86_PC9800 \
	--disable SMP \
	--disable HIGHMEM \
	--disable PAE \
	--disable PCI \
	--disable ACPI \
	--disable MODULES \
	--disable KEXEC \
	--disable HIBERNATION \
	--disable SUSPEND \
	--disable CPU_MITIGATIONS \
	--disable RANDOMIZE_BASE \
	--disable RELOCATABLE \
	--enable MATH_EMULATION \
	--enable KERNEL_GZIP \
	--disable KERNEL_XZ \
	--enable SLUB_TINY \
	--enable BASE_SMALL \
	--set-val LOG_BUF_SHIFT 12 \
	--enable HZ_100 \
	--disable HZ_250

# A 16 MiB link address is appropriate for modern PCs but makes a small-RAM
# PC-9801 impossible to boot.  The second-stage loader and Linux boot protocol
# both support the traditional 1 MiB protected-mode load address.
"$sc" --file "$config" \
	--set-val PHYSICAL_START 0x100000 \
	--set-val PHYSICAL_ALIGN 0x100000

# Select the 386 member of the processor-family choice explicitly.  Merely
# enabling M386 is not sufficient after tinyconfig has selected M686.
"$sc" --file "$config" \
	--disable M486SX \
	--disable M486 \
	--disable M586 \
	--disable M586TSC \
	--disable M586MMX \
	--disable M686 \
	--disable MPENTIUMII \
	--disable MPENTIUMIII \
	--disable MPENTIUMM \
	--disable MPENTIUM4 \
	--disable MK6 \
	--disable MK7 \
	--disable MCRUSOE \
	--disable MEFFICEON \
	--disable MWINCHIPC6 \
	--disable MWINCHIP3D \
	--disable MELAN \
	--disable MGEODEGX1 \
	--disable MGEODE_LX \
	--disable MCYRIXIII \
	--disable MVIAC3_2 \
	--disable MVIAC7 \
	--disable MATOM \
	--disable X86_NATIVE_CPU \
	--enable M386

# Minimal process ABI and filesystems needed by the static acceptance init.
"$sc" --file "$config" \
	--enable MULTIUSER \
	--enable BINFMT_ELF \
	--disable COREDUMP \
	--disable ELF_CORE \
	--disable SYSVIPC \
	--disable POSIX_MQUEUE \
	--disable AIO \
	--disable IO_URING \
	--disable NAMESPACES \
	--disable CGROUPS \
	--disable SECURITY \
	--disable AUDIT \
	--disable KALLSYMS \
	--disable DEBUG_KERNEL \
	--enable PRINTK \
	--enable EARLY_PRINTK \
	--enable BLOCK \
	--enable BLK_DEV \
	--enable DEVTMPFS \
	--enable DEVTMPFS_MOUNT \
	--enable PROC_FS \
	--enable SYSFS \
	--enable TMPFS \
	--enable EXT4_FS \
	--disable EXT4_FS_POSIX_ACL \
	--disable EXT4_FS_SECURITY \
	--enable PARTITION_ADVANCED \
	--enable NEC98_PARTITION

# PC-98 GDC console, keyboard and the built-in serial port used for logs.
"$sc" --file "$config" \
	--enable TTY \
	--enable VT \
	--enable VT_CONSOLE \
	--disable VGA_CONSOLE \
	--disable FB \
	--enable PC98_CONSOLE \
	--enable INPUT \
	--enable INPUT_KEYBOARD \
	--enable KEYBOARD_PC98 \
	--enable SERIAL_PC98_8251 \
	--enable SERIAL_PC98_8251_CONSOLE \
	--disable SERIAL_8250 \
	--disable SOUND

# PC-98 IDE.  The low-memory driver exposes the master CF/HDD directly as
# /dev/hd98a and deliberately avoids the libata and SCSI-disk frameworks.
"$sc" --file "$config" \
	--disable SCSI \
	--disable SCSI_MOD \
	--disable SCSI_COMMON \
	--disable BLK_DEV_SD \
	--disable SCSI_DMA \
	--disable SCSI_PROC_FS \
	--disable SCSI_CONSTANTS \
	--disable SCSI_LOGGING \
	--disable SCSI_SCAN_ASYNC \
	--disable SCSI_LOWLEVEL \
	--disable ATA \
	--disable ATA_VERBOSE_ERROR \
	--disable ATA_FORCE \
	--disable SATA_HOST \
	--disable ATA_SFF \
	--disable ATA_BMDMA \
	--disable PATA_PLATFORM \
	--disable PATA_PC9800 \
	--enable BLK_DEV_PC98_IDE

# Physical FDD is required by the target hardware profile, but the current
# generic x86 floppy layer probes PC/AT ports and CMOS data on PC-98.  Keep it
# out of the measurable baseline until the PC-98 I/O adaptation is complete.
"$sc" --file "$config" \
	--disable BLK_DEV_FD \
	--disable BLK_DEV_FD_RAWCMD

# LGY-98 and only the small IPv4 stack needed to configure and test it.
"$sc" --file "$config" \
	--enable NET \
	--enable PACKET \
	--enable UNIX \
	--enable INET \
	--set-val INET_TABLE_PERTURB_ORDER 10 \
	--disable IPV6 \
	--enable IP_PNP \
	--enable IP_PNP_DHCP \
	--enable ISA \
	--enable NETDEVICES \
	--enable ETHERNET \
	--enable NET_VENDOR_8390 \
	--enable NE2K_LGY98 \
	--disable WLAN \
	--disable WIRELESS

# Remove modern generic facilities that are not used by the fixed PC-9801
# appliance profile.  Keep Intel CPU recognition, the IPv4 stack, ext4, GDC,
# PC-98 input, LGY-98 and the current libata path.
"$sc" --file "$config" \
	--enable PREEMPT_NONE \
	--disable PREEMPT_LAZY \
	--disable PREEMPT_VOLUNTARY \
	--disable PREEMPT_DYNAMIC \
	--disable PREEMPTION \
	--disable BPF \
	--disable PERF_EVENTS \
	--disable PROFILING \
	--disable KPROBES \
	--disable FTRACE \
	--disable FUNCTION_TRACER \
	--disable MICROCODE \
	--enable PROCESSOR_SELECT \
	--enable CPU_SUP_INTEL \
	--disable CPU_SUP_CYRIX_32 \
	--disable CPU_SUP_AMD \
	--disable CPU_SUP_HYGON \
	--disable CPU_SUP_CENTAUR \
	--disable CPU_SUP_TRANSMETA_32 \
	--disable CPU_SUP_UMC_32 \
	--disable CPU_SUP_ZHAOXIN \
	--disable CPU_SUP_VORTEX_32 \
	--disable HID \
	--disable HID_SUPPORT \
	--disable HID_GENERIC \
	--disable KEYBOARD_ATKBD \
	--disable INPUT_MOUSE \
	--disable MOUSE_PS2 \
	--disable SERIO \
	--disable SERIO_I8042 \
	--disable SERIO_SERPORT \
	--disable SERIO_LIBPS2 \
	--disable INPUT_VIVALDIFMAP \
	--disable BLOCK_LEGACY_AUTOLOAD \
	--disable SWAP \
	--disable MQ_IOSCHED_DEADLINE \
	--disable MQ_IOSCHED_KYBER \
	--disable INET_DIAG \
	--disable PROC_PAGE_MONITOR \
	--disable UNIX98_PTYS \
	--disable LEGACY_PTYS \
	--disable COREDUMP \
	--disable DEBUG_KERNEL \
	--disable INIT_STACK_ALL_ZERO \
	--enable INIT_STACK_NONE \
	--disable INIT_ON_ALLOC_DEFAULT_ON \
	--disable INIT_ON_FREE_DEFAULT_ON \
	--enable LD_DEAD_CODE_DATA_ELIMINATION \
	--enable TRIM_UNUSED_KSYMS

"$sc" --file "$config" --set-str CMDLINE \
	"no387 vdso=0 console=ttyS0 console=tty0 earlyprintk=pc9800 root=/dev/hd98a2 rootfstype=ext4 rw init=/sbin/i386-init"
"$sc" --file "$config" --enable CMDLINE_BOOL
"$sc" --file "$config" --enable CMDLINE_OVERRIDE

make -C "$source" O="$build" ARCH=i386 olddefconfig
cp "$config" "$repo/configs/pc9800-i386-minimal-7.1.config"

printf 'minimal config: %s\n' "$repo/configs/pc9800-i386-minimal-7.1.config"
