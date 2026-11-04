#!/usr/bin/env bash
#
# The boot check: the file a tester downloads, unpacked as their tool unpacks
# it and started as their PC starts it, must reach first run.
#
#   ci/check-portable-boots.sh IMAGE.img.xz WORK
#
# Run by .github/workflows/build-disk.yml, type `portable`. Needs /dev/kvm,
# qemu-system-x86, ovmf, xz, btrfs-progs and binutils; sudo for the runner's
# /dev/kvm and to read the drive afterwards. Leaves serial.log and first-run.png
# in WORK.
#
# WHAT IT PROVES, or the run fails: the VM reaches first run's time zone screen
# (the console's own line, `[first-run] starting at timezone`) with something
# on the screen; the system grew into its drive; no account has a password that
# can log in; and nothing wrote a boot entry into the PC's firmware.
#
# WHAT IT CANNOT PROVE: a real USB stick, a real GPU (the console runs on its
# software rendering here), or Etcher and Rufus themselves. tools/installer-vm.sh
# portable tries the same file beside a Windows drive; a real stick is a person's.
#
# THE ONE THING ADDED TO THE VM. The image has no serial console (make-portable-
# image.sh), so the journal reaches this script through a unit passed in QEMU's
# SMBIOS table as a systemd credential. It lives only in that VM's memory, so the
# file booted is the file a tester gets. It starts after the journal moves to the
# drive: a `journalctl --follow` started before that stops at the move.
set -euo pipefail

image=$(realpath "${1:?usage: check-portable-boots.sh IMAGE.img.xz WORK}")
work=${2:?usage: check-portable-boots.sh IMAGE.img.xz WORK}

# The first start took 4 minutes 10 seconds on the runner and the second 45
# seconds to first run (2026-11-03). Five times that is a fault, not a slow day.
readonly LIMIT=1200
readonly FIRST_RUN='[first-run] starting at timezone'
readonly DRIVE_GIB=64

mkdir -p "$work"
cd "$work"

qemu=""
loop=""
finish() {
  if mountpoint -q root 2>/dev/null; then sudo umount root; fi
  if [[ -n $loop ]]; then sudo losetup -d "$loop"; fi
  if [[ -n $qemu ]] && kill -0 "$qemu" 2>/dev/null; then kill "$qemu"; fi
}
trap finish EXIT

faults=0
fault() { echo "check-portable-boots: $*" >&2; faults=$((faults + 1)); }

[[ -e /dev/kvm ]] || { echo "check-portable-boots: this runner has no /dev/kvm" >&2; exit 1; }
sudo chmod 666 /dev/kvm

# As the tester's tool writes it, onto a drive bigger than the file.
xz --decompress --stdout "$image" | dd of=usb.img bs=1M iflag=fullblock conv=sparse status=none
truncate --size="${DRIVE_GIB}G" usb.img
cp /usr/share/OVMF/OVMF_VARS_4M.fd vars.fd

unit='[Unit]
Description=The journal on the serial port, for the boot check
DefaultDependencies=no
After=systemd-journal-flush.service
Conflicts=shutdown.target
Before=shutdown.target
[Service]
ExecStart=/bin/sh -c "exec journalctl --boot --lines=all --follow --no-pager --no-hostname -o short-monotonic >/dev/ttyS0 2>&1"
'
wants='[Unit]
Wants=boot-check-journal.service
'
qemu-system-x86_64 -machine q35,accel=kvm -cpu host -smp "$(nproc)" -m 8192 \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,file=vars.fd \
  -device virtio-vga,xres=1280,yres=800 -display none \
  -device qemu-xhci -device usb-kbd -device usb-tablet -nic none \
  -drive file=usb.img,format=raw,if=none,id=usb0 \
  -device usb-storage,drive=usb0,removable=on,bootindex=1 \
  -smbios "type=11,value=io.systemd.credential.binary:systemd.extra-unit.boot-check-journal.service=$(base64 -w0 <<<"$unit")" \
  -smbios "type=11,value=io.systemd.credential.binary:systemd.unit-dropin.sysinit.target~boot-check=$(base64 -w0 <<<"$wants")" \
  -serial file:serial.log -qmp unix:qmp.sock,server,wait=off &
qemu=$!

qmp() {
  python3 -I - "$@" <<'PY'
import json, socket, sys
s = socket.socket(socket.AF_UNIX)
s.connect("qmp.sock")
f = s.makefile("rw")
f.readline()
def call(cmd, **args):
    f.write(json.dumps({"execute": cmd, "arguments": args}) + "\n")
    f.flush()
    while True:
        r = json.loads(f.readline())
        if "error" in r:
            sys.exit("qmp: " + r["error"]["desc"])
        if "return" in r:
            return
call("qmp_capabilities")
if sys.argv[1] == "shot":
    call("screendump", filename=sys.argv[2], format=sys.argv[3])
else:
    call(sys.argv[1])
PY
}

# Something on the screen: a black or one-colour picture is a console that
# logged the line and drew nothing.
drawn() {
  python3 -I - "$1" <<'PY'
import sys
data = open(sys.argv[1], "rb").read()
# P6, width, height, maximum, then the pixels.
fields, at = [], 0
while len(fields) < 4:
    end = min(i for i in (data.find(b" ", at), data.find(b"\n", at)) if i >= 0)
    if end > at:
        fields.append(data[at:end])
    at = end + 1
pixels = data[at:]
colours = {pixels[i:i + 3] for i in range(0, len(pixels), 3)}
print(f"{len(colours)} colours on the screen")
sys.exit(0 if len(colours) >= 16 else 1)
PY
}

started=$SECONDS
reached=0
while ((SECONDS - started < LIMIT)); do
  if ! kill -0 "$qemu" 2>/dev/null; then
    fault "the VM stopped by itself after $((SECONDS - started)) s"
    break
  fi
  if [[ -f serial.log ]] && grep -aqF "$FIRST_RUN" serial.log; then
    reached=1
    break
  fi
  sleep 5
done

if ((reached)); then
  echo "first run's time zone screen after $((SECONDS - started)) s"
  sleep 30
  qmp shot "$PWD/first-run.ppm" ppm
  qmp shot "$PWD/first-run.png" png
  drawn first-run.ppm || fault "the screen is blank at first run"
elif ((SECONDS - started >= LIMIT)); then
  fault "no '$FIRST_RUN' within $LIMIT s"
  qmp shot "$PWD/first-run.png" png || true
fi

if kill -0 "$qemu" 2>/dev/null; then
  qmp system_powerdown || true
  for _ in $(seq 1 40); do
    kill -0 "$qemu" 2>/dev/null || break
    sleep 3
  done
fi
if kill -0 "$qemu" 2>/dev/null; then kill "$qemu"; fi
wait "$qemu" || true
qemu=""

# What it left on the drive, read as a person would read it from another PC.
loop=$(sudo losetup --find --partscan --read-only --show usb.img)
sudo udevadm settle
mkdir -p root
sudo mount -o ro,rescue=nologreplay "${loop}p4" root

size=$(df -B1 --output=size root | tail -1 | tr -d ' ')
if ((size < (DRIVE_GIB - 3) * 1024 * 1024 * 1024)); then
  fault "the system did not grow into its drive: its filesystem is $size bytes"
else
  echo "the system grew into its drive: its filesystem is $((size / 1000000000)) GB"
fi

shadows=$(sudo find root/root/ostree/deploy/default/deploy -maxdepth 3 -path '*/etc/shadow')
[[ -n $shadows ]] || fault "no /etc/shadow on the drive"
for shadow in $shadows; do
  sudo grep -q '^cabinet:' "$shadow" || fault "no cabinet account in $shadow"
  # A password that can log in does not start with ! or *, and an empty one
  # needs none.
  usable=$(sudo awk -F: '$2 !~ /^[!*]/ {print $1}' "$shadow")
  [[ -z $usable ]] || fault "accounts that can log in with a password: $usable"
done

firmware=$(strings -el vars.fd | grep -iE 'shim|fedora' || true)
[[ -z $firmware ]] || fault "a boot entry was written into the PC's firmware: $firmware"

if ((faults)); then
  echo "--- the console's journal, last lines" >&2
  tail -n 60 serial.log | cut -c1-200 >&2 || true
  exit 1
fi
echo "the boot check passed"
