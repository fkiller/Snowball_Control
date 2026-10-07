#!/bin/sh

# Only stop processes executing our managed runtime, including replaced binaries.
stop_native() {
    path="/mnt/SDCARD/$1"
    for runtime_pid in $(pidof "$1" 2>/dev/null); do
        case "$(readlink /proc/$runtime_pid/exe 2>/dev/null)" in
            "$path"|"$path (deleted)")
                kill "$runtime_pid" 2>/dev/null
                sleep 1
                case "$(readlink /proc/$runtime_pid/exe 2>/dev/null)" in
                    "$path"|"$path (deleted)") kill -9 "$runtime_pid" 2>/dev/null ;;
                esac ;;
        esac
    done
}
start_runtime() {
    /etc/init.d/qt_app2 disable 2>/dev/null
    /etc/init.d/qt_app2 stop 2>/dev/null
    killall KeyboardDevice 2>/dev/null
    stop_native mk20-hud
    stop_native mk20-audio
    [ -x /mnt/SDCARD/mk20-audio ] && [ -x /mnt/SDCARD/mk20-hud ] || return 1
    /mnt/SDCARD/mk20-audio -d >> /tmp/mk20-audio.log 2>&1 < /dev/null
    /mnt/SDCARD/mk20-hud -d >> /tmp/mk20-hud.log 2>&1 < /dev/null
    sleep 1
    pidof mk20-audio >/dev/null && pidof mk20-hud >/dev/null
}
if [ "$1" = "--restart-runtime" ]; then
    start_runtime
    exit $?
fi

# Bring up development Wi-Fi and restrict TCP-only ADB before starting it.
# This is a trusted-private-LAN Preview, without cryptographic ADB authentication.
# This script
# never changes the USB gadget.
CONFIG="/mnt/SDCARD/dev-access.conf"
LOG="/mnt/SDCARD/dev-access.log"
IP_FILE="/mnt/SDCARD/current_ip.txt"
WPA_CONF="/etc/wpa_supplicant.conf"

# Default fallback credentials if config is missing (override via /mnt/SDCARD/dev-access.conf)
WIFI_SSID="YOUR_WIFI_SSID"
WIFI_PSK="YOUR_WIFI_PSK"
DEV_PC_MAC="AA:BB:CC:DD:EE:FF"

# Load config if present
if [ -r "$CONFIG" ]; then
    . "$CONFIG"
fi

log_msg() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $1" >> "$LOG"
}

log_msg "lunch.sh started"

(
    elapsed=0
    while [ "$elapsed" -lt 120 ]; do
        if ip link show wlan0 >/dev/null 2>&1; then
            ifconfig wlan0 up 2>/dev/null

            if ! pidof wpa_supplicant >/dev/null 2>&1; then
                umask 077
                cat > "$WPA_CONF" <<EOF
ctrl_interface=/var/run/wpa_supplicant
update_config=1
network={
    ssid="$WIFI_SSID"
    psk="$WIFI_PSK"
    key_mgmt=WPA-PSK
}
EOF
                wpa_supplicant -Dnl80211 -i wlan0 -c "$WPA_CONF" -B >> "$LOG" 2>&1
                log_msg "wpa_supplicant launched"
            fi

            if ! ip addr show wlan0 2>/dev/null | grep -q "inet " && \
               ! pidof udhcpc >/dev/null 2>&1; then
                udhcpc -i wlan0 -R -b >> "$LOG" 2>&1
                log_msg "udhcpc launched"
            fi

            if ip addr show wlan0 2>/dev/null | grep -q "inet "; then
                if pidof KeyboardDevice >/dev/null 2>&1 || [ "$elapsed" -ge 16 ]; then
                    break
                fi
            fi
        fi
        sleep 2
        elapsed=$((elapsed + 2))
    done

    CURRENT_IP=$(ip -4 addr show wlan0 2>/dev/null | grep -o 'inet [0-9.]*' | awk '{print $2}')
    if [ -n "$CURRENT_IP" ]; then
        echo "$CURRENT_IP" > "$IP_FILE"
        log_msg "wlan0 IP acquired: $CURRENT_IP (after ${elapsed}s)"
    else
        log_msg "Warning: wlan0 did not acquire IP after ${elapsed}s"
    fi

    killall adbd 2>/dev/null
    while iptables -D INPUT -p tcp --dport 5555 -j SNOWBALL_ADB 2>/dev/null; do :; done
    iptables -N SNOWBALL_ADB 2>/dev/null || true
    if iptables -F SNOWBALL_ADB && \
       iptables -A SNOWBALL_ADB -m mac --mac-source "$DEV_PC_MAC" -j ACCEPT && \
       iptables -A SNOWBALL_ADB -j DROP && \
       iptables -I INPUT 1 -p tcp --dport 5555 -j SNOWBALL_ADB; then
        ADB_TRANSPORT_PORT=5555 /bin/adbd -D >/dev/null 2>&1 &
        log_msg "TCP ADB requested after development MAC filter installation"
    else
        log_msg "ADB disabled: development firewall could not be installed"
    fi

    if start_runtime; then
        log_msg "Native HUD and LAN audio services running"
    else
        log_msg "Native runtime failed to start; inspect /tmp/mk20-audio.log"
    fi

    sync
) &

exit 0
