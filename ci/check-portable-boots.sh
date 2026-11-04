#!/usr/bin/env bash
#
# The boot check: the file a tester downloads, unpacked as their tool unpacks
# it and started as their PC starts it, must reach first run, on a PC with Secure
# Boot on and on one with it off.
#
#   ci/check-portable-boots.sh IMAGE.img.xz WORK
#
# Run by .github/workflows/build-disk.yml, type `portable`. Needs /dev/kvm,
# qemu-system-x86, qemu-utils, ovmf, xz, btrfs-progs and binutils; sudo for the
# runner's /dev/kvm and to read the drive afterwards. Leaves serial.log,
# first-run.png, secure-boot-serial.log and secure-boot-first-run.png in WORK.
#
# WHAT IT PROVES, or the run fails. With Secure Boot on: shim's key screen comes
# up by itself, the keys a tester presses enrol the key on the drive, and the
# kernel then starts and reaches first run. With it off: the VM reaches first
# run's time zone screen (the console's own line, `[first-run] starting at
# timezone`) with something on the screen, with no restart for a boot setting; the
# system grew into its drive; no account has a password that can log in; and nothing
# wrote a boot entry into the PC's firmware.
#
# WHAT IT CANNOT PROVE: a real USB stick, a real GPU (the console runs on its
# software rendering here), a real PC's firmware, BitLocker, or Etcher and Rufus
# themselves. tools/installer-vm.sh portable tries the same file beside a Windows
# drive; a real stick is a person's.
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
# What a tester presses on shim's key screens, one screen to a word list: any key
# to begin; Enroll key from disk; the one partition it can read; the key, below
# EFI/ at the top of that partition; Continue; Yes; Reboot.
readonly ENROL_KEYS=(ret "down ret" ret "down ret" "down ret" "down ret" ret)

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

# start_vm JOURNAL FIRMWARE VARS DRIVE FORMAT MACHINE-ARGUMENTS...
start_vm() {
  local journal=$1 firmware=$2 vars=$3 drive=$4 format=$5
  shift 5
  rm -f qmp.sock
  qemu-system-x86_64 "$@" -cpu host -smp "$(nproc)" -m 8192 \
    -drive if=pflash,format=raw,readonly=on,file="$firmware" \
    -drive if=pflash,format=raw,file="$vars" \
    -device virtio-vga,xres=1280,yres=800 -display none \
    -device qemu-xhci -device usb-kbd -device usb-tablet -nic none \
    -drive file="$drive",format="$format",if=none,id=usb0 \
    -device usb-storage,drive=usb0,removable=on,bootindex=1 \
    -smbios "type=11,value=io.systemd.credential.binary:systemd.extra-unit.boot-check-journal.service=$(base64 -w0 <<<"$unit")" \
    -smbios "type=11,value=io.systemd.credential.binary:systemd.unit-dropin.sysinit.target~boot-check=$(base64 -w0 <<<"$wants")" \
    -serial file:"$journal" -qmp unix:qmp.sock,server,wait=off &
  qemu=$!
}

qmp() {
  python3 -I - "$@" <<'PY'
import json, socket, sys, time
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
elif sys.argv[1] == "keys":
    for key in sys.argv[2:]:
        call("send-key", keys=[{"type": "qcode", "data": key}])
        time.sleep(0.5)
else:
    call(sys.argv[1])
PY
}

# seen drawn|blue PICTURE.ppm. Drawn: something on the screen, since a black or
# one-colour picture is a console that logged the line and drew nothing. Blue:
# shim's key screen, the one screen on the way that fills so much of it in blue.
seen() {
  python3 -I - "$1" "$2" <<'PY'
import sys
data = open(sys.argv[2], "rb").read()
# P6, width, height, maximum, then the pixels.
fields, at = [], 0
while len(fields) < 4:
    end = min(i for i in (data.find(b" ", at), data.find(b"\n", at)) if i >= 0)
    if end > at:
        fields.append(data[at:end])
    at = end + 1
pixels = data[at:]
triples = [pixels[i:i + 3] for i in range(0, len(pixels) - 2, 3)]
if sys.argv[1] == "drawn":
    colours = set(triples)
    print(f"{len(colours)} colours on the screen")
    sys.exit(0 if len(colours) >= 16 else 1)
blue = sum(1 for p in triples if p[2] > 120 and p[0] < 40 and p[1] < 40)
sys.exit(0 if blue * 10 > len(triples) else 1)
PY
}

# first_run JOURNAL PICTURE.png: wait for the console's line, then look.
first_run() {
  local journal=$1 picture=$2 started=$SECONDS
  while ((SECONDS - started < LIMIT)); do
    if ! kill -0 "$qemu" 2>/dev/null; then
      fault "$journal: the VM stopped by itself after $((SECONDS - started)) s"
      return 0
    fi
    if [[ -f $journal ]] && grep -aqF "$FIRST_RUN" "$journal"; then
      echo "$journal: first run's time zone screen after $((SECONDS - started)) s"
      sleep 30
      qmp shot "$PWD/screen.ppm" ppm
      qmp shot "$PWD/$picture" png
      seen drawn screen.ppm || fault "$journal: the screen is blank at first run"
      return 0
    fi
    sleep 5
  done
  fault "$journal: no '$FIRST_RUN' within $LIMIT s"
  qmp shot "$PWD/$picture" png || true
}

stop_vm() {
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
}

# Shim's screen counts down from ten seconds, so it is watched for, and the keys
# go in as soon as it shows.
enrol_key() {
  local started=$SECONDS keys
  until qmp shot "$PWD/screen.ppm" ppm 2>/dev/null && seen blue screen.ppm; do
    if ((SECONDS - started >= 300)); then
      fault "no key screen within 300 s with Secure Boot on"
      qmp shot "$PWD/secure-boot-first-run.png" png || true
      return 1
    fi
    sleep 1
  done
  echo "shim's key screen after $((SECONDS - started)) s"
  for keys in "${ENROL_KEYS[@]}"; do
    # shellcheck disable=SC2086 # one word to a key
    qmp keys $keys
    sleep 3
  done
}

# SECURE BOOT ON, first, on a copy-on-write layer: this start leaves the drive as
# it was for the next. Microsoft's keys are in these firmware variables, as on a
# PC sold with Windows.
cp /usr/share/OVMF/OVMF_VARS_4M.ms.fd secure-boot-vars.fd
qemu-img create -q -f qcow2 -b "$PWD/usb.img" -F raw secure-boot.qcow2
start_vm secure-boot-serial.log /usr/share/OVMF/OVMF_CODE_4M.secboot.fd secure-boot-vars.fd secure-boot.qcow2 qcow2 \
  -machine q35,accel=kvm,smm=on -global driver=cfi.pflash01,property=secure,value=on -global ICH9-LPC.disable_s3=1
if enrol_key; then
  first_run secure-boot-serial.log secure-boot-first-run.png
  grep -aqF 'Secure boot enabled' secure-boot-serial.log ||
    fault "the VM with Secure Boot on did not say so: this start proved nothing"
fi
stop_vm
rm -f secure-boot.qcow2

# SECURE BOOT OFF, which keeps what it wrote, to be read below.
cp /usr/share/OVMF/OVMF_VARS_4M.fd vars.fd
start_vm serial.log /usr/share/OVMF/OVMF_CODE_4M.fd vars.fd usb.img raw -machine q35,accel=kvm
first_run serial.log first-run.png
grep -aqF 'No karg changes needed' serial.log ||
  fault "Bazzite's hardware setup wants another boot setting: add it to make-portable-image.sh's boot line, or a first start writes a deployment and restarts"
stop_vm

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
  for journal in secure-boot-serial.log serial.log; do
    echo "--- $journal, last lines" >&2
    tail -n 60 "$journal" 2>/dev/null | cut -c1-200 >&2 || true
  done
  exit 1
fi
echo "the boot check passed"
