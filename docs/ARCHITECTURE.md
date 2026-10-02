# ARCHITECTURE (전체 시스템 아키텍처 사양서) — Snowball Control & Middleware

## 1. Executive Summary & North Star Vision (개요 및 핵심 비전)

Snowball Control은 올위너 T113(듀얼코어 ARM Cortex-A7)과 QMK 키보드 MCU(STM32/GD32)가 탑재된 하드웨어 데스크 터미널(MK20)을 개발자의 로컬 AI 코딩 전용 물리 제어 패널(HUD)로 탈바꿈시키는 로컬-퍼스트 시스템입니다.

### 불변 원칙: Host-Owned Intelligence
- **호스트 머신(Host)**: 개발 도구, Git 저장소, 언어 서버, AI 에이전트 네이티브 런타임(Codex app-server, Antigravity CLI, OpenCode 등)의 소유자이자 두뇌 역할을 담당합니다.
- **데스크 터미널(MK20)**: 고속 상태 시각화(Display), 오디오 피드백/캡처, 초저지연 물리 키 입력(승인, 거절, 취소, 모드 변경)을 전담하는 씬 클라이언트(Thin Client)입니다.

---

## 2. System Topology Overview (시스템 전체 구조)

```mermaid
flowchart TD
    subgraph Hardware ["1. Hardware Desk Terminal (MK20)"]
        Display["640x656 Composite LCD (Top 400x120 HUD + 20x 128x128 Keys)"]
        Knobs["2x Rotary Encoders (Navigation & Volume)"]
        Mics["Hardware Mic Array (ALSA PCM Stream)"]
        QmkMCU["QMK Keyboard MCU (STM32F103/GD32F303)"]
    end

    subgraph Transport ["2. Transport & Device Layer"]
        UDP["LAN UDP Transport (192.168.1.x:7701)"]
        HID["USB Raw HID / CDC Transport (VID 4250:426F / 1D6B:0104)"]
        ADB["ADB Audio Relay (127.0.0.1:15555)"]
    end

    subgraph MiddlewareCore ["3. Core Middleware Engine (Node.js ESM)"]
        CtxMgr["ContextManager (LCD UI, Mode Engine, State Synchronization)"]
        Registry["DeviceRegistry & SessionManager"]
        LocalApi["LocalApi & Web Supervisor (127.0.0.1:8765 noAuth)"]
        WhisperWorker["Resident Whisper Worker (CUDA / Metal / CPU int8 Fallback)"]
    end

    subgraph Discovery ["4. Living Discovery Layer"]
        CatScanner["HarnessCatalogScanner (CLI / Cache Live Query)"]
        SessScanner["HarnessSessionScanner & DB Scanner (SQLite / Transcript)"]
    end

    subgraph Dispatcher ["5. Host Execution Dispatcher"]
        Dispatch["harness-dispatch.mjs (Unified Native Execution)"]
    end

    subgraph Harnesses ["6. Native AI Harness Engines"]
        Codex["Codex App-Server (Stdio JSON-RPC)"]
        Antigrav["Antigravity CLI (stream-json NDJSON)"]
        OpenCode["OpenCode CLI (JSON CLI Run)"]
    end

    Display <--> UDP
    Knobs --> UDP
    QmkMCU <--> HID
    Mics --> ADB

    UDP <--> CtxMgr
    HID <--> CtxMgr
    ADB --> WhisperWorker

    WhisperWorker --> CtxMgr
    CtxMgr <--> Registry
    Registry <--> LocalApi

    CtxMgr --> CatScanner
    CtxMgr --> SessScanner

    CtxMgr --> Dispatch
    Dispatch --> Codex
    Dispatch --> Antigrav
    Dispatch --> OpenCode
```

---

## 3. Deliverables Organization (산출물 구성)

### 3.1 Firmware Deliverables (`mk20-firmware`)
물리 MK20 하드웨어 타깃 산출물:

| Component | Target Architecture | Location / Image | Primary Responsibilities |
|---|---|---|---|
| **GD32 / QMK Firmware** | ARM Cortex-M3/M4 (STM32F103/GD32F303) | `hardware/mk20/qmk/` (Flash `0x08002000`) | 20키 매트릭스 스캔, 듀얼 로터리 인코더, VIA 프로토콜, 내부 UART(`/dev/ttyS1`), 비동기 단독 부팅(`NO_USB_STARTUP_CHECK`, `NO_SUSPEND_POWER_DOWN`), 듀얼 노브 HOST ↔ HID 모드 전환. |
| **Tina Linux OS & BSP** | ARMv7l Cortex-A7 (Allwinner T113) | Flash Partitions (`/rom`, `/overlay`) | Linux 커널 5.4.61, USB ConfigFS Gadget (CDC ACM `1D6B:0104`), RTL8188GU Wi-Fi 드라이버, ALSA 오디오 드라이버. |
| **Device HUD Daemon** | Tina Linux Userspace (Native C) | `/mnt/SDCARD/mk20-hud` *(공장 복구용 레거시: `/data/KeyboardDevice`)* | 640x656 LCD 프레임버퍼 렌더링(`/dev/fb0`), UDP 7701 프로토콜 서버, 키맵 백업(`/mnt/SDCARD/snowball-keymap.bin`), 호스트 키 이벤트 에스컬레이션. |
| **MicroSD Recovery Hook** | FAT32 MicroSD (`/dev/mmcblk0p1`) | `/mnt/SDCARD/lunch.sh` | 제로 리스크 부트 훅: Wi-Fi 설정, TCP `adbd` (포트 5555) 실행, MAC 방화벽 규칙 적용, 공장 롤백 안전 보장. |

### 3.2 Gateway & Middleware Deliverables (`snowball-gateway`)
개발자 호스트 머신(Windows / Mac / Linux) 실행 산출물:

| Component | Implementation | Primary Responsibilities |
|---|---|---|
| **Transport Layer** | Protocol Driver (Node.js UDP / `Mk20Protocol.psm1`) | 와이어 직렬화, IEEE 802.3 CRC32 무결성 검증, 이중 채널 연결 관리 (Wi-Fi UDP/TCP + USB CDC/HID). |
| **ContextManager** | State Machine Engine (`scripts/context-manager.mjs`) | 하네스 전환 -> Scope Key 재계산 -> 세션 자동 복원 -> Model/Effort 동적 바인딩 -> 하네스 간 입력/백그라운드 격리 -> 20개 키 및 상단 LCD 렌더링. |
| **Living Discovery** | Scanner Layer (`scripts/harness-catalog-scanner.mjs`, `scripts/harness-db-scanner.py`) | 로컬 캐시/CLI 동적 질의 (OpenCode 46개 모델, Antigravity 14개 모델, Codex 롤아웃), SQLite 및 Antigravity 듀얼 DB(`antigravity` + `antigravity-cli`) 스캔 & 데스크톱 앱 자동 동기화. |
| **Host Dispatcher** | Execution Router (`scripts/harness-dispatch.mjs`) | Codex Stdio JSON-RPC, Antigravity stream-json, OpenCode JSON CLI 스트리밍 실행, 실시간 델타 라우팅 및 데스크톱 앱 동기화 훅(`syncCodexThread`, `syncAntigravityConversation`). |
| **Multi-Backend STT Engine** | Python Worker & Assessor (`scripts/assess_stt_backend.py`, `scripts/ensure_stt_runtime.py`, `host/dist/audio/whisper_worker.py`) | Apple Metal -> NVIDIA CUDA(`cublas64_12.dll`) -> Vulkan -> CPU 폴백 엔진, `config/stt.json` 기반 모델 제약(floor/ceiling/exclude), 모델 캐시 자동 다운로드 및 언인스톨 삭제 수명주기 관리. |
| **Web Supervisor** | Local Web UI (`packages/api`, `apps/supervisor`) | `http://127.0.0.1:8765/`: 로컬 루프백 전용 무인증(`noAuth`) 세션 모니터링 및 실시간 제어. |

---

## 4. 계층별 상세 아키텍처 (Detailed Architecture by Layer)

### Layer 1: Hardware Desk Terminal (MK20)
- **Display Architecture (640x656 Framebuffer `/dev/fb0`)**:
  - 물리적으로 단일 640x656 RGB565 LCD 패널을 사용하며, 소프트웨어적으로 상단 HUD와 하단 키 매트릭스로 영역 분할:
    1. **Top Display (HUD / Reader, 400x120)**:
       - 상단 1줄: 호스트 머신명, 활성 하네스, 스피커 볼륨(%), 음소거 상태.
       - 상단 2줄: 현재 작업 프로젝트 및 세션명 (턴 수 표기).
       - 본문 4줄: 에이전트 응답 및 유저 프롬프트(한글 폰트 지원), 세션/모델/파일 브라우저 목록.
       - **텍스트 래핑 및 공백 제거 (`wrapWithPrefix`)**: 빈 줄에서 접두사(`[USER] `, `> `)가 단독 출력되지 않도록 정돈하며, 연속된 3개 이상의 빈 줄을 단일 빈 줄로 압축.
       - **최신 발화 자동 스크롤 (`readerScrollLine`)**: 새 세션/턴 로드 시 뷰포트가 최신 사용자 프롬프트 행 인덱스로 자동 이동하여 수동 스크롤 없이 최신 대화 즉시 표시.
       - **프롬프트 점프 네비게이션**: K19(Prev Prompt) 및 K15(Next Prompt)로 긴 대화 내역 간 유저 질문 위치로 신속 점프.
    2. **Key Matrix Display (4x5 Key LCDs, 각 128x128)**:
       - Row 0 (K20, K16, K12, K8, K4): Voice Talk / Send / Control actions.
       - Row 1 (K18, K14, K10, K6, K2): Model / Effort / Access / View Mode (Files / Changes / Settings).
       - Row 2 (K19, K15, K11, K7, K3): 에디터 활성화 시 다이내믹 선택 버튼 (Item 1~5) 또는 프롬프트 점프 / Diff 토글.
       - Row 3 (K17, K13, K9, K5, K1): Modal Escape / Harness 순환 / Project 선택 / Session 선택 / New Task 생성.
- **Dual Rotary Encoders**:
  - Left Knob: 세션 텍스트 스크롤, 리스트 브라우징, 클릭 시 선택 커밋 / 파일 토글 (MCU 핀 `B5`, 행 100).
  - Right Knob: 볼륨 조절 (0~100%), 클릭 시 음소거 토글 (MCU 핀 `B4`, 행 103).
  - **Dual-Knob Chord Switching (HOST ↔ HID 모드 전환)**:
    - 좌/우 노브 버튼을 동시에 누르면(`g_left_down && g_right_down`) Tina Linux `mk20-hud`가 감지하여 `g_pc_keys_on` 플래그를 토글.
    - **HOST 모드 (`g_pc_keys_on = 0`, 기본값)**: 20개 매트릭스 키코드를 `0x0000 (KC_NO)`로 동적 마스킹하여 PC 타이핑을 억제하고 UART를 통해 Snowball Agent/HUD 제어만 수행.
    - **HID 모드 (`g_pc_keys_on = 1`)**: 원래 팩토리/사용자 키맵(`KC_0` ~ `KC_J`)을 복원하여 PC에 일반 USB 키보드 타이핑 전송과 로컬 HUD 제어를 동시 수행.
- **QMK Keyboard MCU Subsystem (`hardware/mk20/qmk/`)**:
  - **MCU**: STM32F103 / GD32F303 running QMK Firmware + STM32duino DFU Bootloader (`0x4250:0x426F`).
  - **Non-Host / Standalone Boot Fix (`NO_USB_STARTUP_CHECK = yes`, `NO_SUSPEND_POWER_DOWN = yes`)**:
    - 기존 QMK의 USB Host 열거 대기 루프(`while (usbGetDriverStateI != USB_ACTIVE)`)를 우회하여 PC USB 연결 없이 단독 전원 공급 시에도 전원 인가 즉시 매트릭스 스캔 및 내부 UART(`/dev/ttyS1`) 통신 활성화.
    - PC 절전(Suspend)이나 USB 분리 시 MCU 슬립 방지.
  - **Internal UART Bus**: T113 Linux(`/dev/ttyS1`) ↔ QMK MCU 간 115200 8N1 통신. 키 상태 변경 시 `0x16 (id_custom_report_key_state)` 패킷 에스컬레이션.
  - **Keymap Resilience**: `/mnt/SDCARD/snowball-keymap.bin` 저장 및 단독 부팅 시 팩토리 키맵 자동 폴백 복원력 탑재.

### Layer 2: Transport & Device Plugins
- **LAN Transport (`packages/device-lan`, `plugins/device-mk20`)**:
  - UDP 기반 고속 프레임버퍼 전송 (포트 7701).
  - 와이어 포맷: `KeyReportWire` (키 눌림/뗌 이벤트, 플래그), `FramebufferPacket` (압축/비압축 프레임버퍼 데이터그램).
- **USB HID / CDC (`packages/device-hid`)**:
  - QMK 복합 HID (`0x4250:0x426F`): 표준 키보드, 마우스, Raw HID 엔드포인트.
  - USB Gadget CDC ACM (`0x1D6B:0x0104`): 유선 직렬 데이터 채널.
- **ADB Audio Relay**:
  - MK20 내부 T113 리눅스 보드의 ALSA 마이크 스트림을 로컬 포트 `15555`를 통해 16kHz 모노 PCM으로 스트리밍.

### Layer 3: Core Middleware Engine
- **`ContextManager` (`scripts/context-manager.mjs`)**:
  - MK20의 상태 머신 총괄: Harness 전환 -> Scope Key 재계산 -> Project/Session 자동 복원 -> Model/Effort 동적 바인딩 -> 20개 LCD 키 및 상단 LCD 렌더링.
  - 뷰 모드 지원: 기본 대화 뷰, Changes 뷰(24줄 압축 Git Diff), Workspace 뷰(전체 디렉터리 브라우저 및 파일 뷰어), Settings 뷰.
  - **하네스 간 입력 및 백그라운드 태스크 완벽 격리 (Cross-Harness Input & Task Isolation)**:
    - 음성 녹음/전사(`isRecordingVoice`, `voiceDraftText`) 및 AI dispatch 요청은 대상 세션 인스턴스(`targetSessionObj`)에 직접 바인딩.
    - Harness A에서 음성 녹음/전사를 시작하고 즉시 Harness B로 전환(Key 13)해도, Harness A의 백그라운드 전사 및 전송은 고유 세션 컨텍스트 내에서 온전히 완료되며 타 하네스 화면에 교차 오염되지 않음.
    - 하네스 복귀 시 `restoreActiveSessionState()`를 통해 본래의 draft 텍스트와 전송 상태가 100% 복원됨.
  - **새 세션 생성(Key 1) 턴 오염 차단 (Zero Turn Inheritance)**:
    - Key 1 동작 시퀀스: 출발 세션 상태 저장 -> `setSessionTurns([])`로 대화 버퍼 즉시 초기화 -> 신규 `s-*` 세션 unshift -> `selectedSessionIdx = 0` 설정 -> `restoreActiveSessionState()` 호출. (이전 세션의 턴이 신규 세션에 재저장되던 `selectSession(0)` 사이드이펙트 제거).
    - `loadCurrentSessionTurns()` 및 dispatch 완료 시점에도 아직 발송되지 않은 신규 `s-*` 세션은 디스크 캐시나 이전 세션의 `turns`를 절대 상속받지 않고 항상 빈 배열에서 시작.
- **`LocalApi` & Web Supervisor (`packages/api`, `apps/supervisor`)**:
  - `http://127.0.0.1:8765/`: 로컬 전용 웹 UI.
  - 무인증(`noAuth`) 및 PIN 입력 없는 즉각적인 세션 모니터링 및 실시간 설정 (기기 네트워크 페어링 보안과 분리된 로컬 루프백 전용).
- **Multi-Backend STT Engine & Hardware Fallback Graph**:
  - **판정 엔진 (`scripts/assess_stt_backend.py`)**:
    - `START -> Apple Silicon? (Metal) -> NVIDIA? (CUDA Driver + cuBLAS cublas64_12.dll 검증) -> Vulkan -> CPU fallback`.
    - Workstation/서버급 CPU(Threadripper 등 다중 CCD)의 경우 Infinity Fabric 포화를 방지하기 위해 스레드 수를 최적화(최대 16 스레드)로 캡핑.
  - **다계층 의존성 설치기 (`scripts/ensure_stt_runtime.py`)**:
    - 시스템 진단 후 누락된 런타임 DLL(예: CUDA 환경의 `cublas64_12.dll` / `nvidia-cublas-cu12`) 및 백엔드 의존성을 자동 설치하여 전사 에러 원천 차단.
  - **설정 엔진 (`config/stt.json`, `packages/core/src/stt-config.ts`)**:
    - `floor`: 시스템 사양이 낮아도 최소 보장할 모델 (기본값: `small`).
    - `ceiling`: 최상위 허용 모델 (기본값: `large-v3-turbo`).
    - `exclude`: 제외할 모델 목록 (예: `['medium']`).
  - **캐시 및 언인스톨 수명주기**:
    - 모델 캐시: `%APPDATA%\Snowball\Middleware\models` (또는 `~/.cache/snowball/models`).
    - 미들웨어 시작 시 하드웨어 최적 모델 자동 검증/다운로드.
    - 언인스톨러: `keep_data` 시 모델 보존, `remove_all` 시 다운로드된 STT 모델 및 사용자 비밀키 완전 삭제.

### Layer 4: Living Model & Session Discovery
- **`HarnessCatalogScanner` (`scripts/harness-catalog-scanner.mjs`)**:
  - **OpenCode**: `opencode models` CLI 질의 + `~/.cache/opencode/models.json` (46개 모델 및 `reasoning_options` 파싱).
  - **Antigravity**: `agy.exe models` CLI 질의 (14개 모델 및 DisplayName 내 Effort 파싱).
  - **Codex**: 롤아웃 메타데이터 및 RPC 모델 스펙 질의.
- **`HarnessSessionScanner` & `scripts/harness-db-scanner.py`**:
  - **OpenCode**: SQLite DB (`~/.local/share/opencode/opencode.db`) 직접 쿼리.
  - **Codex**: 세션 인덱스 (`~/.codex/session_index.jsonl`) 및 세션 롤아웃 JSONL 파싱.
  - **Antigravity 듀얼 DB 스캔 & 양방향 동기화**:
    - CLI(`agy.exe`) 전용 DB(`~/.gemini/antigravity-cli/conversation_summaries.db`)와 데스크톱 앱 DB(`~/.gemini/antigravity/conversation_summaries.db`)를 동시 스캔.
    - CLI에서 생성된 세션 발견 시 `brain/<cid>` 폴더와 SQLite 레코드를 데스크톱 앱 DB(`app_data_dir = 'antigravity'`)로 자동 동기화하여 Antigravity IDE 앱에서 즉시 노출.
  - **트랜스크립트 정제 (Clean Prompt & Turn Extraction)**:
    - Antigravity `transcript.jsonl` 내부의 시스템 메타데이터(`<\ADDITIONAL_METADATA>`, `<\USER_SETTINGS_CHANGE>`, `<\CONTEXT_SUMMARY>`)를 제거하고, 순수 사용자 프롬프트(`<USER_REQUEST>...</USER_REQUEST>`)만을 정밀 추출하여 MK20 상의 불필요한 줄바꿈 제거.
    - 텍스트가 없는 도구 실행 단계는 `processDetails`로 누적 그룹화하여 빈 에이전트 턴 생성을 차단.

### Layer 5: Host Execution Dispatcher (`scripts/harness-dispatch.mjs`)
- **Codex Runner**: `codex.exe app-server --listen stdio://` 스폰, `thread/start` / `thread/resume` -> `turn/start` 스트리밍.
  - **데스크톱 동기화 (`syncCodexThread`)**: 턴 완료 시 `~/.codex/state_5.sqlite`의 threads 테이블을 업데이트하여 Codex Desktop App에 세션명과 대화가 즉시 반영되도록 보장.
- **Antigravity Runner**: `agy.exe --input-format stream-json --output-format stream-json` 스폰, `step_update.text_delta` 스트리밍 처리.
  - **데스크톱 동기화 (`syncAntigravityConversation`)**: 턴 완료 시 `~/.gemini/antigravity-cli/brain/<cid>`를 `~/.gemini/antigravity/brain/<cid>`로 복사하고 `conversation_summaries.db`를 업데이트하여 Antigravity Desktop App에 세션이 100% 즉시 표시되도록 보장.
- **OpenCode Runner**: `opencode.cmd run --format json --model <model> --dir <cwd> <prompt>` 스폰, 모델 고유 variant(`--variant <variant>`) 자동 매핑. (OpenCode는 CLI와 GUI가 동일한 DB를 공유).

---

## 5. Security & Device Pairing Architecture (Zero-Trust Physical Presence)

### 5.1 Security Threat Model & Security Boundaries
- **로컬 웹 슈퍼바이저 경계**: 개발자 PC 내부 루프백(`127.0.0.1:8765`) 전용이며, 인증/PIN 없이 즉시 열려야 합니다.
- **기기 네트워크 경계 (MK20 Wi-Fi)**: 로컬 Wi-Fi 상의 무인증 접근 위험을 원천 차단하기 위해, MK20 내부 방화벽(`iptables`)은 페어링된 호스트 MAC 주소만 엄격히 인가합니다. 신규 호스트 등록 시 **물리적 존재 증명(Proof of Physical Presence)**을 강제합니다.

### 5.2 Pairing Sequence Diagram

```mermaid
sequenceDiagram
    autonumber
    participant Host as Host Gateway (PC)
    participant T113 as Tina Linux (T113)
    participant QMK as QMK Keyboard MCU
    participant User as Developer (Physical)

    Note over Host, T113: Phase 1: Discovery & Handshake
    Host->>T113: Connect via USB CDC or Wi-Fi Beacon
    Host->>T113: A1 RPC: requestPairing(HostName, HostMAC, HostPubKey)

    Note over T113, User: Phase 2: Physical Challenge Generation
    T113->>T113: Generate 6-digit one-time PIN (e.g., 842-195)
    T113->>T113: Render Pairing Dialog on 640x656 LCD
    T113->>User: Display: "Pairing request from [HostName]. Press Key 0 to Approve"

    Note over User, QMK: Phase 3: Physical Verification
    User->>QMK: Presses Key (0,0) (Physical Approve Switch)
    QMK->>T113: UART /dev/ttyS1: keyStateChanged(row=0, col=0, pressed=1)

    Note over T113, Host: Phase 4: Dynamic Firewall Binding & Trust
    T113->>T113: Store Host MAC & PubKey in /mnt/SDCARD/paired_hosts.json
    T113->>T113: iptables: Add ACCEPT rule for Host MAC on port 5555
    T113-->>Host: A1 RPC: pairingSuccess(DeviceToken, DeviceCert)
    Host->>Host: Store DeviceToken in host configuration
```

### 5.3 Dynamic Firewall Enforcement
페어링 성공 시 Tina Linux는 동적으로 방화벽 규칙을 추가하며 재부팅 후에도 영구 유지합니다:
```bash
iptables -I INPUT 1 -p tcp --dport 5555 -m mac --mac-source <HOST_MAC> -j ACCEPT
```

---

## 6. Control Planes & Failover Resilience (이중 제어 플레인 및 장애 복구)

```text
+-----------------------+                    +-----------------------+
|                       |  USB CDC / HID     |                       |
|   Developer Host PC   |====================|   MK20 Control Panel  |
|   (Snowball Gateway)  |  (Primary Wired)   |    (Tina Linux OS)    |
|                       |                    |                       |
|                       |  Wi-Fi UDP 7701    |                       |
|                       |  & TCP Port 5555   |                       |
|                       |--------------------|                       |
|                       |  (Firewalled Wi-Fi)|                       |
+-----------------------+                    +-----------------------+
```

1. **Wired Control Plane (USB CDC ACM `1D6B:0104` + QMK HID `4250:426F`)**:
   - 케이블 직접 연결로 지연 없는 직렬 데이터 및 복합 HID 입력 지원.
2. **Wireless Control Plane (Firewalled Wi-Fi UDP 7701 & TCP 5555)**:
   - 포트 5555(ADB/진단) 및 UDP 7701(프레임버퍼 렌더링 및 키 이벤트).
   - 페어링된 호스트 MAC에 대해서만 인가된 전용 무선 채널.
3. **Seamless Failover**:
   - 코딩 턴 진행 중 USB 케이블이 분리되거나 PC가 절전 모드에 진입해도, 키보드 MCU(`NO_SUSPEND_POWER_DOWN = yes`)와 게이트웨이가 즉시 방화벽 인가된 Wi-Fi 링크로 세션 전환을 수행하여 에이전트 승인 대기나 작업 문맥이 끊기지 않습니다.

---

## 7. Data Flow & Execution Sequences (데이터 흐름 및 실행 시퀀스)

### 7.1 Standard Turn Execution Sequence (단일 세션 턴 실행 흐름)

```mermaid
sequenceDiagram
    autonumber
    actor User as 사용자
    participant MK20 as MK20 Terminal
    participant Ctx as ContextManager
    participant Whisper as Whisper Worker
    participant Dispatch as Host Dispatcher
    participant Agent as AI Harness CLI

    User->>MK20: K20(Talk) 누름
    MK20->>Ctx: KeyDown(20)
    Ctx->>Whisper: StartRecording()
    User->>MK20: 음성 입력 후 K20/K16(Done)
    MK20->>Ctx: KeyUp(20)
    Ctx->>Whisper: StopAndTranscribe()
    Whisper-->>Ctx: "현재 코드 리팩토링 검토해줘" (Voice Draft)
    Ctx-->>MK20: 상단 LCD에 Draft 프리뷰 표시
    User->>MK20: K16(Send) 누름
    MK20->>Ctx: KeyDown(16)
    Ctx->>Dispatch: dispatchHarnessTurn(harness, session, prompt, model, effort)
    Dispatch->>Agent: Spawn Native Process (CLI/RPC)
    Agent-->>Dispatch: Text Stream Deltas
    Dispatch-->>Ctx: onDelta(chunk)
    Ctx-->>MK20: 상단 LCD 실시간 스트리밍 업데이트
    Agent-->>Dispatch: Final Result (Turn Complete)
    Dispatch-->>Ctx: Complete Session Turns Update
    Ctx-->>MK20: Final Key State & UI 동기화 (최신 발화 자동 스크롤)
```

### 7.2 Cross-Harness Switching & Background Task Lifecycle Sequence (하네스 즉시 전환 및 비동기 작업 격리 흐름)

```mermaid
sequenceDiagram
    autonumber
    actor User as 사용자
    participant MK20 as MK20 Terminal
    participant Ctx as ContextManager
    participant Whisper as Whisper Worker
    participant Dispatch as Host Dispatcher
    participant Codex as Codex (Harness A)
    participant AG as Antigravity (Harness B)

    Note over User, Codex: 1. Start Voice Capture on Harness A
    User->>MK20: K20(Talk) 누름 (Harness: Codex)
    MK20->>Ctx: KeyDown(20) [Target: Codex Session A]
    Ctx->>Whisper: start(capId)

    Note over User, AG: 2. Immediate Switch to Harness B (Key 13)
    User->>MK20: K13 (Harness Cycle)
    MK20->>Ctx: KeyDown(13)
    Ctx->>Ctx: saveActiveSessionState(Session A)
    Ctx->>Ctx: cycleHarness() -> Antigravity
    Ctx->>Ctx: restoreActiveSessionState(Session B)
    Ctx-->>MK20: Paint Antigravity UI (Session B is clean, no recording flags)

    Note over Ctx, Whisper: 3. Background STT Finish & Safe Route
    Whisper-->>Ctx: text: "안녕하세요 하이" (for Session A)
    Ctx->>Ctx: Route draft text strictly into Session A object (Session B UI untouched)

    Note over User, AG: 4. Perform New Task on Antigravity
    User->>MK20: K1(New Task) -> K20(Talk) -> K16(Send)
    Ctx->>Dispatch: runAntigravityTurn(Session B, prompt) [Dispatched in Background]

    Note over User, Codex: 5. Switch Back to Harness A (Key 13)
    User->>MK20: K13 (Harness Cycle) -> Codex
    Ctx->>Ctx: restoreActiveSessionState(Session A)
    Ctx-->>MK20: Displays Voice Draft Preview for Session A!
    User->>MK20: K16(Send)
    Ctx->>Dispatch: runCodexTurn(Session A, "안녕하세요 하이")
    Dispatch->>Codex: Dispatches cleanly without cross-talk
```

---

## 8. Desktop Tray & Web Supervisor Integration (데스크톱 트레이 및 웹 수퍼바이저 통합)

### 8.1 Windows System Tray Lifecycle & Testability
- **구현 구조 (`apps/desktop/launch.mjs` & `apps/desktop/main.mjs`)**:
  - Windows 알림 영역(System Tray) 아이콘은 Electron 네이티브 셸을 통해 생성됩니다 (`Tray`, `Menu`, `nativeImage`).
  - 일반 개발/데몬 실행(`node scripts/start-all.mjs`)은 터미널 콘솔 프로세스로 직접 구동되므로 Electron GUI 셸이 뜨지 않아 작업 표시줄 트레이 아이콘이 노출되지 않습니다.
  - 실제 트레이 앱으로 실행하려면 `npm run start:tray` (`node apps/desktop/launch.mjs`)를 실행하며, 백그라운드 코어 워커(`core-worker.mjs`)를 Electron 메인 프로세스가 관리합니다.
- **트레이 동작 검증 방법**:
  - **헤드리스 스모크 테스트**: `npm run test:tray` (`node apps/desktop/launch.mjs --smoke-test`)
    - Electron 셸을 테스트 모드로 구동하여 `trayCreated: true`, `mainWindows: 0` (메인 윈도우 없이 백그라운드 상주), `loopbackNoPin: true` (무인증 로컬 루프백), `pauseRoundtrip: true` 상태를 100% 자동 검증하고 안전하게 종료됩니다.
  - **시각적 실물 테스트**: `npm run start:tray`
    - 윈도우 우측 하단 시스템 트레이에 Snowball 아이콘이 생성되며, 우클릭 시 컨텍스트 메뉴(대시보드 열기, 제어 일시정지, 자동 시작 토글, 종료)가 정상 작동합니다.

### 8.2 Web Supervisor (`http://127.0.0.1:8765/`) Zero-Simulation Control Parity
- **No-Auth Local Loopback Boundary (Rule 3)**:
  - 사용자의 로컬 브라우저는 PIN/토큰 입력 없이 `127.0.0.1:8765`를 통해 즉각 대시보드에 접근하여 전체 현황을 모니터링 및 제어합니다.
- **살아있는 원천 기반 동적 하네스 & 모델 카탈로그 반영 (Rule 2)**:
  - `connectedHarnesses`를 통해 Codex, Antigravity, OpenCode 3대 하네스가 즉시 인식됩니다.
  - `POST /v1/harness/models` 엔드포인트를 통해 각 하네스의 실시간 CLI/캐시(`agy.exe models`, `opencode models`, `models_cache.json`)에서 추출된 최신 모델 목록과 해당 모델이 지원하는 Effort 옵션이 동적으로 로드됩니다.
- **실물 제어 동등성 (Interactive Dispatch Parity - Rule 1)**:
  - 웹 대시보드에서 프롬프트를 입력하고 Send(전송)할 경우, `journal.enqueue()` 및 `onCommandQueued` 핸들러를 통해 하네스 네이티브 프로세스(`dispatchHarnessTurn`)로 직접 발주됩니다.
  - 턴 완료 시 `realTurnsData` 및 세션 저장소에 대화 기록이 실시간 축적되며, MK20 하드웨어와 웹 대시보드가 완벽한 양방향 상태 동기화를 이룹니다.
