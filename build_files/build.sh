#!/usr/bin/env bash
#
# CabinetOS image build.
#
# Called from the Containerfile with build_files/ mounted at /ctx and
# system_files/ inside it, at /ctx/system_files.
#
# The gaming stack comes from Bazzite and is kept as-is; the desktop and Steam
# are stripped out, and the session is wired in. The console itself, the
# frontend, the cores and their system files, is NOT installed here: the
# Containerfile does that in three later layers of its own, through
# install-frontend.sh.

set -euo pipefail

source /ctx/lib.sh

log "CabinetOS build starting"
log "base: $(grep '^PRETTY_NAME=' /usr/lib/os-release | cut -d= -f2-)"

# ---------------------------------------------------------------------------
# Every package this build installs is stamped with the base's own time.
# ---------------------------------------------------------------------------
#
# MEASURED 2026-10-18 (#511): 2026.10.18.6 and .7, built from the same
# build_files/ and system_files/, had OS layers that differed in only the
# rpm database, in the four packages this build installs (Steam's session
# from terra, Sunshine and miniupnpc), because rpm records the time it
# installed each one; and in dnf's countme files (removed at the end). That
# gave the 70 MB layer a new digest in every image, so every update
# downloaded it. rpm takes that time from SOURCE_DATE_EPOCH when it is set
# (tried on the A9: INSTALLTIME and INSTALLTID both follow it), so this
# build gives it the newest install time already in the base: the same for
# every build on one base, and still a real date.
SOURCE_DATE_EPOCH="$(rpm -qa --qf '%{INSTALLTIME}\n' | sort -n | tail -1)"
export SOURCE_DATE_EPOCH
log "packages installed here are stamped ${SOURCE_DATE_EPOCH}, the base's newest install time"

# ---------------------------------------------------------------------------
# Overlay any files from system_files/ onto the image.
# ---------------------------------------------------------------------------
#
# NOTE THE PATH. The Containerfile's ctx stage does `COPY system_files
# /system_files`, and that whole stage is bind-mounted at /ctx — so the files
# land at /ctx/system_files, not /system_files.
#
# This was originally written as /system_files guarded by `if [[ -d ... ]]`,
# which meant it silently copied nothing for several builds. Nobody noticed
# until a unit file was expected to be there. No guard now: if the directory is
# missing the build fails, loudly, here.
if [[ ! -d /ctx/system_files ]]; then
    log "ERROR: /ctx/system_files is missing — the Containerfile ctx stage is wrong"
    exit 1
fi

log "overlaying system_files/"
# /ctx is a read-only bind mount, so nothing here may modify the source. An
# earlier version tried to delete .gitkeep placeholders from it and failed.
# There are no placeholders now — every directory under system_files/ holds a
# real file — so there is nothing to exclude.
cp -avf /ctx/system_files/. / >/dev/null

# Prove it landed, rather than trusting cp's exit code. This is the check that
# would have caught the silent no-op above.
for expected in \
    /usr/bin/cabinetos-session \
    /usr/lib/systemd/system/cabinetos-session.service \
    /usr/lib/sysusers.d/cabinetos.conf \
    /usr/lib/tmpfiles.d/cabinetos.conf \
    /usr/lib/bootc/install/20-cabinetos.toml \
    /usr/libexec/cabinetos-flatpak-setup \
    /usr/lib/systemd/logind.conf.d/50-cabinetos.conf \
    /usr/lib/systemd/system/cabinetos-flatpak-setup.service \
    /usr/lib/systemd/system/cabinetos-flatpak-setup.timer \
    /usr/share/cabinetos/flatpaks.list \
    /usr/libexec/cabinetos-update \
    /usr/lib/systemd/system/cabinetos-update-check.service \
    /usr/lib/systemd/system/cabinetos-update-download.service \
    /usr/share/polkit-1/rules.d/61-cabinetos-update.rules \
    /usr/libexec/cabinetos-files \
    /usr/lib/systemd/system/cabinetos-files.service \
    /usr/lib/systemd/system/cabinetos-files-password.service \
    /usr/lib/systemd/system/cabinetos-developer.service \
    /usr/libexec/cabinetos-saved-services \
    /usr/lib/systemd/system/cabinetos-saved-services.service \
    /usr/lib/systemd/system-preset/40-cabinetos.preset \
    /usr/share/polkit-1/rules.d/62-cabinetos-files.rules \
    /usr/libexec/cabinetos-wii-search \
    /usr/lib/systemd/system/cabinetos-wii-search.service \
    /usr/share/polkit-1/rules.d/65-cabinetos-wii.rules \
    /usr/libexec/cabinetos-steam \
    /usr/lib/systemd/system/cabinetos-steam@.service \
    /usr/share/polkit-1/rules.d/66-cabinetos-steam.rules \
    /usr/libexec/cabinetos-pads \
    /usr/lib/systemd/system/cabinetos-pads@.service \
    /usr/share/polkit-1/rules.d/67-cabinetos-pads.rules \
    /usr/libexec/os-session-select \
    /usr/libexec/cabinetos-system-disk \
    /usr/libexec/cabinetos-steam-download \
    /usr/libexec/cabinetos-steam-libraries \
    /usr/libexec/cabinetos-report \
    /usr/bin/cabinetos-report \
    /etc/gamescope-session-plus/sessions.d/steam \
    /usr/libexec/cabinetos-drive-claim \
    /usr/lib/systemd/system/cabinetos-drive-claim.service \
    /usr/share/polkit-1/rules.d/63-cabinetos-drives.rules \
    /usr/lib/udev/rules.d/90-cabinetos-system-disk.rules \
    /usr/lib/udev/rules.d/72-cabinetos-wii-remote.rules \
    /usr/lib/udev/rules.d/99-cabinetos-wii-remote.rules \
    /etc/ssh/sshd_config.d/30-cabinetos.conf
do
    if [[ -e "${expected}" ]]; then
        log "  overlaid: ${expected}"
    else
        log "  ERROR: ${expected} did not land"
        exit 1
    fi
done

# ---------------------------------------------------------------------------
# The licences, INSIDE the image.
# ---------------------------------------------------------------------------
#
# This image contains twenty-three emulator cores. Most are GPL and six are free
# for non-commercial use only, and the person most likely to redistribute it is
# the one who does `docker pull` and never sees this repository at all. Terms
# that live only beside the binaries are terms that do not travel with them.
#
# /usr/share/licenses/<name>/ is where Fedora puts these, so anything that
# already knows how to look for a package's licence finds ours too.
log "installing licences"
mkdir -p /usr/share/licenses/cabinetos
cp /ctx/licences/LICENSE      /usr/share/licenses/cabinetos/LICENSE
cp /ctx/licences/LICENCES.md  /usr/share/licenses/cabinetos/LICENCES.md
# THE FULL TEXTS (#120), one file each, as fetched at the revision the image is
# built from. Not shown on the TV (MMagTech, 2026-09-25); here, where Fedora
# keeps every package's, and where GPL wants them: with the binaries.
mkdir -p /usr/share/licenses/cabinetos/texts
cp /ctx/licences/texts/*.txt /ctx/licences/texts/SOURCES /usr/share/licenses/cabinetos/texts/
listed=$(grep -cv '^[[:space:]]*\(#\|$\)' /ctx/licences/texts/SOURCES)
shipped=$(find /usr/share/licenses/cabinetos/texts -name '*.txt' -size +100c | wc -l)
if [[ "${listed}" -ne "${shipped}" ]]; then
    log "  ERROR: licences/SOURCES lists ${listed} texts, ${shipped} were installed"
    exit 1
fi
log "  installed ${shipped} licence texts"

# Asserted rather than assumed, for the reason the system_files overlay above
# has the same check: a copy that silently does nothing leaves a green build.
for expected in \
    /usr/share/licenses/cabinetos/LICENSE \
    /usr/share/licenses/cabinetos/LICENCES.md
do
    if [[ -s "${expected}" ]]; then
        log "  installed: ${expected} ($(wc -c < "${expected}") bytes)"
    else
        log "  ERROR: ${expected} is missing or empty"
        exit 1
    fi
done

# The one line in there that constrains what anyone may do with this image, put
# where a person reads it rather than left for them to find in a table.
grep -q 'non-commercial' /usr/share/licenses/cabinetos/LICENCES.md || {
    log "  ERROR: LICENCES.md no longer mentions the non-commercial cores"
    exit 1
}

# ---------------------------------------------------------------------------
# The console's own polkit rule.
# ---------------------------------------------------------------------------
#
# Copied by the system_files overlay above; asserted here, because this one is
# invisible when it is missing. Wi-Fi configuration works on the reference
# machine today WITHOUT it — the session user is in `wheel` and Bazzite grants
# the action to that group — so a build that silently dropped this file would
# ship a console that works until the session user leaves `wheel` (installed
# consoles have it: disk_config/iso.toml) or Bazzite drops its rule. Then
# Wi-Fi stops being configurable,
# with no error anywhere near the cause. See docs/PROJECT.md open question 17.
POLKIT_RULE=/usr/share/polkit-1/rules.d/60-cabinetos-network.rules
if [[ -s "${POLKIT_RULE}" ]]; then
    log "installed: ${POLKIT_RULE}"
else
    log "  ERROR: ${POLKIT_RULE} is missing — Wi-Fi would depend on 'wheel'"
    exit 1
fi
# The action it grants, spelled out, so a rename upstream fails the build rather
# than producing a rule that matches nothing.
grep -q 'org.freedesktop.NetworkManager.settings.modify.system' "${POLKIT_RULE}" || {
    log "  ERROR: ${POLKIT_RULE} no longer names the action it exists to grant"
    exit 1
}
# The user it names has to be the one sysusers.d creates, or the grant lands on
# nobody. Both are in this repository and they must move together.
grep -q '^u cabinet ' /usr/lib/sysusers.d/cabinetos.conf || {
    log "  ERROR: the session user is no longer 'cabinet'; ${POLKIT_RULE} names it"
    exit 1
}

# ---------------------------------------------------------------------------
# Record the starting package set.
# ---------------------------------------------------------------------------
#
# Written into the image at /usr/share/cabinetos/. Having the before-and-after
# in the image makes it possible to answer "what did we actually remove?"
# without re-running the build, and makes Bazzite bumps diffable.
mkdir -p /usr/share/cabinetos
rpm -qa | sort > /usr/share/cabinetos/packages-before-strip.txt
log "base image has $(wc -l < /usr/share/cabinetos/packages-before-strip.txt) packages"

# ---------------------------------------------------------------------------
# Strip.
# ---------------------------------------------------------------------------
#
# Order matters slightly: Steam first, because the desktop pass removes desktop
# entries and we want Steam's gone before then.
/ctx/strip-steam.sh
/ctx/strip-desktop.sh

# Steam's Big Picture session, for the one Steam entry (#223). After the strip,
# which removes cardwire: the session would launch Steam through it.
/ctx/install-steam-session.sh

# Sunshine, for Remote Play (#286): Sunshine's own Fedora package, pinned and
# checksummed. After the strip, so nothing it pulls in is taken out again.
/ctx/install-sunshine.sh

# ---------------------------------------------------------------------------
# Enable SSH.
# ---------------------------------------------------------------------------
#
# Runs after the strip scripts, so it can assert that nothing they removed took
# openssh-server with it. See enable-ssh.sh: sshd is installed, and nothing
# listens until someone at the TV turns File access or Developer access on.
/ctx/enable-ssh.sh

# ---------------------------------------------------------------------------
# Updates must be signed by CabinetOS (#135).
# ---------------------------------------------------------------------------
#
# After the overlay, which put the public key and registries.d/cabinetos.yaml
# in place; the script adds the rule to policy.json and checks it.
/ctx/require-signed-updates.sh

# ---------------------------------------------------------------------------
# Repair repository definitions.
# ---------------------------------------------------------------------------
#
# After everything that adds or removes a repository, so it sees the final
# state. A repo whose GPG key was never shipped
# breaks any operation that reads all repos — which is what building an
# installer ISO does. See the script for the full reasoning.
/ctx/fix-repos.sh

# ---------------------------------------------------------------------------
# Phase 2: the session.
# ---------------------------------------------------------------------------
#
# Makes the machine boot into gamescope instead of a console, and closes the
# routes to a desktop that the strip pass left behind.
/ctx/configure-session.sh

# ---------------------------------------------------------------------------
# Emulator flatpaks.
# ---------------------------------------------------------------------------
#
# After the strip, because it asserts that strip-desktop.sh masked the flatpak
# update timers — which is what keeps a pinned emulator pinned. Nothing is
# installed here; see the script for why that is forced by bootc rather than
# chosen.
/ctx/configure-flatpaks.sh

# ---------------------------------------------------------------------------
# HDMI-CEC.
# ---------------------------------------------------------------------------
#
# After everything that could remove a package. HDMI-CEC is shelved
# (docs/PROJECT.md open question 31, 2026-09-24), but its packages are
# kept installable for the day it comes back, and every one looks like cruft
# in a package list: this reinstalls one that went, and fails the build if it
# cannot.
/ctx/require-cec.sh

# ---------------------------------------------------------------------------
# Frontend runtime libraries.
# ---------------------------------------------------------------------------
#
# Same reasoning and the same position in the order: after anything that
# could have removed a package. The frontend links against these, and they are
# in the base image incidentally rather than by declared dependency.
/ctx/require-frontend-libs.sh

# ---------------------------------------------------------------------------
# The commands and libraries the frontend runs but does not link.
# ---------------------------------------------------------------------------
#
# Here, after the strip and everything else that removes or installs a
# package, for the reason require-frontend-libs.sh is last. Until 2026-10-17
# these ran before the strip, where they could not see what it removed.
#
# NOTHING ELSE WOULD CATCH THESE GOING MISSING. ci/base-watch.txt watches shared
# LIBRARIES, require-frontend-libs.sh checks libraries by name and
# install-frontend.sh reads `ldd`; none sees a command. So a strip pass that took
# NetworkManager or polkit out would leave a green build, a binary that links
# perfectly, and a console that cannot see a Wi-Fi network or say why. They are
# a real dependency of frontend/src/net.cpp and they are invisible to every
# check this repository already has.
#
# They are in the base image today: NetworkManager owns nmcli, polkit owns
# pkcheck and bluez owns bluetoothctl. None is something CabinetOS installs, and
# none should be removed — a console that cannot configure its own network
# cannot complete first run at all (open question 15b's one hard gate), and one
# that cannot pair a controller finishes setup owning a games console nobody can
# play from a sofa.
# tailscale and tailscaled: Remote Play away from home (#286) runs the
# command and starts the daemon. Bazzite's, like the three above.
# wpctl: the volume keys (#351, volume.cpp). WirePlumber's, in the base.
# pdfinfo and pdftoppm: a game's manual (#364, manual.cpp) is measured and its
# pages drawn by them. poppler-utils, in the base.
for needed in /usr/bin/nmcli /usr/bin/pkcheck /usr/bin/bluetoothctl /usr/bin/tailscale \
              /usr/sbin/tailscaled /usr/bin/wpctl /usr/bin/pdfinfo /usr/bin/pdftoppm; do
    if [[ -x "${needed}" ]]; then
        log "present: ${needed} ($(rpm -qf "${needed}" 2>/dev/null || echo 'unowned'))"
    else
        log "  ERROR: ${needed} is not in this image — the frontend runs it"
        exit 1
    fi
done

# THE SAME BLIND SPOT, ONE STEP WORSE: A LIBRARY THAT IS dlopen'd.
#
# frontend/src/gpu.cpp opens libvulkan.so.1 by hand rather than linking it, so
# that the binary still starts on a machine with no Vulkan and says so. That is
# the right behaviour and it costs the one check that would have caught its
# absence: `ldd` on the frontend does not name it, so install-frontend.sh's
# sweep cannot see it (require-frontend-libs.sh could list it by name, and
# this check is where it is named instead).
#
# WITHOUT IT, PLAYSTATION 2 AND GAMECUBE SIMPLY DO NOT PLAY, and the console
# says only "this core wants Vulkan and no libvulkan.so.1 on this machine" —
# on a machine whose GPU is fine. Every other core keeps working, so the build
# is green, the console boots, and one tier of the library quietly goes dark.
#
# The ICD matters as much as the loader: a loader with no driver behind it
# enumerates zero devices, which is exactly what the test VM reports.
for needed in /usr/lib64/libvulkan.so.1 /usr/lib64/libvulkan_radeon.so; do
    if [[ -e "${needed}" ]]; then
        log "present: ${needed} ($(rpm -qf "${needed}" 2>/dev/null || echo 'unowned'))"
    else
        log "  ERROR: ${needed} is not in this image — PS2 and GameCube need it"
        exit 1
    fi
done

# ---------------------------------------------------------------------------
# A built-in speaker counts as not there (#547).
# ---------------------------------------------------------------------------
#
# Edits one PipeWire file so a speaker inside the case never takes the sound
# from the TV, and fails the build if a PipeWire update changed that file.
/ctx/built-in-speaker.sh

# ---------------------------------------------------------------------------
# The console itself is NOT installed here, as of 2026-09-23.
# ---------------------------------------------------------------------------
#
# The frontend, the cores and their system files are three later RUN steps in
# the Containerfile, each its own image layer, so that a frontend change ships
# the frontend and not everything this script touched. See the Containerfile
# and docs/PROJECT.md open question 27. The library checks above still run
# first and are still the legible error: install-frontend.sh's ldd sweep runs
# after them, in the last layer.

# ---------------------------------------------------------------------------
# dnf's transaction history, removed so this layer builds the same every time.
# ---------------------------------------------------------------------------
#
# MEASURED 2026-09-23: two builds from identical inputs produced an OS layer
# that differed in exactly three files, these, because dnf5 stamps each
# transaction with the time it ran. Everything else, the rpm database and the
# SELinux policy included, came out byte-identical. Those 4 MB were enough to
# give the whole 60 MB layer a new digest on every build, so every update
# shipped it whether anything had changed or not. See docs/PROJECT.md open
# question 27.
#
# Nothing reads it. This is an image-based console: nobody runs `dnf install`
# on it, and `dnf history` on a deployed machine is not a thing anyone does.
# dnf5 recreates the database, empty, if it is ever opened. The package list
# itself is what the rpm database and packages-after-strip.txt record.
rm -f /usr/lib/sysimage/libdnf5/transaction_history.sqlite*
log "removed dnf's transaction history, which is dated and would change this layer every build"

# And dnf's state in /var/lib/dnf, which the base does not have: a countme
# file per repository this build fetched from, holding the week it ran
# (#511), and a lock. /var in an image reaches only a console installed from
# it, and nothing on a console runs dnf.
rm -rf /var/lib/dnf

# And what dnf and the SELinux tools leave in /run. `bootc container lint`
# warns about it (nonempty-run-tmp), and it was always there; the rechunk used
# to drop it on the way out. /run is emptied at every boot, so a console never
# sees these, and without them the lint has nothing to say.
rm -rf /run/dnf /run/selinux-policy

# ---------------------------------------------------------------------------
# Record the result.
# ---------------------------------------------------------------------------
rpm -qa | sort > /usr/share/cabinetos/packages-after-strip.txt
log "image now has $(wc -l < /usr/share/cabinetos/packages-after-strip.txt) packages"

group_start "Packages removed by this build"
comm -23 /usr/share/cabinetos/packages-before-strip.txt \
         /usr/share/cabinetos/packages-after-strip.txt
group_end

# ---------------------------------------------------------------------------
# Sanity checks.
# ---------------------------------------------------------------------------
#
# These are the things whose absence would make the image useless but would not
# fail the build. Better to fail loudly here than to find out on real hardware.
group_start "Sanity checks"

check_present() {
    local what="$1"
    local path="$2"
    if [[ -e "${path}" ]]; then
        log "  ok: ${what} (${path})"
    else
        log "  MISSING: ${what} (${path})"
        return 1
    fi
}

failed=0

# cage is the middle rung of the session's compositor ladder — the one that
# makes a GPU-less VM a usable development target, because it renders in
# software where gamescope refuses to. Looks like desktop cruft; is not.
if command -v cage >/dev/null 2>&1; then
    log "  ok: cage ($(command -v cage))"
else
    log "  MISSING: cage — the software-rendering session fallback"
    failed=1
fi

# gamescope is the compositor Phase 2 and Phase 5 are built on. If a removal
# ever takes it out, the project stops. Checked via PATH rather than a fixed
# location, because Bazzite sources it from the terra repo and may move it.
if command -v gamescope >/dev/null 2>&1; then
    log "  ok: gamescope ($(command -v gamescope))"
else
    log "  MISSING: gamescope is not on PATH"
    failed=1
fi

# The kernel. bootc container lint would also catch this, but the error here is
# more legible.
check_present "kernel modules directory" /usr/lib/modules || failed=1

# SSH, since 2026-10-07 (enable-ssh.sh; issue #134): NOTHING listens at boot.
# Port 22 is File access and port 2222 is Developer access; both start off,
# and each is started only by its switch in Settings. The always-on
# development shell (cabinetos-dev-ssh.service) is gone and must stay gone.
# The word, not the exit status: `is-enabled` also exits 0 for "static", which
# is what cabinetos-developer.service is (no [Install]: nothing can enable it),
# and that failed the first build of this check (2026-10-07).
for unit in sshd.service sshd.socket cabinetos-developer.service; do
    if [[ "$(systemctl is-enabled "${unit}" 2>/dev/null)" == enabled* ]]; then
        log "  WRONG: ${unit} is enabled; a port would be open with its switch off"
        failed=1
    fi
done
if [[ -e /usr/lib/systemd/system/cabinetos-dev-ssh.service ||
      -e /usr/share/cabinetos/DEVELOPMENT-IMAGE ]]; then
    log "  WRONG: the always-on development shell is back (cabinetos-dev-ssh.service or its marker)"
    failed=1
fi
log "  ok: no SSH port open at boot"
check_present "File access's sshd rules" /etc/ssh/sshd_config.d/30-cabinetos.conf || failed=1

# The session must be enabled, or the machine boots to a console — which is
# Phase 1 behaviour, not Phase 2.
if systemctl is-enabled cabinetos-session.service >/dev/null 2>&1; then
    log "  ok: cabinetos-session.service is enabled"
else
    log "  MISSING: cabinetos-session.service is not enabled"
    failed=1
fi

# SELinux will refuse to execute the session script from the wrong place — this
# cost a debugging round on the VM (203/EXEC from /etc). /usr/bin is correct and
# gets labelled bin_t automatically.
check_present "session script" /usr/bin/cabinetos-session || failed=1

# The emulator flatpak units. Enabled in configure-flatpaks.sh, checked again
# here for the same reason everything else in this block is: absence would not
# fail the build, it would just mean no PS3, Xbox or Switch on the machine and
# nothing saying so.
for unit in cabinetos-flatpak-setup.service cabinetos-flatpak-setup.timer; do
    if systemctl is-enabled "${unit}" >/dev/null 2>&1; then
        log "  ok: ${unit} is enabled"
    else
        log "  MISSING: ${unit} is not enabled"
        failed=1
    fi
done

check_present "emulator flatpak manifest" /usr/share/cabinetos/flatpaks.list || failed=1

# SYSTEM UPDATE, docs/SETTINGS.md, System. The root half is a script that runs
# these; every one is in the base today and none is something we install, so
# a strip pass or a base bump that took one away would ship a console whose
# update row says "Couldn't check" for a reason nobody would connect to it.
for needed in bootc skopeo jq ostree flock chpasswd mountpoint semanage; do
    if command -v "${needed}" >/dev/null 2>&1; then
        log "  ok: ${needed} ($(command -v "${needed}"))"
    else
        log "  MISSING: ${needed}, which cabinetos-update, cabinetos-files or the dev shell runs"
        failed=1
    fi
done
# The rule has to name the units that exist, or the session's start is
# refused and the row can only ever say it could not.
for unit in cabinetos-update-check.service cabinetos-update-download.service; do
    grep -q "${unit}" /usr/share/polkit-1/rules.d/61-cabinetos-update.rules || {
        log "  MISSING: 61-cabinetos-update.rules no longer names ${unit}"
        failed=1
    }
done

# THE CONSOLE ITSELF IS CHECKED IN install-frontend.sh's LAST CALL, not here.
# The frontend, the cores and their files are installed in later layers, after
# this script has finished, so this block cannot see them. The list moved
# there whole: the frontend, the cores, PPSSPP's files, and PlayStation 2's
# emulator, its two libraries and its resources, each of which fails quietly in
# its own way when absent.

# The session must actually RUN the frontend. It ran `sleep infinity` until
# 2026-09-19, which is a session that starts, takes the display, and draws
# nothing — indistinguishable on a television from a machine that failed to
# boot.
if grep -q 'CABINETOS_APP:-/usr/bin/cabinetos-frontend' /usr/bin/cabinetos-session; then
    log "  ok: the session runs the frontend"
else
    log "  MISSING: cabinetos-session does not default to the frontend"
    failed=1
fi

# Where the console keeps games and saves. Created at every boot rather than
# built into the image, because /var in a bootc image is unpacked from the
# FIRST image only and never updated — see the file itself.
check_present "the storage root's tmpfiles rule" /usr/lib/tmpfiles.d/cabinetos.conf || failed=1

# THE STEAM ENTRY (#223). Steam starts only from its tile: never at boot, never
# from an autostart entry, never through cardwire. The slice helper's tools
# are all in the base and none is something we install, so a strip pass that
# took one away would ship a tile that cannot set Steam up.
check_present "Steam's bootstrapper" /usr/bin/steam || failed=1
check_present "Steam's session" /usr/share/gamescope-session-plus/gamescope-session-plus || failed=1
for autostart in /etc/xdg/autostart/steam.desktop /etc/skel/.config/autostart/steam.desktop; do
    if [[ -e "${autostart}" ]]; then
        log "  WRONG: ${autostart} exists; Steam would start at login"
        failed=1
    fi
done
if [[ -e /usr/bin/cardwire ]]; then
    log "  WRONG: cardwire is in the image; Steam's session would launch through it and fail"
    failed=1
fi
for needed in fallocate chattr mkfs.ext4 e2fsck resize2fs losetup mountpoint setpriv; do
    if command -v "${needed}" >/dev/null 2>&1; then
        log "  ok: ${needed} ($(command -v "${needed}"))"
    else
        log "  MISSING: ${needed}, which the Steam entry runs (cabinetos-steam, and setpriv around Steam)"
        failed=1
    fi
done
grep -q 'cabinetos-steam@' /usr/share/polkit-1/rules.d/66-cabinetos-steam.rules || {
    log "  MISSING: 66-cabinetos-steam.rules no longer names cabinetos-steam@"
    failed=1
}

# EXTRA DRIVES (#236): the console is the only thing that mounts one, ext4
# drives made elsewhere get their folder from cabinetos-drive-claim, and
# Steam's libraries on them are listed by a Python script.
for gone in /usr/lib/udev/rules.d/99-steamos-automount.rules \
            /usr/lib/udev/rules.d/99-framework-steam-automount.rules; do
    if [[ -e "${gone}" ]]; then
        log "  WRONG: ${gone} is in the image; it would mount drives behind the console"
        failed=1
    fi
done
if [[ -e /usr/lib/systemd/system/ublue-os-media-automount.service ]] && \
   [[ "$(systemctl is-enabled ublue-os-media-automount.service 2>/dev/null)" != masked ]]; then
    log "  WRONG: ublue-os-media-automount.service is not masked; it would take internal ext4 drives"
    failed=1
fi
for needed in python3 findmnt install; do
    if ! command -v "${needed}" >/dev/null 2>&1; then
        log "  MISSING: ${needed}, which the extra drive helpers run"
        failed=1
    fi
done
# THE DIAGNOSTIC REPORT (#195) runs these. It survives any one being gone
# (it says so in the report), but a report without the log or the hardware
# list cannot find a fault, so a strip pass that removed one fails here.
for needed in journalctl lspci lsusb lsblk lscpu nmcli bluetoothctl edid-decode vulkaninfo du; do
    if ! command -v "${needed}" >/dev/null 2>&1; then
        log "  MISSING: ${needed}, which the diagnostic report runs"
        failed=1
    fi
done
grep -q 'cabinetos-drive-claim.service' /usr/share/polkit-1/rules.d/63-cabinetos-drives.rules || {
    log "  MISSING: 63-cabinetos-drives.rules no longer names cabinetos-drive-claim.service"
    failed=1
}

# The default target must be multi-user. The session is pulled in by it; a
# graphical.target default would try to start a desktop.
if [[ "$(readlink -f /usr/lib/systemd/system/default.target)" == *multi-user.target ]]; then
    log "  ok: default target is multi-user.target"
else
    log "  MISSING: default target is not multi-user.target"
    failed=1
fi

group_end

if [[ "${failed}" -ne 0 ]]; then
    log "sanity checks FAILED"
    exit 1
fi

log "CabinetOS build complete"
