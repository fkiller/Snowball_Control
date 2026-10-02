<p align="center"><img src="assets/banner.png" alt="Snowball Control" width="100%"></p>

# Snowball Control — Preview

MK20 데스크 터미널에서 개인 PC의 AI 하네스를 물리적으로 제어하는 프로젝트입니다. 최초 범주는 Codex, AGY, OpenCode, MK20과 로컬 Web UI입니다. 개인 PC·신뢰하는 내부 네트워크를 위한 Preview이며, UDP/개발 ADB의 보안을 생략한 부분이 있습니다.

이 저장소는 수정 QMK, Tina Linux용 네이티브 HUD, 기기 도구·플러그인·STT 참조 호스트를 제공합니다. 전체 미들웨어와 Web Supervisor는 동반 저장소 [Snowball_Middleware](https://github.com/fkiller/Snowball_Middleware)에 있습니다. 두 저장소를 같은 부모 폴더에 배치하세요. Control `host/npm start`는 Codex 전용 참조 런타임입니다.

## 시작

Node.js 22.12 이상과 Python 3.10 이상을 준비합니다. 미들웨어를 빌드하기 전에 Control의 공용 STT 호스트도 빌드합니다.

```sh
cd Snowball_Control/host
npm ci
npm run build
cd ../../Snowball_Middleware
npm ci
npm run build
npm run start:local
```

Web UI는 `http://127.0.0.1:8765/`에 로그인/PIN 없이 접근합니다. MK20 전체 통합 Preview는 Middleware에서 `node scripts/start-all.mjs`로 실행합니다. 하네스의 실제 CLI 설치·로그인, 기기 네트워크 설정과 로컬 STT 모델이 필요합니다. 모델/effort는 CLI·캐시에서 발견하고 없는 목록을 대신 만들지 않습니다. 현재 읽은 버전과 검증 범위는 [아키텍처](docs/ARCHITECTURE.md)에 있습니다.

## MK20 준비

- USB host 없이 키 입력이 멈추는 원래 QMK 문제 때문에 **수정된 MK20 QMK 업데이트가 필수**입니다. [플래싱 안내](hardware/mk20/qmk/README.md)를 따르세요.
- **변경 전 SD 전체 이미지 백업을 권장**합니다. Snowball 이미지를 적용하고 문제가 생기면 백업 이미지를 복원합니다. [배포·백업·복원 절차](docs/ARCHITECTURE.md#sd-이미지-배포백업복원)를 참고하세요.
- 실제 Wi-Fi 설정은 Git에서 제외된 SD의 `dev-access.conf`에 저장하세요. [기기 연결 도구](hardware/mk20/dev-tools/README.md)를 참고하세요.
- 현재 HUD는 Tina Linux의 C 데몬이며 상단 428×142, 키 128×128 framebuffer를 사용합니다. 빌드에는 별도로 확보한 호환 toolchain/sysroot와 FreeType 헤더가 필요합니다.
- STT는 CUDA 또는 CPU를 사용합니다. Python 의존성과 모델은 별도로 설치해야 합니다.

### MK20 대기 화면 및 연결 상태

미들웨어가 실행되지 않았거나 PC와 통신이 끊어지면, 상단 디스플레이에 상태 안내와 Snowball 아이콘이 표시되고 10번 키에 Snowball 아이콘이 점등되어 대기 상태임을 알립니다. PC 미들웨어가 시작되면 실시간 세션 화면으로 자동 전환됩니다.

| 대기 모드 (상단 화면) | 대기 모드 (10번 키) | 연결 완료 (세션 화면) |
| :---: | :---: | :---: |
| <img src="assets/screenshots/standby_top_display.png" width="300" alt="대기 화면 상단 디스플레이"> | <img src="assets/screenshots/standby_key10.png" width="128" alt="대기 화면 10번 키"> | <img src="assets/screenshots/online_top_display.png" width="300" alt="연결 완료 상단 디스플레이"> |

## 검사

Control `host`에서 `npm test`, `plugins/device-mk20`에서 `npm test`, 저장소 루트에서 `powershell -File hardware/mk20/contract/Test-QmkProtocol.ps1`을 실행합니다. 실제 발화 WAV와 Whisper 환경이 있으면 `host`에서 `npm run test:stt`를 실행하세요. Middleware의 전체 테스트·하네스 전환·실물 화면 검사와 남은 공개 질문은 [단일 설계 문서](docs/ARCHITECTURE.md)에 모았습니다.

## 라이선스와 공개 범위

Snowball 자체 코드는 [Apache-2.0](LICENSE)입니다. QMK와 Allwinner T113 펌웨어 소스는 제조사로부터 직접 이메일로 제공받았습니다. QMK 기반 펌웨어의 GPL 고지와 제조사·제3자 구성의 조건은 별개입니다. 제조사 SDK/BSP, 개인 설정, SD 백업은 이 저장소에 포함하지 않습니다. 배포 대상과 빌드 기록은 [단일 설계 문서](docs/ARCHITECTURE.md)에서 관리합니다.
