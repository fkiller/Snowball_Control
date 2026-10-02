# MK20 development access

## Working topology and current address

The addresses below describe the network topology for MK20 development. The live MK20 connects to the development PC's local Wi-Fi LAN (e.g. `192.168.1.248`). TCP ADB is reachable over the local Wi-Fi network. Check your current DHCP lease and USB enumeration before connecting.

- Product USB protocol: `VID 1D6B:PID 0104` (ConfigFS CDC ACM serial)
- Linux development shell: TCP ADB at `192.168.1.248:5555` (port 5555)
- Device identity: `20080411`
- Linux: Tina Linux 4.0.0, kernel 5.4.61, ARMv7 (Allwinner T113)
- Wi-Fi development network: Configured via `dev-access.conf` on microSD

The HUD daemon (`/mnt/SDCARD/mk20-hud`) and the TCP ADB daemon run simultaneously.

Copy `dev-access.conf.example` to `dev-access.conf` and set your 2.4 GHz Wi-Fi
credentials plus the development PC's Wi-Fi MAC address. The real configuration
is intentionally ignored by Git. Deploy both `lunch.sh` and
`dev-access.conf` to the root of the MK20 microSD card.

## Connect from Development Workstation

Connect your PC to the same Wi-Fi network as the MK20, then use ADB platform-tools:

```bash
adb connect 192.168.1.248:5555
adb -s 192.168.1.248:5555 shell
```

Or on Windows with local relay:

```powershell
$adb = "adb"
& $adb connect 192.168.1.248:5555
& $adb -s 192.168.1.248:5555 shell
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
```

Set `MK20_ADB` when `adb.exe` is not installed at the default development
location. Pass `-Device 127.0.0.1:15555` when using the dual-NIC relay, or
`-Device address:port` when DHCP changes the MK20 endpoint. The helper's
`192.168.69.27:5555` default describes the original development network.

## Preview security

This Tina image does not include the ADB authentication service. Device
firewall rules therefore allow port 5555 only from the development PC's Wi-Fi
MAC address and drop other connections to that port. `lunch.sh` connects the
device Wi-Fi, obtains its DHCP lease, launches TCP-only ADB, and reapplies the
rules at boot without changing the USB gadget.

If the development PC's Wi-Fi adapter changes, update `DEV_PC_MAC` in
`dev-access.conf` before replacing the device copy at `/mnt/SDCARD/lunch.sh`.

## Device-side files

- `/mnt/SDCARD/lunch.sh`: Wi-Fi + TCP-only ADB + firewall boot hook
- `/mnt/SDCARD/dev-access.conf`: ignored site credentials and allowed PC MAC
- `/mnt/SDCARD/dev-access.log`: bootstrap log
- `/mnt/SDCARD/adbd-configfs.init.factory`: factory copy of the ADB init script
- `/etc/init.d/adbd`: development copy with `ADB_TRANSPORT_PORT=5555`

## Snowball image backup and restore

Back up the entire SD card before applying the Snowball image. If an update
fails, write the backup image back to the SD card. The deployment scope and
procedure are maintained in the [central document](../../../docs/ARCHITECTURE.md).
The helper's legacy factory-reset command is outside this deployment procedure.

## Source evidence

- `package/utils/adb/adbd-configfs.init` starts ADB and supports
  `ADB_TRANSPORT_PORT`.
- `package/utils/adb/src/adb.c` enables TCP transport from that environment
  variable.
- `package/PCMonitorApp/setusbconfig` owns the product serial gadget mode.
- `package/PCMonitorApp/qt_app1` executes `/mnt/SDCARD/lunch.sh` as root.

변경 전 SD 전체 이미지와 파일 백업을 권장합니다. 이전 SD 내용 분석 대신 우리가 배포하는 Snowball 이미지와 백업 이미지 복원을 검증합니다. 현재 설계·QMK 필수 조건·Preview 보안 경계는 [중앙 문서](../../../docs/ARCHITECTURE.md)를 참고하세요.
