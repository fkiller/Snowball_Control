#!/bin/sh

# Bring up the development Wi-Fi, start TCP-only ADB after the product
# application is ready, then restrict it to the development PC. This script
# never changes the USB gadget.
CONFIG="/mnt/SDCARD/dev-access.conf"
LOG="/mnt/SDCARD/dev-access.log"
IP_FILE="/mnt/SDCARD/current_ip.txt"
WPA_CONF="/etc/wpa_supplicant.conf"

# Default fallback credentials if config is missing
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
    sleep 2
    ADB_TRANSPORT_PORT=5555 /bin/adbd -D >/dev/null 2>&1 &
    sleep 2

    iptables -C INPUT -p tcp --dport 5555 -m mac --mac-source "$DEV_PC_MAC" -j ACCEPT 2>/dev/null || \
        iptables -I INPUT 1 -p tcp --dport 5555 -m mac --mac-source "$DEV_PC_MAC" -j ACCEPT

    iptables -C INPUT -p tcp --dport 5555 -j DROP 2>/dev/null || \
        iptables -A INPUT -p tcp --dport 5555 -j DROP

    log_msg "TCP ADB daemon running (pid $(pidof adbd)) on port 5555, MAC restricted to $DEV_PC_MAC"
    sync
) &

exit 0
