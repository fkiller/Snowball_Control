#!/bin/sh
# Developer opt-in only. Not part of the production runtime deploy manifest.
# Invoked explicitly from the owner's preserved dev-access.conf.
CONFIG=/mnt/SDCARD/dev-access.conf
LOG=/tmp/snowball-dev-adb.log
LOCK=/tmp/snowball-dev-adb.lock

configured_mac() {
    # Read only the MAC assignment; never execute a credential/config file.
    awk -F= '$1=="DEV_PC_MAC" {value=$2; gsub(/[\042\047\r]/,"",value); print value}' "$1"
}
valid_mac() {
    case "$1" in *[!0123456789abcdefABCDEF:]*) return 1 ;; esac
    printf '%s\n' "$1" | grep -Eq '^([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$'
}
log() { printf '[%s] %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*" >> "$LOG"; }

if [ "$1" = --check-config ]; then
    mac=$(configured_mac "$2")
    valid_mac "$mac" || exit 1
    printf '%s\n' "$mac"
    exit 0
fi
[ "$1" = --watch ] || { echo 'Usage: snowball-dev-adb.sh --watch | --check-config FILE' >&2; exit 2; }
# A separate opt-in guard outlives a managed HUD/audio restart. It never stops
# adbd, tears down USB, replaces product rules, or opens ADB to a whole LAN.
if ! mkdir "$LOCK" 2>/dev/null; then
    owner=$(cat "$LOCK/pid" 2>/dev/null)
    case "$owner" in ''|*[!0-9]*) exit 1 ;; esac
    kill -0 "$owner" 2>/dev/null && exit 0
    rm -f "$LOCK/pid"; rmdir "$LOCK" 2>/dev/null || exit 1
    mkdir "$LOCK" || exit 1
fi
echo $$ > "$LOCK/pid"
trap 'rm -f "$LOCK/pid"; rmdir "$LOCK" 2>/dev/null' EXIT
trap 'exit 0' INT TERM
last=
while :; do
    mac=$(configured_mac "$CONFIG")
    if valid_mac "$mac"; then
        # An explicit exact-MAC rule precedes stale managed MAC chains.
        # With no valid configuration the guard changes nothing.
        if iptables -C INPUT -i wlan0 -p tcp --dport 5555 -m mac --mac-source "$mac" -j ACCEPT 2>/dev/null ||
           iptables -I INPUT 1 -i wlan0 -p tcp --dport 5555 -m mac --mac-source "$mac" -j ACCEPT; then
            if ! pidof adbd >/dev/null; then
                ADB_TRANSPORT_PORT=5555 /bin/adbd -D >> /tmp/snowball-dev-adbd.log 2>&1 < /dev/null &
                log 'Started development ADB after confirming exact-MAC access rule'
            fi
            if [ "$last" != "$mac" ]; then log "Exact-MAC development access maintained for $mac"; last=$mac; fi
        else
            log 'Could not maintain development access; existing daemon and rules retained'
        fi
    elif [ "$last" != invalid ]; then
        log 'Invalid or missing development MAC; existing daemon and rules retained'
        last=invalid
    fi
    sleep 5
done
