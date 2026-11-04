#!/usr/bin/env bash
#
# The file a tester writes to a USB drive with Etcher or Rufus:
# bootc-image-builder's raw disk, made small, made to boot as an ISO install
# boots, and compressed.
#
#   sudo ci/make-portable-image.sh DISK.raw OUTPUT.img.xz
#
# Run by .github/workflows/build-disk.yml, type `portable`. Needs root (a loop
# device and its mounts), btrfs-progs, util-linux and xz. DISK.raw is changed
# in place. OUTPUT.img.xz.sha256 is written beside the file.
#
# WHY A SECOND STEP. The builder has no setting for any of these
# (docs/decisions/2026-11-03-portable-disk-image.md). What it installed stays
# as it is; three things change:
#
# 1. THE SIZE. The builder makes the disk twice the size of the image, 28.9 GB
#    on 2026-11-03 with about 10 GB in it, and a tester's tool writes every
#    byte of a compressed file, free space too. So the filesystem is packed
#    and shrunk, and the partition and the file cut to match. The file has
#    to fit any drive big enough; cabinetos-grow-root fills the drive at the
#    first start.
# 2. THE BOOT LINE. The builder always adds `console=tty0 console=ttyS0`, which
#    sends systemd's messages to a serial port a tester's PC does not have and
#    leaves the TV on the last kernel line for the minutes a first start takes.
#    It gets what an ISO install has instead, `rhgb quiet`: the boot splash.
# 3. THE PC'S FIRMWARE. Booted from a drive's fallback path, shim runs
#    EFI/BOOT/fb*.efi when it is there, which writes a "Fedora" entry into the
#    PC's own boot list, first in the order, and that entry stays after the
#    drive is gone. Measured in the VM on 2026-11-03. Without that file shim
#    loads grub from its own folder, so grub is copied there. A portable treats
#    the PC as somebody else's
#    (docs/decisions/2026-11-03-running-from-a-usb-drive.md).
#
# THE FORMAT. .xz is the smallest format both tools open directly (their own
# lists: Etcher's supported-formats.ts has no .zst). -6 because -9 saved 2.4%
# for 11 minutes more on the runner. CRC32 because Rufus's decoder checks only
# that one and merely warns about the others.
set -euo pipefail

raw=${1:?usage: make-portable-image.sh DISK.raw OUTPUT.img.xz}
out=${2:?usage: make-portable-image.sh DISK.raw OUTPUT.img.xz}

work=$(mktemp -d)
loop=""
detach() {
  local m
  for m in "$work"/*/; do
    if mountpoint -q "$m"; then umount "$m"; fi
  done
  if [[ -n $loop ]]; then losetup -d "$loop"; loop=""; fi
}
trap 'detach; rm -rf "$work"' EXIT

fail() { echo "make-portable-image: $*" >&2; exit 1; }

attach() {
  loop=$(losetup --find --partscan --show "$raw")
  udevadm settle
}

mkdir "$work/esp" "$work/boot" "$work/root"
attach

# THE BUILDER'S LAYOUT, ASSERTED (lessons rule 10): a changed layout stops the
# build rather than ship a drive that does not boot.
[[ $(blkid -o value -s TYPE "${loop}p2") == vfat &&
   $(blkid -o value -s TYPE "${loop}p3") == ext4 &&
   $(blkid -o value -s TYPE "${loop}p4") == btrfs &&
   ! -e "${loop}p5" ]] || fail "the builder's partitions changed: $(lsblk -ln -o NAME,FSTYPE "$loop" | tr '\n' ' ')"

# 3. THE PC'S FIRMWARE.
mount "${loop}p2" "$work/esp"
efi=$work/esp/EFI
for arch in x64 ia32; do
  [[ -f $efi/BOOT/fb$arch.efi && -f $efi/fedora/grub$arch.efi ]] ||
    fail "no EFI/BOOT/fb$arch.efi or EFI/fedora/grub$arch.efi: the boot files changed"
  rm "$efi/BOOT/fb$arch.efi"
  cp "$efi/fedora/grub$arch.efi" "$efi/BOOT/grub$arch.efi"
done
umount "$work/esp"

# 2. THE BOOT LINE. `loader` is a link to the live one of loader.0 and loader.1.
mount "${loop}p3" "$work/boot"
entries=("$work"/boot/loader/entries/*.conf)
[[ ${#entries[@]} == 1 ]] || fail "expected one boot entry, found ${#entries[@]}"
grep -qE '^options .* console=tty0 console=ttyS0( |$)' "${entries[0]}" ||
  fail "the builder's boot line no longer has its serial console: $(grep '^options' "${entries[0]}")"
sed -i -E '/^options /{s/ console=tty0 console=ttyS0//; s/^options /options rhgb quiet /}' "${entries[0]}"
umount "$work/boot"

# 1. THE SIZE. Packed first, so the chunks hold no free space to carry along.
# The room left is what the first start needs before it grows; the smallest
# that shrinks at all is taken, since a shrink that has nowhere to move a chunk
# fails and changes nothing.
mount "${loop}p4" "$work/root"
btrfs balance start -dusage=100 -musage=100 "$work/root" >/dev/null
allocated=$(btrfs filesystem usage -b "$work/root" | awk '/Device allocated:/{print $3}')
mib=$((1024 * 1024))
size=""
for room in 512 768 1024 1536 2048 3072; do
  try=$(((allocated / mib + room) * mib))
  if btrfs filesystem resize "$try" "$work/root" >/dev/null 2>&1; then
    size=$try
    break
  fi
done
[[ -n $size ]] || fail "the filesystem would not shrink: $(btrfs filesystem usage "$work/root" | head -5)"
fstrim "$work/root"
umount "$work/root"
start=$(<"/sys/class/block/${loop##*/}p4/start")
detach

# The partition table written again for the shorter file, with every partition
# and id as they were: a table cut short has its backup past the file's end, and
# sfdisk will not read that. The GPT keeps its backup in the last 33 sectors.
sectors=$((size / 512))
sfdisk --dump "$raw" |
  sed -E '/^(device|last-lba):/d' |
  sed -E "/^[^ ]*4 : /s/size= *[0-9]+/size=$sectors/" >"$work/table"
truncate --size=$((((start + sectors + 33) / 2048 + 1) * mib)) "$raw"
sfdisk --quiet --no-reread --no-tell-kernel --wipe-partitions never "$raw" <"$work/table"
sfdisk --verify "$raw" >/dev/null || fail "the new partition table does not verify"

# Rufus takes a compressed file as a disk image by this mark, and Etcher reads
# the size from this header.
[[ $(od -An -tx1 -j510 -N2 "$raw" | tr -d ' \n') == 55aa ]] || fail "no boot mark in the first sector"
[[ $(dd if="$raw" bs=1 skip=512 count=8 status=none) == "EFI PART" ]] || fail "no GPT header in the second sector"

# Checked on the file as it now is, not as it was.
attach
btrfs check --readonly "${loop}p4"
mount -o ro "${loop}p4" "$work/root"
[[ $(findmnt -n -b -o SIZE "$work/root") == "$size" ]] || fail "the filesystem and the partition differ"
umount "$work/root"
detach

xz --threads=0 -6 --check=crc32 --stdout "$raw" >"$out"
(cd "$(dirname "$out")" && sha256sum "$(basename "$out")") >"$out.sha256"
chown --reference="$raw" "$out" "$out.sha256"

{
  echo "Written to a drive: $(stat -c %s "$raw") bytes ($(($(stat -c %s "$raw") / 1000000000)) GB)"
  echo "Download: $(stat -c %s "$out") bytes ($(($(stat -c %s "$out") / 1000000000)) GB)"
  cat "$out.sha256"
} | tee -a "${GITHUB_STEP_SUMMARY:-/dev/null}"
