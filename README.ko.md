<p align="center">
  <img src="assets/banner.png" alt="Snowball Control Banner" width="100%">
</p>

<h1 align="center">
  <img src="assets/icon.png" width="48" height="48" valign="middle" alt="Snowball Icon">
  Snowball Control — Preview
</h1>

<p align="center">
  <strong>Physical Terminal Firmware, Tina Linux HUD & Device Integration for AI Coding Agents</strong>
</p>

<p align="center">
  <a href="README.md">English</a> | <a href="README.ko.md">한국어</a>
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-Apache_2.0-blue.svg" alt="License"></a>
  <img src="https://img.shields.io/badge/Node.js-%3E%3D22.12-green.svg" alt="Node.js">
  <img src="https://img.shields.io/badge/Platforms-Tina%20Linux%20%7C%20QMK-orange.svg" alt="Platforms">
  <a href="https://github.com/fkiller/Snowball_Middleware"><img src="https://img.shields.io/badge/Companion-Snowball%20Middleware-purple.svg" alt="Companion Repo"></a>
</p>

---

## 🌟 개요 (Overview)

**Preview:** 개인 PC와 신뢰하는 내부 네트워크 전용입니다. 최초 범주는 Codex, AGY, OpenCode, MK20과 로컬 Web UI입니다. UDP/개발 ADB의 보안(인증 및 암호화)을 생략한 부분은 [현재 설계 문서](docs/ARCHITECTURE.ko.md)에 명시합니다.

**Snowball Control**은 MK20 데스크 터미널의 물리적 인터페이스 펌웨어(QMK), Allwinner T113 Tina Linux용 네이티브 C HUD 엔진, 하드웨어 연동 도구 및 STT 참조 호스트를 제공합니다.

전체 미들웨어와 Web Supervisor는 동반 저장소 [Snowball_Middleware](https://github.com/fkiller/Snowball_Middleware)에 있습니다. 두 저장소를 같은 부모 폴더에 배치하세요. Control의 `host/npm start`는 Codex 전용 참조 런타임입니다.

---

## 🚀 시작하기 (Getting Started)

Node.js 22.12 이상과 Python 3.10 이상을 준비합니다. 미들웨어를 빌드하기 전에 Control의 공용 STT 호스트 라이브러리를 먼저 빌드합니다.

```bash
# 1. Control 호스트 라이브러리 빌드
cd Snowball_Control/host
npm ci
npm run build

# 2. Middleware 빌드 및 Web Supervisor 실행
cd ../../Snowball_Middleware
npm ci
npm run build
npm run start:local
```

Web UI는 `http://127.0.0.1:8765/`에 로그인/PIN 없이 접근합니다. MK20 전체 통합 Preview는 Middleware에서 `node scripts/start-all.mjs`로 실행합니다. 하네스의 실제 CLI 설치·로그인, 기기 네트워크 설정과 로컬 STT 모델이 필요합니다. 모델/effort는 CLI·캐시에서 발견하고 없는 목록을 대신 만들지 않습니다. 현재 읽은 버전과 검증 범위는 [아키텍처 문서](docs/ARCHITECTURE.ko.md)에 있습니다.

---

## ⌨️ MK20 하드웨어 준비 (MK20 Setup)

- **수정된 MK20 QMK 업데이트 필수**: USB host 연결 없이 독립 전원에서 키 입력 스캔이 멈추는 원래 QMK 버그를 해결하기 위해, 수정된 QMK 펌웨어 업데이트가 **필수**입니다. [플래싱 안내](hardware/mk20/qmk/README.ko.md)를 따르세요.
- **변경 전 SD 전체 이미지 백업 권장**: Snowball 이미지를 적용하기 전 카드 전체 백업을 권장하며, 문제 발생 시 백업 이미지를 복원합니다. [배포·백업·복원 절차](docs/ARCHITECTURE.ko.md#sd-이미지-배포백업복원)를 참고하세요.
- **Wi-Fi 설정**: 실제 접속 정보는 Git에서 제외된 SD 카드의 `dev-access.conf`에 설정합니다. [기기 연결 도구](hardware/mk20/dev-tools/README.md)를 참고하세요.
- **네이티브 HUD 데몬**: 현재 HUD는 Tina Linux(ARMv7)의 C 데몬이며 상단 428×142, 키 128×128 framebuffer를 사용합니다.
- **로컬 STT**: CUDA 또는 CPU 추론을 지원합니다. Python 의존성과 모델은 별도로 준비해야 합니다.

---

## 📺 MK20 대기 화면 및 연결 상태 (Standby Screen)

미들웨어가 실행되지 않았거나 PC와의 네트워크 통신(UDP sync)이 끊어지면, 기기는 자동으로 **Snowball 대기 모드(Standby Mode)**로 전환됩니다:
- **상단 디스플레이 (`/dev/fb21`)**: 좌측에 상태 안내("Host Disconnected", "Snowball Standby Mode")와 우측에 128×128 16비트 Snowball 강아지 아이콘을 렌더링합니다.
- **10번 키 (`/dev/fb10`)**: 128×128 Snowball 아이콘이 점등되고, 나머지 키는 백라이트가 소등되어 대기 상태임을 직관적으로 표시합니다.
- **자동 복귀**: PC 미들웨어가 시작되면 실시간 작업 세션 화면으로 즉시 전환됩니다.

| 대기 모드 상단 화면 (`/dev/fb21`) | 대기 모드 10번 키 (`/dev/fb10`) | 미들웨어 연결 완료 (`/dev/fb21`) |
| :---: | :---: | :---: |
| <img src="assets/screenshots/standby_top_display.png" width="300" alt="대기 화면 상단 디스플레이"> | <img src="assets/screenshots/standby_key10.png" width="128" alt="대기 화면 10번 키"> | <img src="assets/screenshots/online_top_display.png" width="300" alt="연결 완료 상단 디스플레이"> |

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
├── assets/                     # 브랜딩, 부트로더 로고 및 기기 캡처 스크린샷
│   ├── banner.png
│   ├── icon.png
│   ├── bootlogo.bmp            # U-Boot 로고 (160x160 24-bit BMP)
│   ├── mk20-plus.bin           # 상단 디스플레이 부팅 리소스 (428x142 RGB565)
│   └── screenshots/            # 실제 기기 프레임버퍼 캡처 이미지
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
