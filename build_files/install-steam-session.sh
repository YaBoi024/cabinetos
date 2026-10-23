#!/usr/bin/env bash
#
# Steam's own Big Picture session, for the Steam entry. Issue #223.
#
# Picking the Steam tile hands the screen over to this: gamescope in Steam
# mode with Steam's gamepad UI, exactly as Bazzite's deck images run it, and
# back to the console when Steam closes (cabinetos-session, steam_step). It is
# two small upstream packages, about 46 KB of scripts:
#
#   gamescope-session         the harness: starts gamescope in Steam mode,
#                             waits for it, runs the client, tears down,
#                             honours Steam's reboot and shutdown requests
#   gamescope-session-steam   the Steam half: its environment, the client
#                             command, and `steamos-session-select`, which is
#                             what Steam's "Switch to Desktop" runs
#
# Both are bazzite-deck packages, so they are not in our base; they come from
# terra, whose repository file the base already carries, and float with terra
# at build time rather than with the base pin. CabinetOS changes nothing in them.
#
# WITHOUT WEAK DEPENDENCIES, on purpose: gamescope-session recommends
# "cardwire or switcheroo-control", and cardwire is exactly what strip-steam.sh
# removed so that the session launches Steam directly (audit, #223, question
# 2). The build asserts it stayed gone.
#
# NOT installed: gamescope-session-ogui-steam (OpenGamepadUI on top of Steam),
# steamos-manager, inputplumber, steam-notif-daemon, the deck's bootstrap
# tarball. The entry is Steam and nothing else.

source /ctx/lib.sh

group_start "Installing Steam's gamescope session (#223)"

# terra is in the base DISABLED (enabled=0; Bazzite turns it off at the end of
# its own build), so it is enabled for this one install and stays off after.
# Found by the dry run on the A9, 2026-10-03: "No match for argument".
#
# FOUR TRIES, A MINUTE APART. Terra's mirror now and then serves a package
# that does not match its own metadata while it is publishing ("Downloading
# successful, but checksum doesn't match"). It failed the image build that way
# twice by 2026-10-04 and a re-run passed both times, so a build that fails
# for it has nothing wrong with it. Each retry drops the downloaded packages
# and refetches the metadata, so it asks the mirror afresh.
refresh=()
for attempt in 1 2 3 4; do
    if dnf5 -y install --enablerepo=terra --setopt=install_weak_deps=False \
        "${refresh[@]}" \
        gamescope-session \
        gamescope-session-steam
    then
        break
    fi
    log "  install attempt ${attempt} failed"
    if [[ ${attempt} -eq 4 ]]; then
        # THE STORED COPY (#270), when terra will not answer properly. On
        # 2026-10-05 its index stayed broken for over an hour (a 404, then a
        # checksum list that never caught up), and three builds in a row
        # failed for it. build_files/terra-fallback/ holds the two packages,
        # refreshed weekly by terra-fallback-update.yml. Checked against
        # terra's own key first: a copy that does not verify is not used.
        log "  terra failed four times; installing the stored copy (#270)"
        keys=$(mktemp -d)
        rpmkeys --dbpath "${keys}" --import /etc/pki/rpm-gpg/RPM-GPG-KEY-terra44
        rpmkeys --dbpath "${keys}" -K /ctx/terra-fallback/*.rpm || {
            log "ERROR: the stored copy does not verify against terra's key"; exit 1; }
        rm -rf "${keys}"
        dnf5 -y install --setopt=install_weak_deps=False /ctx/terra-fallback/*.rpm || {
            log "ERROR: could not install Steam's session, from terra or the stored copy"; exit 1; }
        break
    fi
    dnf5 clean packages --enablerepo=terra || true
    refresh=(--refresh)
    sleep 60
done

# Asserted, file by file: each is what the entry actually runs, and a rename
# upstream would otherwise ship a tile that does nothing.
for expected in \
    /usr/bin/steam \
    /usr/share/gamescope-session-plus/gamescope-session-plus \
    /usr/share/gamescope-session-plus/sessions.d/steam \
    /usr/bin/steamos-session-select
do
    if [[ -x "${expected}" || -f "${expected}" ]]; then
        log "  present: ${expected} ($(rpm -qf "${expected}" 2>/dev/null || echo 'unowned'))"
    else
        log "  ERROR: ${expected} is missing; the Steam entry runs it"
        exit 1
    fi
done

# The two lines of upstream's script the entry depends on, so a change there
# fails the build instead of the TV: it launches the client through cardwire
# when cardwire exists, and runs it directly otherwise.
grep -q 'command -v /usr/bin/cardwire' /usr/share/gamescope-session-plus/gamescope-session-plus || {
    log "  ERROR: gamescope-session-plus no longer launches the client the way #223 expects; read it"
    exit 1
}
grep -q '^export CLIENTCMD="steam ' /usr/share/gamescope-session-plus/sessions.d/steam || {
    log "  ERROR: sessions.d/steam no longer sets CLIENTCMD to steam; read it"
    exit 1
}

# Steam's "Switch to Desktop" ends in os-session-select when it exists, which
# is ours (system_files, `steam -shutdown`). Checked here because upstream
# names the paths it looks in.
grep -q '/usr/libexec/os-session-select' /usr/bin/steamos-session-select || {
    log "  ERROR: steamos-session-select no longer looks for /usr/libexec/os-session-select"
    exit 1
}

for gone in cardwire cardwire-gui gamescope-session-ogui-steam; do
    if is_installed "${gone}"; then
        log "  ERROR: ${gone} is installed; the Steam session must not have it"
        exit 1
    fi
done
[[ -e /usr/bin/switcherooctl ]] && {
    log "  ERROR: /usr/bin/switcherooctl is back; the session would launch Steam through it"
    exit 1
}

# A display manager session file nobody reads (there is no display manager).
# Left alone: it is upstream's, and harmless.

group_end
