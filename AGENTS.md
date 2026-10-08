# AGENTS.md

## Purpose & North Star
Snowball 프로젝트의 본질은 **사용자 컴퓨터 상에서의 로컬 제어(Local Control)**입니다.
하드웨어 데스크 터미널(MK20)과 로컬 미들웨어를 통해 개발자가 자신의 PC에서 실행 중인 다양한 AI 하네스(Codex, Antigravity, OpenCode 등)를 즉각적이고 물리적으로 통제하는 것이 핵심 목적입니다.

---

## Non-Negotiable Core Rules (불변 원칙)

### 1. Zero Simulation (시뮬레이션 전면 금지)
- 가짜 승인, mock 지연 타이머, 정적 성공 메시지 반환 등 흉내 내는 구현은 전면 금지한다.
- MK20 하드웨어 버튼을 눌렀을 때의 동작, 세션 생성, 명령 전송, 음성 텍스트 변환(STT), 모델/Effort 변경은 반드시 실제 네이티브 백엔드 프로세스 및 하드웨어와 연동되어야 한다.

### 2. Living Source of Truth (살아있는 원천 기반 동적 발견)
- 모델 리스트, 지원 Variant(Effort), 세션, 워크스페이스는 절대로 코드 내에 하드코딩하지 않는다.
- 각 하네스의 네이티브 CLI 및 로컬 캐시(예: `opencode models`, `~/.cache/opencode/models.json`, `agy.exe models`, Codex 세션 인덱스 등)를 동적으로 스캔하여 항상 실제 기기 환경과 100% 일치하는 데이터를 제공해야 한다.
- 모델마다 지원하는 Effort 목록이 다른 현실을 반영하여, 모델 선택 시 해당 모델 고유의 Effort 목록만 동적으로 노출한다.

### 3. Local-First & Security Boundary
- 사용자의 로컬 Web UI(Supervisor)는 인증(Auth/PIN) 없이 즉시 접근 가능해야 하며, 외부 인터넷에 노출되지 않는 로컬 루프백 전용이다. (기기 페어링 보안과 분리)
- 클라우드 제어 플레인을 필수 전제로 두지 않으며, 인터넷 연결이 차단되어도 로컬 제어와 데스크 터미널 조작은 온전히 동작해야 한다.

### 4. Plugin Sandbox Isolation
- 플러그인(디바이스 플러그인, 하네스 플러그인)은 보안 격리(Sandbox Isolation)를 유지해야 한다.
- 플러그인이 코어 권한이나 임의의 파일 시스템에 무단 접근하는 것을 제한하되, 호스트 레벨의 실제 명령 실행(Dispatch) 및 상태 관찰(Observer) 기능 자체를 비활성화하는 것은 아니다.

---

## Working & Engineering Rules

### 1. Repository First
- 추측에 기반하여 구조를 변경하지 않고, 항상 기존 코드와 아키텍처, 의존성, 테스트를 먼저 확인한다.
- 불필요한 광범위 리팩토링이나 외부 라이브러리 추가를 지양하고, 기존 단일화된 문서(`docs/NORTH_STAR.md`, `docs/ARCHITECTURE.md`)를 기준으로 작업한다.

### 2. Test Integrity
- 변경 사항 적용 시 전체 테스트 스위트(`npm test`, `tests/mk20-ux-parity.test.mjs`, `scripts/test-harness-switching.mjs`)를 실행하여 회귀(Regression)가 없음을 검증해야 한다.
- 실물 하드웨어 또는 캡처 스크립트(`scripts/dump_mk20_screens.py`)를 통해 MK20 화면 렌더링 상태를 육안 검증한다.

### 3. Direct Collaboration
- 번거로운 Handoff 프로토콜 없이, 사용자와 실시간 대화 및 커밋 기반으로 문제를 해결하고 완결성 있게 작업을 마친다.

### 4. Single Source Architecture & Documentation Hygiene (문서 중앙화, 무결성 및 최소화 원칙)
- **변경과 문서를 함께 반영**: 동작·설치·운영·프로토콜 변경은 완료 보고나 배포 전에 같은 변경 세트에서 관련 `docs/ARCHITECTURE.md`, 한국어 번역 및 사용자/개발자 안내를 갱신한다. 공통 변경은 Web/MK20/M5Stack과 하네스 3종에 대한 영향·검증 범위를 확인하고, 실물 미확인 항목은 명시한다. 기기 전용 변경을 다른 플러그인으로 자동 일반화하지 않는다.
- **`docs/ARCHITECTURE.md`로 중앙화**: 시스템 아키텍처, 하드웨어/펌웨어 사양, 통신 프로토콜, 동작 모드 등 모든 설계 및 변경 사항은 반드시 유일한 단일 문서인 `docs/ARCHITECTURE.md`에만 반영하고 최신 상태를 유지한다.
- **문서 내 레거시/Outdated 내용 정리 및 챕터 중복 금지**:
  - `docs/ARCHITECTURE.md` 내부에 과거 레거시 구현(예: 공장 출하 Qt 앱 등)과 현재 실물 런타임 활성 구현(예: C 네이티브 HUD 데몬 등)이 모호하게 혼재하거나 상충되지 않도록, 항상 최신 실물 런타임 기준으로 레거시 표기를 정리·명시해야 한다.
  - 챕터 간(예: 시스템 토폴로지, 산출물 테이블, 계층별 상세 사양 간) 내용이 모순되거나 불필요하게 겹치지 않도록 구조를 정돈하여 단일 정보원(Single Source of Truth)을 유지한다.
- **중복 문서 생성 금지**: 루트 디렉터리나 하위 폴더에 `ARCHITECTURE.md` 등 유사하거나 파편화된 아키텍처 문서를 중복 생성하지 않는다.
- **문서 수 최소화 (Minimize Document Footprint)**:
  - 새로운 설계나 기능 추가 시 별도의 신규 마크다운 문서를 마구 늘리지 않고, 항상 `docs/ARCHITECTURE.md`의 해당 계층/섹션에 통합·흡수한다.
  - 문서 작업 시 기존 문서를 사전에 중복 체크하여 저장소 전체의 문서 수를 항상 최소한으로 유지한다.
