#!/bin/sh
# Trusted maintenance only. No renderer-supplied path, shell or network settings.
set -eu
root=/mnt/SDCARD
stage=$root/.snowball-update
case "${1:-}" in
freeze)
    grep -q '^/dev/mmcblk0p1 /mnt/SDCARD vfat ' /proc/mounts
    test ! -e "$stage"
    for name in mk20-hud mk20-audio; do
        for p in $(pidof "$name" 2>/dev/null || true); do
            case "$(readlink /proc/$p/exe)" in "$root/$name") kill "$p" ;; esac
        done
    done
    sleep 1
    for name in mk20-hud mk20-audio; do
        for p in $(pidof "$name" 2>/dev/null || true); do
            case "$(readlink /proc/$p/exe)" in "$root/$name") exit 1 ;; esac
        done
    done
    sync
    mount -o remount,ro "$root"
    ;;
resume)
    mount -o remount,rw "$root"
    # Old boot scripts ignore command-line options. Never execute one as a stop
    # or resume helper: that could reconfigure Wi-Fi or launch a second bootstrap.
    for name in mk20-audio mk20-hud; do
        if ! pidof "$name" >/dev/null 2>&1; then
            "$root/$name" -d >> "/tmp/$name.log" 2>&1 < /dev/null
        fi
    done
    pidof mk20-hud >/dev/null && pidof mk20-audio >/dev/null
    ;;
begin)
    mount -o remount,rw "$root"
    test ! -e "$stage"
    mkdir "$stage"
    ;;
commit)
    test -d "$stage"
    test ! -e "$stage/journal"
    mask=0; i=0
    for name in mk20-hud mk20-audio lunch.sh fonts/D2Coding.ttf mk20-update; do
        test -f "$stage/new$i"
        if test -e "$root/$name"; then test -f "$root/$name"; mask=$((mask | (1 << i))); fi
        i=$((i + 1))
    done
    if test -e "$root/fonts"; then test -d "$root/fonts"; else mkdir "$root/fonts"; fi
    # Retain every old file before publishing the journal. Copying retains boot
    # entry names throughout replacement, including on FAT without hard links.
    i=0
    for name in mk20-hud mk20-audio lunch.sh fonts/D2Coding.ttf mk20-update; do
        if test -f "$root/$name"; then cp "$root/$name" "$stage/old$i"; fi
        i=$((i + 1))
    done
    sync
    printf '%s\n' "$mask" > "$stage/journal"
    sync
    # Install recovery first, then its boot hook, before replacing HUD/audio.
    for entry in '4:mk20-update' '2:lunch.sh' '0:mk20-hud' '1:mk20-audio' '3:fonts/D2Coding.ttf'; do
        i=${entry%%:*}; name=${entry#*:}
        mv "$stage/new$i" "$root/$name"
        chmod 755 "$root/$name"
        sync
    done
    ;;
finalize)
    test -f "$stage/journal"
    : > "$stage/committed"
    sync
    /tmp/snowball-migration-recover --recover
    ;;
rollback)
    mount -o remount,rw "$root"
    if test -f "$stage/journal"; then
        /tmp/snowball-migration-recover --recover
    elif test -d "$stage"; then
        for i in 0 1 2 3 4; do rm -f "$stage/new$i" "$stage/old$i"; done
        rmdir "$stage"
    fi
    ;;
*) exit 2 ;;
esac
