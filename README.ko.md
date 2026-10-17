<p align="center">
  <img src="assets/banner.png" alt="Snowball Banner" width="100%">
</p>

<h1 align="center">
  <img src="assets/icon.png" width="48" height="48" valign="middle" alt="Snowball Icon">
  Snowball Control — Preview
</h1>

<p align="center">
  <strong>Snowball의 inter-Harness 로컬 제어를 위한 물리 데스크 터미널</strong>
</p>

<p align="center">
  <a href="README.md">English</a> | <a href="README.ko.md">한국어</a>
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-Apache_2.0-blue.svg" alt="License"></a>
  <img src="https://img.shields.io/badge/Node.js-%3E%3D22.12-green.svg" alt="Node.js">
  <img src="https://img.shields.io/badge/Platforms-Tina%20Linux%20%7C%20QMK-orange.svg" alt="Platforms">
  <a href="https://github.com/fkiller/Snowball_Middleware#one-shot-install"><img src="https://img.shields.io/badge/Install-Snowball-purple.svg" alt="Install Snowball"></a>
</p>

---

<a id="one-shot-install"></a>
## Snowball 한 번에 설치

**PC마다 Snowball을 한 번 설치하세요.** 기본 설치에 Web UI, MK20 검색·연결·음성 런타임, M5Stack 검색·게이트웨이, Codex·Antigravity·OpenCode 하네스 플러그인 3종이 모두 포함됩니다. 기기 프로필을 고르거나 USB 기기를 연결할 필요가 없습니다.

사용하는 OS를 펼친 뒤 명령을 복사하세요.

<details open>
<summary><strong>Windows · PowerShell</strong></summary>

```powershell
& ([scriptblock]::Create((irm 'https://raw.githubusercontent.com/fkiller/Snowball_Middleware/main/install.ps1')))
```

</details>

<details>
<summary><strong>macOS · Terminal</strong></summary>

```bash
curl -fsSL https://raw.githubusercontent.com/fkiller/Snowball_Middleware/main/install.sh | bash
```

</details>

<details>
<summary><strong>Linux · Terminal</strong></summary>

```bash
curl -fsSL https://raw.githubusercontent.com/fkiller/Snowball_Middleware/main/install.sh | bash
```

</details>

설치가 끝나면 트레이 아이콘에서 **http://127.0.0.1:8765/** Web UI와 설정·일시정지/재개·로그인 자동 시작을 관리합니다. Windows는 Node/Git/Python을 준비합니다. macOS/Linux는 curl·Git·venv/pip을 포함한 Python >=3.12가 필요하며 Linux는 그래픽 데스크톱과 Electron 시스템 라이브러리도 필요합니다. 네이티브 하네스 앱 설치와 로그인은 각 공급자를 통해 준비하세요.

<a id="update-snowball"></a>
## Snowball 업데이트

### 1. PC 소프트웨어 업데이트

**기존에 설치한 폴더를 사용하세요.** 아래는 기본 경로 예시입니다. 다른 폴더(예: `Snowball-LAN`)에 설치했다면 첫 줄을 그 경로로 바꾸세요. 기기 프로필 옵션은 넣지 않습니다.

<details open>
<summary><strong>Windows · PowerShell</strong></summary>

```powershell
$installRoot = Join-Path $env:LOCALAPPDATA 'Snowball'
& ([scriptblock]::Create((irm 'https://raw.githubusercontent.com/fkiller/Snowball_Middleware/main/install.ps1'))) -InstallRoot $installRoot -Update
```

</details>

<details>
<summary><strong>macOS · Terminal</strong></summary>

```bash
install_root="$HOME/Library/Application Support/Snowball"
curl -fsSL https://raw.githubusercontent.com/fkiller/Snowball_Middleware/main/install.sh | bash -s -- --root "$install_root" --update
```

</details>

<details>
<summary><strong>Linux · Terminal</strong></summary>

```bash
install_root="${XDG_DATA_HOME:-$HOME/.local/share}/Snowball"
curl -fsSL https://raw.githubusercontent.com/fkiller/Snowball_Middleware/main/install.sh | bash -s -- --root "$install_root" --update
```

</details>

업데이트는 기기 설정·페어링 키·컨트롤러 상태·음성 캐시·API 포트·로그인 자동 시작 선택을 보존합니다. 이전 단일 기기 설치에도 두 기기 어댑터를 포함합니다. PC 업데이트로 **Included(제공)** 후보를 갱신한 뒤, 기기 펌웨어는 아래 절차로 적용합니다.

<a id="update-device"></a>
### 2. 기기 펌웨어 업데이트

MK20 런타임 **0.2.7**은 화면 폭에 맞춘 대화 Markdown 미리보기(제목·목록·코드·인용·표)와 Neo둥근모 기반의 또렷한 16×16 한글 비트맵 글꼴을 제공합니다. PC 소프트웨어를 먼저 업데이트한 뒤 아래 기기 설정 창에서 MK20 런타임을 검토·업데이트하세요. 구형 LAN 업데이트는 해시가 같은 파일을 유지하고 실제 여유 공간을 확인합니다. QMK는 별도 구성요소이며 이 후보의 실물 인수는 아직 미확인입니다.

1. PC 업데이트 후 기기를 USB 데이터 케이블로 연결하고 트레이에서 **기기 설정…** 메뉴를 엽니다.
2. 기기를 선택하고 **Installed(현재) → Included(제공)** 버전과 개선 사항을 확인합니다.
3. 기종을 확인한 뒤 **설치 / 업데이트**를 선택합니다. 복구 백업 검증과 설정 보존 절차를 거쳐 쓰기를 진행합니다.
4. 재기동과 실행 버전 확인이 끝날 때까지 USB 연결을 유지합니다. 필요하면 같은 창의 Wi-Fi 안내를 따릅니다.

[기기별 상세 절차와 복구](https://github.com/fkiller/Snowball_Middleware/blob/main/README.ko.md#update-device) · [현재 M5Stack 제공 버전과 개선 사항](https://github.com/fkiller/Snowball_Device_M5Stack/blob/main/firmware/release.json)

<details>
<summary>기기 설정 화면 예시</summary>

<p align="center">
  <img src="assets/screenshots/m5stack_device_setup_ko.png" width="100%" alt="M5Stack 기기 설정: 현재 0.5.0, 제공 0.5.2 및 전체 플래시 백업 진행 화면"><br>
  <em>사용자 제공 캡처(2026-10-12): 0.5.0 → 0.5.2 업데이트의 전체 플래시 백업이 19%(3.0/16.0 MiB) 진행 중인 예시입니다.</em>
</p>

<p align="center">
  <img src="assets/screenshots/mk20_device_setup_ko.png" width="100%" alt="MK20 한국어 기기 설정: LAN 관리 연결, 제공 런타임 0.2.6 및 런타임 파티션 백업 진행 화면"><br>
  <em>사용자 제공 캡처(2026-10-12): MK20 LAN 관리 연결에서 제공 런타임 0.2.6으로 업데이트하기 위한 파티션 백업 진행 예시입니다. 현재 버전의 미확인은 버전을 확인하지 못했다는 뜻이며, 업데이트 완료 화면은 아닙니다.</em>
</p>

</details>

## 기기에서 PC 연결

MK20은 Wi-Fi 연결 후 **K17 · Machines**에서 PC를 선택합니다. M5Stack 펌웨어 0.4.0 이상은 **Machines**에서 등록 PC나 새 PC를 선택하여 LAN으로 페어링합니다. PC 추가에는 전체 PC 설치만 필요하며 USB 등록 스크립트는 필요 없습니다. 기기 포트를 허용하고 클라이언트 격리를 해제한 같은 신뢰 사설 LAN을 사용하세요.

<details>
<summary><strong>고급 옵션</strong></summary>

| 목적 | Windows | macOS / Linux |
| --- | --- | --- |
| 기존 설치 폴더 | `-InstallRoot 경로` | `--root 경로` |
| 사설 LAN 인터페이스 지정 | `-Bind PC의_사설_IP` | `--bind PC의_사설_IP` |
| API 포트 지정 | `-Port 포트` | `--port 포트` |
| 설치 후 실행하지 않기 | `-NoStart` | `--no-start` |
| 바로가기 생략 | `-NoShortcut` | `--no-shortcut` |
| 진단용 부분 구성 | `-Profile web\|mk20\|m5stack` | `--profile web\|mk20\|m5stack` |

일반 설치·업데이트는 두 기기와 하네스 3종을 함께 포함합니다. 사설 LAN이 없거나 선택이 모호하면 검색은 대기하고 로컬 Web UI는 유지합니다. Windows가 묻는 경우 미들웨어 Node.js의 개인 네트워크 접근을 허용하세요. Linux의 USB/시리얼 접근은 일반 OS 권한을 사용합니다. Unix 전체 옵션은 `bash install.sh --help`로 확인합니다. 설치 폴더의 **Start-Snowball.ps1** / **Start-Snowball.sh**는 다운로드 없이 다시 실행합니다.

</details>

M5Stack **0.5.6**는 머신 목록에서 식별되지 않은 “기존 등록 PC” 항목을 숨기고 구 키는 삭제하지 않습니다. 실제 호스트 식별자가 있는 PC만 선택하며, 읽기 입력 시 받은 실제 내용과 스크롤바를 즉시 이동하고 아직 응답받지 못한 행에는 로딩을 표시합니다. 호스트의 자동 LAN 선택은 알려진 Hyper-V·WSL·VPN 어댑터 이름을 제외하며 명시적 bind와 대상 기기 서브넷 선택을 우선합니다. PC 업데이트는 호스트 검색 수정과 펌웨어 후보를 제공하고, 기기 쓰기는 설정 창에서 검토·전체 백업·설정 보존 후 진행합니다. 0.5.6의 손으로 A/C 조작 검증은 남아 있으며 업데이트 전 진단 탐색에서는 실제 395의 0·12·24행 수신을 확인했습니다.

[공통 아키텍처와 검증 범위](https://github.com/fkiller/Snowball_Control/blob/main/docs/ARCHITECTURE.ko.md)

[Control · MK20](https://github.com/fkiller/Snowball_Control) · [Middleware](https://github.com/fkiller/Snowball_Middleware) · [M5Stack](https://github.com/fkiller/Snowball_Device_M5Stack) · Harness: [Codex](https://github.com/fkiller/Snowball_Harness_Codex), [Antigravity](https://github.com/fkiller/Snowball_Harness_Antigravity), [OpenCode](https://github.com/fkiller/Snowball_Harness_OpenCode)

---

## 🌟 개요 (Overview)

**Snowball은 inter-Harness 로컬 제어 인터페이스입니다.** 개발자가 MK20 데스크 터미널과 로컬 Web Supervisor에서 Codex, AGY, OpenCode를 공통된 조작 방식으로 탐색하고 제어합니다. 사용자가 하네스·프로젝트·세션을 선택하고, 각 하네스는 고유한 실행 환경·이력·모델·권한을 유지합니다.

제품의 철학은 **제어권을 사용자의 손과 컴퓨터에 두는 것**입니다. 도구를 오가고 작업을 확인하는 동작은 즉각적이어야 하며, 기능은 실제 설치 환경에서 발견하고 결과와 한계는 사실대로 보여줘야 합니다. 자세한 [제품 철학](docs/ARCHITECTURE.ko.md#제품-철학-inter-harness-로컬-제어)은 아키텍처 문서에서 중앙 관리합니다.

**Preview:** 개인 PC와 신뢰하는 내부 네트워크 전용입니다. 최초 범주는 Codex, AGY, OpenCode, MK20과 로컬 Web UI입니다. UDP/개발 ADB의 보안(인증 및 암호화)을 생략한 부분은 [현재 설계 문서](docs/ARCHITECTURE.ko.md)에 명시합니다.

**Snowball Control**은 MK20 데스크 터미널의 물리적 인터페이스 펌웨어(QMK), Allwinner T113 Tina Linux용 네이티브 C HUD 엔진, 하드웨어 연동 도구 및 STT 참조 호스트를 제공합니다.

전체 설치와 실행은 위의 [Snowball Middleware](https://github.com/fkiller/Snowball_Middleware) 공통 진입점을 사용합니다. 이 저장소는 MK20 기기 구성 요소이며, Control의 `host/npm start`는 Codex 전용 참조 런타임입니다.

---

## 🎥 하드웨어 시연 및 비주얼 쇼케이스 (Visual Showcase)

### 1. 계층 네비게이션 및 음성 세션 실기기 시연
MK20에서 구동되는 브레드크럼 계층 네비게이션(`기기 > 하네스 > 프로젝트 > 세션`), 내장 마이크 음성 프롬프트 캡처, 모달 뷰(좌측 상단 타이틀 겸 Close 버튼, 열별 단축키, 4×4 그리드 캔버스), 그리고 로터리 노브 선택 동작 시연입니다:

<p align="center">
  <img src="assets/screenshots/mk20_navigation_demo.gif" width="480" alt="MK20 네비게이션 시연"><br>
  <em>실물 하드웨어에서 동작하는 브레드크럼 네비게이션 및 모달 제어.</em><br>
  <a href="assets/videos/mk20_navigation_demo.mp4">▶️ 고화질 전체 MP4 영상 보기</a> &nbsp;|&nbsp; <a href="https://x.com/fkiller/status/2099345561627382015">🔗 X (Twitter) 원본 글</a>
</p>

### 2. UI 엘리먼트 및 키 매트릭스 테스트
20개 기계식 스위치 위의 개별 128×128 LCD 키캡 화면, 듀얼 로터리 엔코더 노브, 동적 명암비 반전 테마, 초저지연 프레임버퍼 렌더링 물리 테스트입니다:

<p align="center">
  <img src="assets/screenshots/mk20_ui_elements_test.gif" width="400" alt="MK20 UI 엘리먼트 테스트"><br>
  <em>키 매트릭스 디스플레이, 다이얼 및 동적 테마 렌더링 물리 테스트.</em><br>
  <a href="assets/videos/mk20_ui_elements_test.mp4">▶️ 고화질 전체 MP4 영상 보기</a> &nbsp;|&nbsp; <a href="https://x.com/fkiller/status/2095861881281917119">🔗 X (Twitter) 원본 글</a>
</p>

### 3. 웹 수퍼바이저 대시보드 (Web Supervisor)
탐색된 작업공간, 세션 저널, MK20 하드웨어 연결 상태를 실시간으로 모니터링하는 로컬 루프백 제어 평면(`http://127.0.0.1:8765/`) 화면입니다:

<p align="center">
  <img src="assets/screenshots/web_supervisor_dashboard_ko.png" width="100%" alt="Snowball 웹 수퍼바이저 대시보드">
</p>

### 4. MK20 오프라인 대기 화면 및 연결 상태
선택한 미들웨어의 유효한 화면이 없으면 **Machines(K17)**를 계속 표시하여 페어링·재시도·전환할 수 있습니다. 선택하면 실제 PC 이름과 `Connecting to PC`를 표시하며, 8초 동안 유효한 화면이 없으면 `No PC response`를 표시합니다. 최근 알림을 받은 `available`과 실제 화면을 받은 `connected`를 구분합니다. 일반 조작은 선택 PC의 유효한 화면과 scope를 확인한 뒤 가능합니다. 저장된 선택은 페어링된 PC를 다시 발견하고 유효한 화면이 돌아오면 복귀합니다.

| 미연결 Machines 안내 (`/dev/fb17`) | 선택 PC의 화면 응답 없음 (`/dev/fb21`) |
| :---: | :---: |
| <img src="assets/screenshots/mk20_pairing_key17.png" width="128" alt="실제 미연결 Machines 키"> | <img src="assets/screenshots/mk20_no_pc_response.png" width="428" alt="실제 선택 PC의 화면 응답 시간 초과"> |

2026-10-08에 런타임 0.2.1에서 캡처한 실제 프레임버퍼입니다. 응답 없음 화면은 미해결 연결을 기록하며 PC 페어링/제어 성공을 뜻하지 않습니다. 교차 구성 검증과 남은 실물 확인은 [중앙 문서](docs/ARCHITECTURE.ko.md#변경-영향과-문서-동기화)에 기록합니다.

---

## ⌨️ MK20 하드웨어 준비 (MK20 Setup)

- **수정된 MK20 QMK 업데이트 필수**: USB host 연결 없이 독립 전원에서 키 입력 스캔이 멈추는 원래 QMK 버그를 해결하기 위해, 수정된 QMK 펌웨어 업데이트가 **필수**입니다. [플래싱 안내](hardware/mk20/qmk/README.ko.md)를 따르세요.
- **변경 전 복구 백업 필수**: 제품 USB 업데이트는 변경하는 런타임 파티션 전체 백업을 검증합니다. 전체 카드 이미지 설치·복원은 고급 유지보수 작업입니다. [배포·백업·복원 절차](docs/ARCHITECTURE.ko.md#sd-이미지-배포백업복원)를 참고하세요.
- **Wi-Fi 설정**: 저장된 Wi-Fi가 없을 때 공통 설정 창에서 가져오거나 직접 입력하며 기존 설정을 보존합니다. 실제 접속 정보는 Git에 포함하지 않습니다. [기기 연결 도구](hardware/mk20/dev-tools/README.md)를 참고하세요.
- **네이티브 HUD 데몬**: 현재 HUD는 Tina Linux(ARMv7)의 C 데몬이며 상단 428×142, 키 128×128 framebuffer를 사용합니다.
- **로컬 STT**: CUDA 또는 CPU 추론을 지원합니다. 기본 설치가 MK20의 Python 의존성을 준비하고, 런타임이 실제 하드웨어에 맞는 로컬 모델을 확인·다운로드합니다.

---

## M5Stack + FACES 실기 영상

[![M5Stack + FACES](https://raw.githubusercontent.com/fkiller/Snowball_Device_M5Stack/main/assets/screenshots/m5stack_navigation_demo.gif)](https://github.com/fkiller/Snowball_Device_M5Stack/blob/main/assets/videos/m5stack_navigation_demo.mp4)

[기기 펌웨어와 설치](https://github.com/fkiller/Snowball_Device_M5Stack) · [GitHub 전체 MP4](https://github.com/fkiller/Snowball_Device_M5Stack/blob/main/assets/videos/m5stack_navigation_demo.mp4) · [X](https://x.com/fkiller/status/2106892916149158101?s=20)

---

## 🧪 검사 및 테스트 (Verification & Tests)

```bash
# 하드웨어 플러그인 계약 테스트 (13개 통과)
npm test --prefix plugins/device-mk20

# Linux <-> GD32/QMK 시리얼 계약 테스트 (14개 통과)
powershell -File hardware/mk20/contract/Test-QmkProtocol.ps1

# 호스트 참조 단위 테스트 (52개 통과)
npm test --prefix host
```

---

## 📁 저장소 구조 (Repository Structure)

```text
Snowball_Control/
├── assets/                     # 브랜딩, 부트로더 로고, 시연 영상 및 기기 캡처 스크린샷
│   ├── banner.png
│   ├── icon.png
│   ├── bootlogo.bmp            # U-Boot 로고 (160x160 24-bit BMP)
│   ├── mk20-plus.bin           # 상단 디스플레이 부팅 리소스 (428x142 RGB565)
│   ├── screenshots/            # 실제 기기 프레임버퍼 캡처, GIF 및 웹 UI 화면
│   │   ├── mk20_navigation_demo.gif
│   │   ├── mk20_ui_elements_test.gif
│   │   ├── web_supervisor_dashboard_ko.png
│   │   ├── web_supervisor_dashboard_en.png
│   │   ├── mk20_no_pc_response.png
│   │   ├── mk20_pairing_key17.png
│   │   └── online_top_display.png
│   └── videos/                 # 원본 고화질 MP4 녹화 영상
│       ├── mk20_navigation_demo.mp4
│       └── mk20_ui_elements_test.mp4
├── docs/                       # 아키텍처 및 상세 사양서 (단일 설계 원천)
│   ├── ARCHITECTURE.md         # 영문 아키텍처 사양서 (기본)
│   └── ARCHITECTURE.ko.md      # 한글 아키텍처 사양서
├── hardware/mk20/
│   ├── contract/               # Tina Linux <-> QMK 하드웨어 시리얼 계약
│   ├── dev-tools/              # MK20 부트스트랩 스크립트, lunch.sh & mk20ctl
│   ├── hud/                    # Tina Linux용 초저지연 네이티브 C HUD 엔진
│   └── qmk/                    # 독립 구동 지원 수정 QMK 펌웨어 소스 및 바이너리
├── host/                       # 참조 호스트 런타임 및 STT 어댑터
├── plugins/device-mk20/        # 격리된 하드웨어 전송 계층 및 스킨 엔진
├── LICENSE                     # Apache-2.0 라이선스
├── README.md                   # 영문 기본 README
└── README.ko.md                # 한글 README
```

---

## 📄 라이선스와 공개 범위 (License)

Snowball 자체 코드는 [Apache-2.0](LICENSE)입니다. QMK와 Allwinner T113 펌웨어 소스는 제조사로부터 직접 이메일로 제공받았습니다. QMK 기반 펌웨어의 GPL 고지와 제조사·제3자 구성의 조건은 별개입니다. 제조사 SDK/BSP, 개인 설정, SD 백업은 이 저장소에 포함하지 않습니다. 배포 대상과 빌드 기록은 [단일 설계 문서](docs/ARCHITECTURE.ko.md)에서 관리합니다.
