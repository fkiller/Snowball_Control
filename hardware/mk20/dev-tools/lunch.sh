#!/bin/sh

# Bring up the development Wi-Fi, start TCP-only ADB after the product
# application is ready, then restrict it to the development PC. This script
# never changes the USB gadget. Site-specific credentials live in the ignored
# /mnt/SDCARD/dev-access.conf file.
CONFIG="/mnt/SDCARD/dev-access.conf"
LOG="/mnt/SDCARD/dev-access.log"
WPA_CONF="/etc/wpa_supplicant.conf"

if [ ! -r "$CONFIG" ]; then
    echo "Missing $CONFIG" >> "$LOG"
    exit 1
fi

. "$CONFIG"

if [ -z "$WIFI_SSID" ] || [ -z "$WIFI_PSK" ] || [ -z "$DEV_PC_MAC" ]; then
    echo "Incomplete $CONFIG" >> "$LOG"
    exit 1
fi

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
            fi

            if ! ip addr show wlan0 2>/dev/null | grep -q "inet " && \
               ! pidof udhcpc >/dev/null 2>&1; then
                udhcpc -i wlan0 -R -b >> "$LOG" 2>&1
            fi

            if pidof KeyboardDevice >/dev/null 2>&1 && \
               ip addr show wlan0 2>/dev/null | grep -q "inet "; then
                break
            fi
        fi
        sleep 2
        elapsed=$((elapsed + 2))
    done

    killall adbd 2>/dev/null
    sleep 2
    ADB_TRANSPORT_PORT=5555 /bin/adbd -D >/dev/null 2>&1 &
    sleep 3

    iptables -C INPUT -p tcp --dport 5555 -m mac --mac-source "$DEV_PC_MAC" -j ACCEPT 2>/dev/null || \
        iptables -I INPUT 1 -p tcp --dport 5555 -m mac --mac-source "$DEV_PC_MAC" -j ACCEPT

    iptables -C INPUT -p tcp --dport 5555 -j DROP 2>/dev/null || \
        iptables -A INPUT -p tcp --dport 5555 -j DROP

    echo "TCP ADB ready after ${elapsed}s $(date)" >> "$LOG"
) &

exit 0
