<p align="right">
  <a href="README.md">English</a> | <strong>한국어</strong>
</p>

# MK20 개발 접근 가이드

## 동작 토폴로지 및 현재 주소

아래 주소는 MK20 개발을 위한 네트워크 구성을 설명합니다. 실물 MK20은 개발 PC의 로컬 Wi-Fi LAN(예: `192.168.1.248`)에 연결됩니다. TCP ADB는 로컬 Wi-Fi 네트워크를 통해 접근할 수 있습니다. 연결하기 전에 현재 DHCP 할당 주소와 USB 열거 상태를 확인하세요.

- 기기 USB 프로토콜: `VID 1D6B:PID 0104` (ConfigFS CDC ACM 시리얼)
- Linux 개발 쉘: `192.168.1.248:5555`의 TCP ADB (포트 5555)
- 기기 ID: `20080411`
- Linux: Tina Linux 4.0.0, 커널 5.4.61, ARMv7 (Allwinner T113)
- Wi-Fi 개발 네트워크: microSD 카드의 `dev-access.conf`로 설정

HUD 데몬(`/mnt/SDCARD/mk20-hud`)과 TCP ADB 데몬은 동시에 실행됩니다.

`dev-access.conf.example`을 `dev-access.conf`로 복사하고 2.4 GHz Wi-Fi 접속 정보 및 개발 PC의 Wi-Fi MAC 주소를 설정하세요. 실제 설정 파일은 Git에서 제외됩니다. `lunch.sh`와 `dev-access.conf`를 MK20 microSD 카드의 루트 디렉터리에 배치하세요.

## 개발 워크스테이션에서 연결

PC를 MK20과 동일한 Wi-Fi 네트워크에 연결한 후 ADB platform-tools를 사용합니다:

```bash
adb connect 192.168.1.248:5555
adb -s 192.168.1.248:5555 shell
```

Windows 환경에서 로컬 릴레이 사용 시:

```powershell
$adb = "adb"
& $adb connect 192.168.1.248:5555
& $adb -s 192.168.1.248:5555 shell
```

기대되는 쉘 권한:

```text
uid=0(root) gid=0(root)
```

## `mk20ctl`

호스트 헬퍼 스크립트는 기기 COM 포트 감지와 네트워크 ADB 작업을 통합 제공합니다:

```powershell
./mk20ctl.ps1 doctor
./mk20ctl.ps1 info
./mk20ctl.ps1 put -Source ./file.bin -Destination /mnt/SDCARD/file.bin
./mk20ctl.ps1 put -Transport Com -Source ./recovery.sh -Destination /mnt/SDCARD/lunch.sh
./mk20ctl.ps1 shell
```

기본 위치에 `adb.exe`가 설치되어 있지 않은 경우 `MK20_ADB` 환경변수를 설정하세요. 듀얼 NIC 릴레이 사용 시 `-Device 127.0.0.1:15555`를 전달하고, DHCP 변경 시 `-Device address:port`를 전달합니다. 헬퍼의 기본값 `192.168.69.27:5555`는 초기 개발 환경의 기록입니다.

## Preview 보안

이 Tina 이미지에는 ADB 인증 서비스가 포함되어 있지 않습니다. 따라서 기기 방화벽 규칙은 개발 PC의 Wi-Fi MAC 주소에서 들어오는 5555 포트 연결만 허용하고 그 외의 연결은 차단(DROP)합니다. `lunch.sh`는 부팅 시 Wi-Fi를 연결하고, DHCP 임대를 획득한 후, TCP 전용 ADB를 실행하며, USB 가젯 구성을 변경하지 않고 방화벽 규칙을 적용합니다.

개발 PC의 Wi-Fi 어댑터가 변경되면, `/mnt/SDCARD/lunch.sh`를 기기에 교체하기 전에 `dev-access.conf`의 `DEV_PC_MAC`을 먼저 업데이트하세요.

## 기기 내부 파일

- `/mnt/SDCARD/lunch.sh`: Wi-Fi + TCP 전용 ADB + 방화벽 부팅 훅
- `/mnt/SDCARD/dev-access.conf`: 무시되는 현장 접속 정보 및 허용된 PC MAC
- `/mnt/SDCARD/dev-access.log`: 부트스트랩 로그
- `/mnt/SDCARD/adbd-configfs.init.factory`: ADB 초기화 스크립트의 공장 원본 복사본
- `/etc/init.d/adbd`: `ADB_TRANSPORT_PORT=5555`가 설정된 개발용 복사본

## Snowball 이미지 백업 및 복원

Snowball 이미지를 적용하기 전에 SD 카드 전체를 백업하세요. 업데이트에 실패한 경우 백업 이미지를 다시 SD 카드에 기록하여 복원합니다. 배포 범위와 운영 절차는 [아키텍처 문서](../../../docs/ARCHITECTURE.ko.md)에서 관리합니다.

## 소스 근거

- `package/utils/adb/adbd-configfs.init`: ADB를 시작하고 `ADB_TRANSPORT_PORT`를 지원합니다.
- `package/utils/adb/src/adb.c`: 해당 환경변수로부터 TCP 전송을 활성화합니다.
- `package/PCMonitorApp/setusbconfig`: 제품 시리얼 가젯 모드를 소유합니다.
- `package/PCMonitorApp/qt_app1`: root 권한으로 `/mnt/SDCARD/lunch.sh`를 실행합니다.

변경 전 SD 전체 이미지와 파일 백업을 권장합니다. 이전 SD 내용 분석 대신 우리가 배포하는 Snowball 이미지와 백업 이미지 복원을 검증합니다. 현재 설계·QMK 필수 조건·Preview 보안 경계는 [아키텍처 문서](../../../docs/ARCHITECTURE.ko.md)를 참고하세요.
