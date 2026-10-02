# MK20 QMK Keyboard Firmware Guide & Architecture

## 1. Directory Structure

```text
hardware/mk20/qmk/
├── README.md                               # 본 아키텍처 및 펌웨어 설명서
├── bin/                                    # 빌드 완료된 배포용 바이너리
│   ├── syk_keyboards_mk20_plus_via.bin     # MK20-PLUS VIA 펌웨어 (NO_USB_STARTUP_CHECK 적용)
│   ├── syk_keyboards_mk10_via.bin          # MK10 VIA 펌웨어
│   ├── firmware-download.png               # QMK Toolbox 플래싱 가이드 스크린샷
│   ├── README.en.md                        # 제조사 릴리즈 노트 (영문)
│   └── README.zh-CN.md                     # 제조사 릴리즈 노트 (중문)
└── source/                                 # QMK 펌웨어 소스 코드
    ├── bootload/
    │   └── README.md                       # STM32duino USB DFU 부트로더 정보
    └── qmk/
        ├── MK10/                           # MK10 전용 QMK 소스
        └── mk20_plus/                      # MK20-PLUS 전용 QMK 소스
            ├── config.h                    # 핀맵, 인코더 해상도 및 비동기 시작 설정
            ├── info.json                   # 4x5 매트릭스 핀, 인코더 및 USB VID/PID (0x4250:0x426F)
            ├── mcuconf.h                   # STM32/GD32 MCU 클록 및 직렬 포트 구성
            ├── mk20_plus.c                 # UART(/dev/ttyS1) 프레이밍, 노브 버튼/인코더 에스컬레이션
            ├── rules.mk                    # NO_USB_STARTUP_CHECK 및 NO_SUSPEND_POWER_DOWN 빌드 옵션
            └── keymaps/
                ├── default/
                └── via/                    # 기본 VIA 키맵 (KC_0 .. KC_J)
```

---

## 2. 문제 원인 분석 (Root Cause Analysis)

### 이전 펌웨어의 문제점
- 기존 MK20 QMK 펌웨어는 PC Host USB 연결이 없을 때(예: 독립 전원 공급, 또는 USB 케이블 미연결 시) 키보드가 완전히 멈추고 키 입력이 작동하지 않았습니다.
- **근본 원인**: QMK의 기본 USB 스택(ChibiOS/STM32)은 부팅 시 USB Host의 열거(Enumeration)를 대기하는 루프(`while (usbGetDriverStateI(&USB_DRIVER) != USB_ACTIVE)`)에 진입합니다. Host USB가 없으면 이 루프에서 무한 대기하므로, 키 매트릭스 스캔(`matrix_scan`)과 내부 UART 통신(`/dev/ttyS1`)이 전혀 시작되지 않았습니다.

### 새 펌웨어의 해결책
1. **`NO_USB_STARTUP_CHECK = yes` 활성화**:
   - 부팅 시 USB Host 열거 여부를 기다리지 않고 즉시 메인 루프를 시작하도록 변경되었습니다.
   - PC USB 연결 유무와 무관하게 전원이 인가되는 즉시 키 매트릭스 스캔과 UART 통신이 활성화됩니다.
2. **`NO_SUSPEND_POWER_DOWN = yes` 추가**:
   - USB VBUS가 차단되거나 PC가 절전(Suspend) 모드에 진입해도 키보드 MCU가 저전력 절전 모드로 들어가지 않고 키 스캔 및 UART 통신을 계속 유지합니다.

---

## 3. 동작 시나리오 및 통신 규약

### A. 독립 전원 부팅 (별도로 부팅할 때, Non-Host Mode)
- QMK MCU는 전원 인가 즉시 실행되어 `/dev/ttyS1` UART 리스너를 열고 키 매트릭스를 스캔합니다.
- Tina Linux의 `mk20-hud` 데몬이 구동되면 UART를 통해 QMK와 통신하며, 키 입력 이벤트는 UART 프레임(`0x16`, `id_custom_report_key_state`)을 통해 Tina Linux로 정상 에스컬레이션됩니다.
- USB Host가 없으므로 HID 패킷 전송 시도는 안전하게 드롭되며 MCU가 블로킹되지 않습니다.

### B. PC Host USB 연결 (HID로 연결될 때)
- PC에 USB 연결 시 `VID 0x4250 / PID 0x426F` 복합 HID 장치(키보드, 마우스, 미디어 키, Raw HID)로 정상 인식됩니다.

### C. HOST 작동 vs HID 작동 전환 (두 노브 동시 클릭)
- **노브 하드웨어 구조**:
  - 좌측 노브 버튼: MCU 핀 `B5` (행 100)
  - 우측 노브 버튼: MCU 핀 `B4` (행 103)
  - `mk20-hud`는 초기화 시 행 100~108의 키코드를 `0x0000 (KC_NO)`로 설정하여 노브 이벤트가 항상 UART로 전송되도록 보장합니다.
- **전환 메커니즘**:
  - 두 노브를 동시에 누르면(`g_left_down && g_right_down`) `mk20-hud`가 이를 감지하여 `g_pc_keys_on` 플래그를 토글합니다.
  - **HOST 모드 (`g_pc_keys_on = 0`, 기본값)**:
    - 20개 매트릭스 키코드를 모두 `0x0000 (KC_NO)`로 설정합니다.
    - 키를 누르면 UART를 통해 Tina Linux/Snowball Control로만 이벤트가 전달되고, PC에는 타이핑되지 않습니다.
  - **HID 모드 (`g_pc_keys_on = 1`)**:
    - 20개 매트릭스 키코드를 원래의 키맵(`KC_0` ~ `KC_J`)으로 복원합니다.
    - 키를 누르면 Tina Linux 에스컬레이션과 함께 PC에 실제 USB 키보드 입력이 전송됩니다.

---

## 4. 펌웨어 플래싱 방법 (Firmware Flashing Guide)

### 준비물
- [QMK Toolbox 다운로드](https://qmk.fm/toolbox) (Windows용 설치)

### 플래싱 절차
1. MK20의 USB 케이블을 PC에서 분리합니다.
2. MK20 키패드의 **맨 왼쪽 위 첫 번째 키 (Row 0, Col 0 - `KC_0` 위치)**를 손가락으로 누른 상태를 유지합니다.
3. 키를 누른 상태에서 USB 케이블을 PC에 연결합니다.
4. 연결 1~2초 후 키에서 손을 뗍니다. 기기가 STM32duino DFU 부트로더 모드로 진입합니다.
5. QMK Toolbox를 실행하면 하단 콘솔에 노란색 메시지로 장치 인식 메시지가 표시됩니다:
   ```text
   *** STM32duino device connected: ...
   ```
6. **Local file** 항목의 `Open` 버튼을 눌러 새 펌웨어 바이너리를 선택합니다:
   - 파일 경로: `hardware/mk20/qmk/bin/syk_keyboards_mk20_plus_via.bin`
7. 우측 상단의 **Flash** 버튼을 클릭합니다.
8. 플래싱이 완료(Success)되면 USB 케이블을 한 번 뽑았다가 다시 연결합니다.
