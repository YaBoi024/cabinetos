#!/usr/bin/bash
#
# "Starting the CabinetOS installer" with dots that keep moving, on the
# screen while the installer loads, so the time between the boot menu and
# the drive question is never a still black screen that looks frozen.
#
# Run by installer-wait.service twice. In the installer's initrd, until the
# switch to the installer's own root: ci/brand-installer-iso.sh puts both in
# a small initrd of their own that the boot menu loads after the builder's,
# which stays as it is. Then in the installer's root, from the product
# image, until Anaconda starts its console (tmux's socket appears), which
# takes the screen a moment later. Written like the drive question
# (disk_config/iso.toml, %pre): the same place, double size on a screen 1600
# pixels wide or more. The initrd has no `head` or `printf` program, so only
# bash's own commands, `cat`, `setfont` and `sleep` are used.
#
# EVERY FRAME OPENS THE CONSOLE AGAIN AND PUTS THE LINE IN ITS PLACE. The
# console's setup runs a second time just after this starts, when the
# kernel's console moves to the graphics driver; that cuts off a console
# opened before it, so a line written once stopped moving, and it puts the
# normal font back, so the double size is put back every two seconds.

tty=/dev/tty1

width=0
for c in /sys/class/drm/card*-*; do
  [ "$(cat "$c/status" 2>/dev/null)" = connected ] || continue
  read -r mode < "$c/modes" 2>/dev/null || continue
  w=${mode%%x*}
  [ "${w:-0}" -gt "$width" ] && width=$w
done

dots=("" "." ".." "...")
printf '\033[2J' > "$tty" 2>/dev/null
for ((n = 0; n < 1200; n++)); do
  [ -S /tmp/tmux-0/default ] && exit 0
  if [ "$width" -ge 1600 ] && ((n % 4 == 0)); then
    setfont -d -C "$tty" /usr/lib/kbd/consolefonts/Lat2-Terminus16.psfu.gz 2>/dev/null
  fi
  printf '\033[?25l\033[3;3HStarting the CabinetOS installer%-3s' "${dots[n % 4]}" > "$tty" 2>/dev/null
  sleep 0.5
done
