#!/bin/sh
# Developer opt-in only. Not part of the production runtime deploy manifest.
# Invoked explicitly from the owner's preserved dev-access.conf.
umask 077
CONFIG=/mnt/SDCARD/dev-access.conf
LOG=/tmp/snowball-dev-adb.log
LOCK=/tmp/snowball-dev-adb.lock
POLICY=/mnt/SDCARD/snowball-dev-adb.policy

configured_mac() {
    # Read only the MAC assignment; never execute a credential/config file.
    awk -F= '$1=="DEV_PC_MAC" {value=$2; gsub(/[\042\047\r]/,"",value); print value}' "$1"
}
valid_mac() {
    case "$1" in *[!0123456789abcdefABCDEF:]*) return 1 ;; esac
    printf '%s\n' "$1" | grep -Eq '^([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$'
}
log() { printf '[%s] %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*" >> "$LOG"; }

private_subnet() {
    case "$1" in *[!0123456789./]*|'') return 1 ;; esac
    printf '%s\n' "$1" | grep -Eq '^([0-9]{1,3}\.){3}[0-9]{1,3}/[0-9]{1,2}$' || return 1
    printf '%s\n' "$1" | awk -F'[./]' 'NF!=5 {exit 1} {for(i=1;i<=4;i++)if($i<0||$i>255)exit 1; if($5<8||$5>30)exit 1; if($1==10||($1==172&&$2>=16&&$2<=31)||($1==192&&$2==168))exit 0; exit 1}'
}
if [ "$1" = --check-subnet ]; then private_subnet "$2"; exit $?; fi
if [ "$1" = --check-config ]; then
    mac=$(configured_mac "$2")
    valid_mac "$mac" || exit 1
    printf '%s\n' "$mac"
    exit 0
fi
[ "$1" = --watch ] || { echo 'Usage: snowball-dev-adb.sh --watch | --check-config FILE' >&2; exit 2; }
# A separate opt-in guard outlives a managed HUD/audio restart. It never stops
# adbd, tears down USB, replaces product rules, or opens ADB beyond the explicitly approved local subnet.
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
trap '' HUP
last=
while :; do
    # Retry the installed product initializer when boot-time dependencies were
    # late. Reuse its implementation; it refuses to replace an active gadget.
    if [ ! -c /dev/ttyGS0 ] && [ -r /mnt/SDCARD/lunch.sh ]; then
        sed -n '/^start_product_usb() (/,/^)/p' /mnt/SDCARD/lunch.sh > /tmp/snowball-dev-usb-function.sh
        if [ -s /tmp/snowball-dev-usb-function.sh ]; then
            ( . /tmp/snowball-dev-usb-function.sh; start_product_usb ) >> /tmp/snowball-dev-usb.log 2>&1
        fi
    fi
    if [ -c /dev/ttyGS0 ] && [ -x /tmp/mk20-update ] && ! pidof mk20-update >/dev/null; then
        /tmp/mk20-update </dev/null >> /tmp/mk20-update.log 2>&1 &
    fi
    mac=$(configured_mac "$CONFIG")
    policy=$(tr -d '\r\n' < "$POLICY" 2>/dev/null)
    subnet=
    if [ "$policy" = trusted-lan ]; then
        subnet=$(ip -4 route show dev wlan0 scope link | awk '$1 ~ /\// {print $1; exit}')
        private_subnet "$subnet" || subnet=
    fi
    scope="mac:$mac"
    [ -z "$subnet" ] || scope="lan:$subnet"
    if valid_mac "$mac" || [ -n "$subnet" ]; then
        iptables -N SNOWBALL_DEV_ADB 2>/dev/null || true
        if [ "$last" != "$scope" ]; then
            if [ -n "$subnet" ]; then
                iptables -I SNOWBALL_DEV_ADB 1 -i wlan0 -s "$subnet" -j ACCEPT || { sleep 5; continue; }
            else
                iptables -I SNOWBALL_DEV_ADB 1 -i wlan0 -m mac --mac-source "$mac" -j ACCEPT || { sleep 5; continue; }
            fi
            # Remove stale grants only after the new grant exists.
            while [ "$(iptables -S SNOWBALL_DEV_ADB | grep -c '^-A ')" -gt 1 ]; do iptables -D SNOWBALL_DEV_ADB 2 || break; done
            last=$scope
            log "Development access policy maintained: $scope"
        fi
        first=$(iptables -S INPUT | sed -n '/^-A INPUT /{p;q;}')
        if [ "$first" != '-A INPUT -p tcp -m tcp --dport 5555 -j SNOWBALL_DEV_ADB' ]; then
            while iptables -D INPUT -p tcp --dport 5555 -j SNOWBALL_DEV_ADB 2>/dev/null; do :; done
            iptables -I INPUT 1 -p tcp --dport 5555 -j SNOWBALL_DEV_ADB || { sleep 5; continue; }
        fi
        if ! pidof adbd >/dev/null; then
            ADB_TRANSPORT_PORT=5555 /bin/adbd -D >> /tmp/snowball-dev-adbd.log 2>&1 < /dev/null &
            log 'Started missing development ADB after confirming access rule'
        fi
    elif [ "$last" != invalid ]; then
        log 'Invalid or missing development MAC; existing daemon and rules retained'
        last=invalid
    fi
    sleep 5
done
