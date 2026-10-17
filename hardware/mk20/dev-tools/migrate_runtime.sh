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
plan)
    available=$(df -Pk "$root" | awk 'NR == 2 { print $4 }')
    case "$available" in ''|*[!0-9]*) exit 1 ;; esac
    printf 'SPACE %s\n' "$available"
    i=0
    for name in mk20-hud mk20-audio lunch.sh fonts/D2Coding.ttf mk20-update; do
        if test -e "$root/$name"; then
            test -f "$root/$name"
            bytes=$(wc -c < "$root/$name")
            digest=$(sha256sum "$root/$name"); digest=${digest%% *}
            printf 'FILE %s %s %s\n' "$i" "$bytes" "$digest"
        fi
        i=$((i + 1))
    done
    ;;
keep)
    test -d "$stage"
    case "${2:-}" in
        0) name=mk20-hud ;; 1) name=mk20-audio ;; 2) name=lunch.sh ;;
        3) name=fonts/D2Coding.ttf ;; 4) name=mk20-update ;; *) exit 1 ;;
    esac
    expected=${3:-}
    test "${#expected}" -eq 64
    case "$expected" in *[!0-9a-f]*) exit 1 ;; esac
    test ! -e "$stage/new$2"
    test -f "$root/$name"
    actual=$(sha256sum "$root/$name"); actual=${actual%% *}
    test "$actual" = "$expected"
    # copyN is already an allowlisted native-recovery cleanup path. A kept
    # file has no oldN: its original-presence journal bit leaves it in place.
    (set -C; printf '%s\n' "$expected" > "$stage/copy$2")
    sync
    ;;
commit)
    test -d "$stage"
    test ! -e "$stage/journal"
    mask=0; i=0
    for name in mk20-hud mk20-audio lunch.sh fonts/D2Coding.ttf mk20-update; do
        if test -f "$stage/copy$i"; then
            test ! -e "$stage/new$i"
            expected=$(cat "$stage/copy$i")
            test "${#expected}" -eq 64
            case "$expected" in *[!0-9a-f]*) exit 1 ;; esac
            test -f "$root/$name"
            actual=$(sha256sum "$root/$name"); actual=${actual%% *}
            test "$actual" = "$expected"
        else
            test -f "$stage/new$i"
        fi
        if test -e "$root/$name"; then test -f "$root/$name"; mask=$((mask | (1 << i))); fi
        i=$((i + 1))
    done
    if test -e "$root/fonts"; then test -d "$root/fonts"; else mkdir "$root/fonts"; fi
    # Retain changed originals before publishing the journal; kept files remain
    # at their original names. Copying retains boot
    # entry names throughout replacement, including on FAT without hard links.
    i=0
    for name in mk20-hud mk20-audio lunch.sh fonts/D2Coding.ttf mk20-update; do
        if test ! -f "$stage/copy$i" && test -f "$root/$name"; then cp "$root/$name" "$stage/old$i"; fi
        i=$((i + 1))
    done
    sync
    printf '%s\n' "$mask" > "$stage/journal"
    sync
    # Install recovery first, then its boot hook, before replacing HUD/audio.
    for entry in '4:mk20-update' '2:lunch.sh' '0:mk20-hud' '1:mk20-audio' '3:fonts/D2Coding.ttf'; do
        i=${entry%%:*}; name=${entry#*:}
        if test ! -f "$stage/copy$i"; then
            mv "$stage/new$i" "$root/$name"
            chmod 755 "$root/$name"
        fi
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
        for i in 0 1 2 3 4; do rm -f "$stage/new$i" "$stage/old$i" "$stage/copy$i"; done
        rmdir "$stage"
    fi
    ;;
*) exit 2 ;;
esac
