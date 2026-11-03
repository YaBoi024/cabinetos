#!/usr/bin/env bash
#
# A throwaway virtual machine on the A9 for trying the installer ISO.
#
# WHY THIS EXISTS. The installer erases a drive, and the only real hardware is
# MMagTech's own console. So every installer change is tried here first: a
# fresh empty disk, the ISO in the drive, no network unless asked, and a
# picture of the screen back on the Mac after every step. The console's own
# install is never touched; the VM is a file under ~/installer-vm.
#
# IT RUNS FROM THE MAC. QEMU runs on the A9 inside a podman container (the
# image carries no QEMU, and should not), with /dev/kvm passed in, so the VM
# runs at full speed on the console's 24 cores. Headless: nothing reaches the
# television, so this is safe while somebody is playing.
#
# Usage:
#   tools/installer-vm.sh setup              build the QEMU container (once)
#   tools/installer-vm.sh install ISO        empty 64 GB disk, boot ISO from it
#                                            (ISO is a file in ~/installer-vm)
#   tools/installer-vm.sh boot               boot what was installed, no ISO
#   tools/installer-vm.sh iso ISO            boot the ISO, keeping the disks
#   tools/installer-vm.sh shot FILE.png      the VM's screen, to FILE.png here
#   tools/installer-vm.sh key ret [tab ...]  press keys (QEMU qcodes: ret, tab,
#                                            spc, esc, up, down, left, right,
#                                            y, n, f1 ... ; ctrl-alt-f2 chords)
#   tools/installer-vm.sh type 'text'        type text
#   tools/installer-vm.sh click X Y          click at a pixel of a 1280x800 shot
#   tools/installer-vm.sh stop               switch the VM off
#
#   --net     give the VM a network (user-mode NAT). Without it the VM has no
#             network device at all, which is the point: the install is meant
#             to need none (#136).
#   --disks N add N-1 more empty disks, for trying a machine with two drives.
#   --res WxH the screen's size (default 1280x800), e.g. 1920x1080 for a TV.
#             Shots and clicks are then in that size: click X Y W H.
#
# The VM's disk is an NVMe drive, as on the A9, so the installer sees a model
# name ("QEMU NVMe Ctrl") and a size, the two things its question must name.
set -uo pipefail

A9=${CABINETOS_A9:?set it to cabinet@<your console> (CLAUDE.local.example.md)}
KEY=${CABINETOS_KEY:-$HOME/.ssh/cabinetos}
SSH=(ssh -i "$KEY" -p 2222 -o ConnectTimeout=20 -o StrictHostKeyChecking=no "$A9")
DIR=installer-vm             # under the A9's home
NAME=installer-vm            # container and image name

NET=0
DISKS=1
RES=""
ARGS=()
while (($#)); do
  case "$1" in
    --net) NET=1 ;;
    --disks) DISKS=$2; shift ;;
    --res) RES=$2; shift ;;
    *) ARGS+=("$1") ;;
  esac
  shift
done
set -- "${ARGS[@]}"
CMD=${1:-}; shift || true

# QMP from the A9's own python: the socket sits in the shared directory.
qmp() {
  "${SSH[@]}" "python3 - $(printf '%q ' "$@")" <<'PY'
import json, socket, sys, os
s = socket.socket(socket.AF_UNIX)
s.connect(os.path.expanduser("~/installer-vm/qmp.sock"))
f = s.makefile("rw")
def call(cmd, **args):
    f.write(json.dumps({"execute": cmd, "arguments": args}) + "\n"); f.flush()
    while True:
        r = json.loads(f.readline())
        if "return" in r or "error" in r:
            if "error" in r: sys.exit("qmp: " + r["error"]["desc"])
            return r["return"]
f.readline(); call("qmp_capabilities")
op, rest = sys.argv[1], sys.argv[2:]
SHIFTED = {'!':'1','@':'2','#':'3','$':'4','%':'5','^':'6','&':'7','*':'8','(':'9',')':'0',
           '_':'minus','+':'equal','{':'bracket_left','}':'bracket_right',':':'semicolon',
           '"':'apostrophe','<':'comma','>':'dot','?':'slash','|':'backslash','~':'grave_accent'}
PLAIN = {' ':'spc','-':'minus','=':'equal','[':'bracket_left',']':'bracket_right',';':'semicolon',
         "'":'apostrophe',',':'comma','.':'dot','/':'slash','\\':'backslash','`':'grave_accent','\n':'ret'}
def keys(names):
    call("send-key", keys=[{"type": "qcode", "data": k} for k in names], **{"hold-time": 50})
if op == "shot":
    call("screendump", filename="/work/shot.png", format="png")  # the path inside the container
elif op == "key":
    import time
    for k in rest:
        keys(k.split("-")); time.sleep(0.25)
elif op == "type":
    import time
    for ch in " ".join(rest):
        if ch.isupper(): keys(["shift", ch.lower()])
        elif ch in SHIFTED: keys(["shift", SHIFTED[ch]])
        elif ch in PLAIN: keys([PLAIN[ch]])
        else: keys([ch])
        time.sleep(0.04)
elif op == "click":
    # Screen pixels of the last shot (the VM's mode, 1280x800 by default).
    import time
    x, y, w, h = (int(v) for v in rest)
    call("input-send-event", events=[
        {"type": "abs", "data": {"axis": "x", "value": x * 32767 // (w - 1)}},
        {"type": "abs", "data": {"axis": "y", "value": y * 32767 // (h - 1)}}])
    time.sleep(0.1)
    call("input-send-event", events=[{"type": "btn", "data": {"down": True, "button": "left"}}])
    time.sleep(0.08)
    call("input-send-event", events=[{"type": "btn", "data": {"down": False, "button": "left"}}])
elif op == "quit":
    call("quit")
PY
}

run_vm() {   # $1 = ISO path on the A9, or empty to boot the disk
  local iso=$1 net="-nic none" extra=""
  ((NET)) && net="-nic user,model=virtio-net-pci"
  [[ -n $iso ]] && extra="-drive file=/work/$iso,media=cdrom,readonly=on,if=none,id=cd0 -device ide-cd,drive=cd0,bus=ide.0,bootindex=1"
  local disks="" i
  for ((i = 0; i < DISKS; i++)); do
    disks+=" -drive file=/work/disk$i.qcow2,if=none,id=d$i -device nvme,serial=CABINETVM0$i,drive=d$i,bootindex=$((i + 2))"
  done
  "${SSH[@]}" "cd ~/$DIR && podman rm -f $NAME >/dev/null 2>&1; rm -f qmp.sock
    podman run -d --name $NAME --device /dev/kvm --security-opt label=disable \
      -v ~/$DIR:/work localhost/$NAME:latest \
      qemu-system-x86_64 -machine q35,accel=kvm -cpu host -smp 8 -m 6144 \
        -drive if=pflash,format=raw,readonly=on,file=/usr/share/edk2/ovmf/OVMF_CODE.fd \
        -drive if=pflash,format=raw,file=/work/vars.fd \
        -device virtio-vga${RES:+,xres=${RES%x*},yres=${RES#*x}} -display none -vnc 127.0.0.1:1 \
        -device qemu-xhci -device usb-kbd -device usb-tablet \
        $net $disks $extra \
        -qmp unix:/work/qmp.sock,server,wait=off >/dev/null && echo 'VM running'"
}

case "$CMD" in
  setup)
    "${SSH[@]}" "mkdir -p ~/$DIR && cd ~/$DIR && printf '%s\n' \
      'FROM registry.fedoraproject.org/fedora:44' \
      'RUN dnf -y install --setopt=install_weak_deps=False qemu-system-x86-core qemu-img edk2-ovmf qemu-device-display-virtio-vga qemu-device-display-virtio-gpu qemu-device-usb-host xorriso cpio guestfs-tools mtools squashfs-tools gettext grub2-tools && dnf clean all' \
      > Containerfile && podman build -t $NAME:latest ." ;;
  install)
    ISO=${1:?install needs the ISO path on the A9}
    "${SSH[@]}" "cd ~/$DIR && rm -f disk*.qcow2 && cp /usr/share/edk2/ovmf/OVMF_VARS.fd vars.fd 2>/dev/null \
      || podman run --rm -v ~/$DIR:/work --security-opt label=disable localhost/$NAME:latest cp /usr/share/edk2/ovmf/OVMF_VARS.fd /work/vars.fd
      for ((i = 0; i < $DISKS; i++)); do podman run --rm -v ~/$DIR:/work --security-opt label=disable localhost/$NAME:latest qemu-img create -q -f qcow2 /work/disk\$i.qcow2 64G; done"
    run_vm "$ISO" ;;
  boot)
    run_vm "" ;;
  iso)
    # The ISO again with the disks as they are, for reading what an install
    # wrote from the installer's shell (ctrl-alt-f2) while its question waits.
    run_vm "${1:?iso needs the ISO}" ;;
  repack)
    # A TEST ISO, never a shipping one: an existing ISO with this checkout's
    # disk_config/iso.toml put into it the way bootc-image-builder would (the
    # kickstart as /osbuild.ks, the module list as the installer's
    # 90-osbuild.conf), then this checkout's look put on it by
    # ci/brand-installer-iso.sh, as build-disk.yml does. Minutes instead of a
    # CI build per try. The ISO that ships is always CI's, built from the same
    # files.
    SRC=${1:?repack needs a source ISO in ~/installer-vm}
    OUT=${2:?and an output name}
    scp -q -i "$KEY" -P 2222 "$(dirname "$0")/../disk_config/iso.toml" "$A9:$DIR/iso.toml" || exit 1
    (cd "$(dirname "$0")/.." && rsync -aR --delete -e "ssh -i $KEY -p 2222" ci/brand-installer-iso.sh disk_config "$A9:$DIR/repo/") || exit 1
    "${SSH[@]}" "cd ~/$DIR && python3 - <<'PY'
import tomllib
d = tomllib.load(open('iso.toml', 'rb'))['customizations']['installer']
open('osbuild.ks', 'w').write('%include /run/install/repo/osbuild-base.ks\n' + d['kickstart']['contents'])
# bib's own list (legacy_iso.go), plus enable, minus disable: disable wins.
base = {'Network', 'Payloads', 'Runtime', 'Security', 'Services', 'Storage', 'Users'}
mods = {'org.fedoraproject.Anaconda.Modules.' + m for m in base} | set(d['modules'].get('enable', []))
mods -= set(d['modules'].get('disable', []))
open('90-osbuild.conf', 'w').write('# osbuild customizations\n\n[Anaconda]\nactivatable_modules=\n' + ''.join('    %s\n' % m for m in sorted(mods)))
PY
      # The module list goes in as an updates image, which the installer lays
      # over its own files at boot (images/updates.img on the install media),
      # rather than by rebuilding install.img: unpacking that without root
      # loses its SELinux labels.
      podman run --rm -v ~/$DIR:/work --security-opt label=disable localhost/$NAME:latest sh -ec '
        mkdir -p /tmp/u/etc/anaconda/conf.d && cd /tmp/u
        cp /work/90-osbuild.conf etc/anaconda/conf.d/90-osbuild.conf
        find . | cpio -o -H newc --quiet | gzip > /tmp/updates.img
        rm -f /work/$OUT /work/.branded.iso
        /work/repo/ci/brand-installer-iso.sh /work/$SRC /work/.branded.iso
        xorriso -indev /work/.branded.iso -outdev /work/$OUT -boot_image any replay \
          -map /work/osbuild.ks /osbuild.ks -map /tmp/updates.img /images/updates.img 2>&1 | tail -1
        rm -f /work/.branded.iso'" ;;
  shot)
    OUT=${1:?shot needs a file name}
    qmp shot && scp -q -i "$KEY" -P 2222 "$A9:$DIR/shot.png" "$OUT" && echo "$OUT" ;;
  key)  qmp key "$@" ;;
  type) qmp type "$@" ;;
  click) qmp click "$1" "$2" "${3:-1280}" "${4:-800}" ;;
  stop) qmp quit 2>/dev/null; "${SSH[@]}" "podman rm -f $NAME >/dev/null 2>&1; echo stopped" ;;
  *) sed -n '2,40p' "$0"; exit 1 ;;
esac
