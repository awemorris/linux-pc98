# linux-pc98 bootloader overlay

`fs/` mirrors files overlaid onto the zedBSD BOOT FAT partition in Linux
PC-98 product images.  zedBSD itself owns the IPL, PBR, `IO.SYS`, `vmunix`,
swapfile, `/bin/sh`, `/bin/noct`, and `/bin/linux`; this directory owns only
the product menu and its applications.

Run `./build-remacs.sh` to create `fs/apps/emacs.nap`.  The generated NAP is
ignored by Git.  `fs/home/skkjisyo.dic` is a reviewed distribution asset and
is kept in Git.

Install the overlay after zedBSD has initialized the BOOT partition:

```sh
./bootloader/install-fs.sh --partition 1 IMAGE
```

The installed manifest is deliberately explicit:

- `/etc/zinit.rc`
- `/bin/menu.nct`
- `/bin/menuback.bmp`
- `/apps/holoris.nct`
- `/apps/emacs.nap`
- `/home/skkjisyo.dic`

No `.emacs.el`, `AUTOEXEC.NCT`, `holoris.nap`, or legacy boot logo is
installed.
