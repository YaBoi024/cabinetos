#!/usr/bin/env bash
#
# The installer ISO in CabinetOS's look: bootc-image-builder's ISO, repacked.
#
#   ci/brand-installer-iso.sh SOURCE.iso OUTPUT.iso
#
# Run by .github/workflows/build-disk.yml on every ISO it builds, and by
# tools/installer-vm.sh repack for test ISOs. Needs xorriso, mtools,
# squashfs-tools, gettext (msgfmt), GRUB's script check (grub-script-check
# or grub2-script-check), cpio and gzip.
#
# WHY A REPACK. The builder has no setting for the boot menu, the kernel's
# command line or the installer's own files (#284), so the ISO it makes is
# changed afterwards (docs/decisions/2026-11-02-installer-iso-repacked.md).
# Nothing it built is unpacked or rebuilt: the installer's image, its initrd
# and the kernel stay byte for byte. Three things are added or changed:
#
# 1. The boot menu, disk_config/grub.cfg, in all three places it lives: the
#    EFI boot partition (a FAT image appended after the ISO9660 tree, which
#    UEFI reads), the copy of that image in the tree, and BIOS's
#    /boot/grub2/grub.cfg. It carries the builder's own kernel line, so that
#    line is checked in the builder's menu first. Its picture under UEFI,
#    disk_config/grub-theme.txt and grub-background.png, goes in
#    /boot/grub2/themes/cabinetos/.
# 2. images/product.img, which the installer's initrd lays over the
#    installer's files at boot (as it does updates.img): its /.buildstamp
#    without Fedora's version, and an English catalog for the one line that
#    would still leave a gap for it, so the title reads "CABINETOS
#    INSTALLATION" rather than "CABINETOS 44 INSTALLATION";
#    disk_config/installer.conf as /etc/anaconda/conf.d/95-cabinetos.conf;
#    and the stylesheet and logo.
# 3. images/cabinetos-wait.img, a small initrd the boot menu loads after the
#    builder's, with the waiting line (disk_config/installer-wait.sh), which
#    the product image also carries for after the switch to the installer's
#    root.
# Nothing else: the kickstart, the payload and the volume label are the
# builder's.
set -euo pipefail

src=${1:?usage: brand-installer-iso.sh SOURCE.iso OUTPUT.iso}
out=${2:?usage: brand-installer-iso.sh SOURCE.iso OUTPUT.iso}
dc=$(cd "$(dirname "$0")/../disk_config" && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

fail() { echo "brand-installer-iso: $*" >&2; exit 1; }

# The font is only checked for: the UEFI menu loads it to draw its picture.
xorriso -osirrox on -indev "$src" \
  -extract /boot/grub2/grub.cfg "$work/builder-grub.cfg" \
  -extract /EFI/BOOT/fonts/unicode.pf2 "$work/unicode.pf2" \
  -extract /images/efiboot.img "$work/efiboot.img" \
  -extract /images/install.img "$work/install.img" 2>/dev/null \
  || fail "could not read the boot menu, its font or the installer image from $src"

# THE BUILDER'S KERNEL LINE, ASSERTED (lessons rule 10). Our menu repeats it;
# if the builder ever changes it, this stops the build rather than ship a
# menu that boots the old way.
for line in \
  "search --no-floppy --set=root -l 'CabinetOS'" \
  "linux /images/pxeboot/vmlinuz inst.stage2=hd:LABEL=CabinetOS inst.ks=hd:LABEL=CabinetOS:/osbuild.ks quiet" \
  "initrd /images/pxeboot/initrd.img"; do
  grep -qF -- "$line" "$work/builder-grub.cfg" || fail "the builder's boot menu no longer has: $line"
  grep -qF -- "$line" "$dc/grub.cfg" || fail "disk_config/grub.cfg does not have: $line"
done
# GRUB's own parser, so a menu it cannot read never ships (it has no `&&`,
# for one).
"$(command -v grub2-script-check || command -v grub-script-check || echo grub-script-check)" "$dc/grub.cfg" \
  || fail "GRUB cannot parse disk_config/grub.cfg"
mtype -i "$work/efiboot.img" ::/EFI/BOOT/grub.cfg >/dev/null \
  || fail "no EFI/BOOT/grub.cfg in the EFI boot image"
mcopy -o -i "$work/efiboot.img" "$dc/grub.cfg" ::/EFI/BOOT/grub.cfg

# The product image. Its .buildstamp is the builder's with the version line
# emptied; the version was Fedora's, not CabinetOS's. The title's line is
# reworded by an English catalog (disk_config/installer-en.po), so the line
# it rewords is checked in Anaconda's own code first.
p=$work/product
mkdir -p "$p/etc/anaconda/conf.d" "$p/usr/share/anaconda/pixmaps" "$p/usr/share/locale/en/LC_MESSAGES"
unsquashfs -cat "$work/install.img" .buildstamp > "$work/buildstamp" \
  || fail "no .buildstamp in the installer image"
helpers=$(unsquashfs -l "$work/install.img" | sed -n 's|^squashfs-root/||; /site-packages\/pyanaconda\/ui\/helpers\.py$/p')
[[ -n $helpers ]] || fail "no pyanaconda/ui/helpers.py in the installer image"
title=$(sed -n 's/^msgid "\(.*INSTALLATION\)"$/\1/p' "$dc/installer-en.po")
unsquashfs -cat "$work/install.img" "$helpers" | grep -qF "\"$title\"" \
  || fail "Anaconda no longer has the title line installer-en.po rewords: $title"
# The waiting line (disk_config/installer-wait.sh and its unit), as a small
# initrd of its own that the boot menu loads after the builder's, which the
# kernel unpacks over it. The console font it doubles is the one the drive
# question uses; the builder's initrd does not carry it.
w=$work/wait
mkdir -p "$w/etc/systemd/system/sysinit.target.wants" "$w/usr/libexec" "$w/usr/lib/kbd/consolefonts"
install -m 0755 "$dc/installer-wait.sh" "$w/usr/libexec/cabinetos-installer-wait"
cp "$dc/installer-wait.service" "$w/etc/systemd/system/cabinetos-installer-wait.service"
ln -s ../cabinetos-installer-wait.service "$w/etc/systemd/system/sysinit.target.wants/cabinetos-installer-wait.service"
unsquashfs -cat "$work/install.img" usr/lib/kbd/consolefonts/Lat2-Terminus16.psfu.gz \
  > "$w/usr/lib/kbd/consolefonts/Lat2-Terminus16.psfu.gz" || fail "no Lat2-Terminus16 font in the installer image"
(cd "$w" && find . | cpio -o -H newc -R 0:0 --quiet) > "$work/cabinetos-wait.img"
rm -f "$work/install.img"
grep -qx 'product = CabinetOS' "$work/buildstamp" || fail ".buildstamp does not name CabinetOS"
grep -qx 'version = .*' "$work/buildstamp" || fail ".buildstamp has no version line"
# FORK (handheld): the product's name is "CabinetOS Handheld", so the title reads
# "CABINETOS HANDHELD INSTALLATION".
sed -e 's/^version = .*/version =/' -e 's/^product = .*/product = CabinetOS Handheld/' "$work/buildstamp" > "$p/.buildstamp"
cp "$dc/installer.conf" "$p/etc/anaconda/conf.d/95-cabinetos.conf"
cp "$dc/installer.css" "$p/usr/share/anaconda/pixmaps/cabinetos.css"
cp "$dc/installer-logo.png" "$p/usr/share/anaconda/pixmaps/cabinetos-logo.png"
cp "$dc/installer-logo@2x.png" "$p/usr/share/anaconda/pixmaps/cabinetos-logo@2x.png"
msgfmt -o "$p/usr/share/locale/en/LC_MESSAGES/anaconda.mo" "$dc/installer-en.po"
# The waiting line again, for after the switch to the installer's root.
mkdir -p "$p/etc/systemd/system/anaconda.target.wants" "$p/usr/libexec"
install -m 0755 "$dc/installer-wait.sh" "$p/usr/libexec/cabinetos-installer-wait"
cp "$dc/installer-wait.service" "$p/etc/systemd/system/cabinetos-installer-wait.service"
ln -s ../cabinetos-installer-wait.service "$p/etc/systemd/system/anaconda.target.wants/cabinetos-installer-wait.service"
(cd "$p" && find . | cpio -o -H newc -R 0:0 --quiet) | gzip -9 > "$work/product.img"

# The EFI boot partition is replaced by the edited image, the same size, and
# El Torito's EFI entry follows it (replay keeps it pointing at partition 2).
rm -f "$out"
xorriso -indev "$src" -outdev "$out" -boot_image any replay \
  -append_partition 2 28732ac11ff8d211ba4b00a0c93ec93b "$work/efiboot.img" \
  -map "$work/efiboot.img" /images/efiboot.img \
  -map "$dc/grub.cfg" /EFI/BOOT/grub.cfg \
  -map "$dc/grub.cfg" /boot/grub2/grub.cfg \
  -map "$dc/grub-theme.txt" /boot/grub2/themes/cabinetos/theme.txt \
  -map "$dc/grub-background.png" /boot/grub2/themes/cabinetos/background.png \
  -map "$work/product.img" /images/product.img \
  -map "$work/cabinetos-wait.img" /images/cabinetos-wait.img 2>&1 | tail -n 3

# Read it back: the EFI partition the firmware boots (GPT partition 2, in
# 512-byte sectors) is the edited image, and El Torito's EFI entry is it.
read -r start size < <(xorriso -indev "$out" -report_system_area plain 2>/dev/null \
  | awk '/^GPT start and size :/ && $6 == 2 { print $7, $8 }')
[[ -n ${start:-} ]] || fail "the repacked ISO has no GPT partition 2"
dd if="$out" bs=512 skip="$start" count="$size" status=none | cmp -s - "$work/efiboot.img" \
  || fail "the repacked ISO's EFI partition is not the edited image"
xorriso -indev "$out" -report_el_torito as_mkisofs 2>/dev/null | grep -q "^-e '--interval:appended_partition_2_" \
  || fail "El Torito's EFI entry is not partition 2"
echo "brand-installer-iso: $out ($(du -h "$out" | cut -f1))"
