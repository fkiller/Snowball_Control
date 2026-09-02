# MK20 development access

## Working topology

- Product USB protocol: `VID 1D6B:PID 0104`, currently `COM6`
- Linux development shell: TCP ADB at `192.168.69.27:5555`
- Device identity: `20080411`
- Linux: Tina Linux 4.0.0, kernel 5.4.61, ARMv7
- Wi-Fi development network: `YOUR_WIFI_SSID`

The product application (`/data/KeyboardDevice`) and the TCP ADB daemon run at
the same time. The USB gadget remains in product serial mode; TCP ADB does not
reconfigure USB.

Copy `dev-access.conf.example` to `dev-access.conf` and set the 2.4 GHz Wi-Fi
credentials plus the development PC's Wi-Fi MAC address. The real configuration
is intentionally ignored by Git. Deploy both `lunch.sh` and
`dev-access.conf` to the root of the MK20 microSD card.

## Connect from Windows

Connect the PC to the same 2.4 GHz network as the MK20, then use Google's
platform tools:

```powershell
netsh wlan connect name="YOUR_WIFI_SSID" interface="Wi-Fi"
$adb = "$env:LOCALAPPDATA\Temp\Codex-MK20-ADB\platform-tools\adb.exe"
& $adb connect 192.168.69.27:5555
& $adb -s 192.168.69.27:5555 shell
```

Expected shell identity:

```text
uid=0(root) gid=0(root)
```

## `mk20ctl`

The host helper combines product COM discovery and network ADB operations:

```powershell
./mk20ctl.ps1 doctor
./mk20ctl.ps1 info
./mk20ctl.ps1 put -Source ./file.bin -Destination /mnt/SDCARD/file.bin
./mk20ctl.ps1 put -Transport Com -Source ./recovery.sh -Destination /mnt/SDCARD/lunch.sh
./mk20ctl.ps1 shell
./mk20ctl.ps1 restore            # dry run
./mk20ctl.ps1 restore -Force     # destructive factory rollback
```

Set `MK20_ADB` when `adb.exe` is not installed at the default development
location. Pass `-Device address:port` if DHCP changes the MK20 endpoint.

## Security

This Tina image does not include the ADB authentication service. Device
firewall rules therefore allow port 5555 only from the development PC's Wi-Fi
MAC address and drop other connections to that port. `lunch.sh` connects the
device Wi-Fi, obtains its DHCP lease, launches TCP-only ADB, and reapplies the
rules at boot without changing the USB gadget.

If the development PC's Wi-Fi adapter changes, update `DEV_PC_MAC` in
`lunch.sh` before replacing the device copy at `/mnt/SDCARD/lunch.sh`.

## Device-side files

- `/mnt/SDCARD/lunch.sh`: Wi-Fi + TCP-only ADB + firewall boot hook
- `/mnt/SDCARD/dev-access.conf`: ignored site credentials and allowed PC MAC
- `/mnt/SDCARD/dev-access.log`: bootstrap log
- `/mnt/SDCARD/adbd-configfs.init.factory`: factory copy of the ADB init script
- `/etc/init.d/adbd`: development copy with `ADB_TRANSPORT_PORT=5555`

## Restore factory behavior

Run these commands through the network ADB shell:

```sh
cp /mnt/SDCARD/adbd-configfs.init.factory /etc/init.d/adbd
chmod 755 /etc/init.d/adbd
rm -f /mnt/SDCARD/lunch.sh
/data/setusbconfig serial
sync
reboot
```

This removes TCP ADB persistence and returns USB ownership to the product
serial protocol after reboot.

## Source evidence

- `package/utils/adb/adbd-configfs.init` starts ADB and supports
  `ADB_TRANSPORT_PORT`.
- `package/utils/adb/src/adb.c` enables TCP transport from that environment
  variable.
- `package/PCMonitorApp/setusbconfig` owns the product serial gadget mode.
- `package/PCMonitorApp/qt_app1` executes `/mnt/SDCARD/lunch.sh` as root.
